// IP Helper network configuration.
//
// Every route carries an explicit destination prefix, next hop, interface and
// metric, so nothing depends on a best interface lookup and no global priority
// is ever changed. The snapshot is used by the fail-closed preflight and by the
// verification that the default route and the DNS servers stayed untouched.

// The socket headers have to come before windows.h, otherwise the legacy
// winsock definitions win and the IP Helper types do not compile.
#include <winsock2.h>
#include <ws2tcpip.h>

#include <windows.h>

#include <iphlpapi.h>
#include <netioapi.h>
#include <objbase.h>
#include <setupapi.h>
#include <cfgmgr32.h>

#include <initguid.h>
#include <devguid.h>
#include <devpkey.h>

#include <algorithm>
#include <vector>

#include "platform/iface/netconfig.h"

#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "ole32.lib")

namespace phpvpn {
namespace {

struct DeviceInfoSet {
    HDEVINFO value = INVALID_HANDLE_VALUE;
    ~DeviceInfoSet() {
        if (value != INVALID_HANDLE_VALUE) SetupDiDestroyDeviceInfoList(value);
    }
    DeviceInfoSet(const DeviceInfoSet&) = delete;
    DeviceInfoSet& operator=(const DeviceInfoSet&) = delete;
    explicit DeviceInfoSet(HDEVINFO handle) : value(handle) {}
};

Error phantomInspectionError(const char* operation, DWORD code) {
    return Error(ErrorCode::netconfig_apply_failed,
                 std::string(operation) + " failed (Windows " + std::to_string(code) + ")");
}

bool sameOsName(const wchar_t* left, const wchar_t* right) {
    return CompareStringOrdinal(left, -1, right, -1, TRUE) == CSTR_EQUAL;
}

Status verifyRecordedInterface(const std::wstring& alias, const GUID& guid,
                               std::uint64_t luid) {
    MIB_IF_ROW2 row{};
    row.InterfaceLuid.Value = luid;
    const DWORD result = GetIfEntry2(&row);
    if (result != NO_ERROR) return phantomInspectionError("interface recheck", result);
    if (row.InterfaceLuid.Value != luid || !IsEqualGUID(row.InterfaceGuid, guid) ||
        !sameOsName(row.Alias, alias.c_str())) {
        return Error(ErrorCode::journal_ambiguous, "the recorded interface identity changed");
    }
    return Status::success();
}

Status verifyPhantomDevice(HDEVINFO set, SP_DEVINFO_DATA& device,
                          const std::wstring& instanceId) {
    if (!IsEqualGUID(device.ClassGuid, GUID_DEVCLASS_NET)) {
        return Error(ErrorCode::journal_ambiguous, "the recorded device has a different class");
    }
    wchar_t actualId[MAX_DEVICE_ID_LEN]{};
    if (!SetupDiGetDeviceInstanceIdW(set, &device, actualId, MAX_DEVICE_ID_LEN, nullptr)) {
        return phantomInspectionError("device instance inspection", GetLastError());
    }
    if (!sameOsName(actualId, instanceId.c_str())) {
        return Error(ErrorCode::journal_ambiguous, "the recorded device instance changed");
    }
    DEVPROPTYPE propertyType = 0;
    DEVPROP_BOOLEAN present = DEVPROP_TRUE;
    DWORD propertySize = 0;
    if (!SetupDiGetDevicePropertyW(set, &device, &DEVPKEY_Device_IsPresent, &propertyType,
                                  reinterpret_cast<PBYTE>(&present), sizeof(present),
                                  &propertySize, 0)) {
        return phantomInspectionError("device presence inspection", GetLastError());
    }
    if (propertyType != DEVPROP_TYPE_BOOLEAN || propertySize != sizeof(present) ||
        present != DEVPROP_FALSE) {
        return Error(ErrorCode::journal_ambiguous, "the recorded device is not proven phantom");
    }
    return Status::success();
}

Status verifyNoInterfaceIdentity(const std::wstring& alias, const GUID& guid,
                                 std::uint64_t luid) {
    MIB_IF_TABLE2* table = nullptr;
    const DWORD result = GetIfTable2(&table);
    if (result != NO_ERROR || !table) {
        return phantomInspectionError("interface table inspection", result);
    }
    bool found = false;
    for (ULONG i = 0; i < table->NumEntries; ++i) {
        const auto& row = table->Table[i];
        if (row.InterfaceLuid.Value == luid || IsEqualGUID(row.InterfaceGuid, guid) ||
            sameOsName(row.Alias, alias.c_str())) {
            found = true;
            break;
        }
    }
    FreeMibTable(table);
    if (found) return Error(ErrorCode::journal_ambiguous, "the interface identity still exists");
    return Status::success();
}

Ipv4Address fromSockaddr(const SOCKADDR_INET& address) noexcept {
    if (address.si_family != AF_INET) {
        return Ipv4Address(0);
    }
    const std::uint32_t network = address.Ipv4.sin_addr.S_un.S_addr;
    return Ipv4Address(ntohl(network));
}

void toSockaddr(Ipv4Address address, SOCKADDR_INET& out) noexcept {
    out = SOCKADDR_INET{};
    out.si_family = AF_INET;
    out.Ipv4.sin_family = AF_INET;
    out.Ipv4.sin_addr.S_un.S_addr = htonl(address.value());
}

std::string narrow(const wchar_t* text) {
    if (text == nullptr) {
        return std::string();
    }
    const int required = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    if (required <= 1) {
        return std::string();
    }
    std::string out(static_cast<std::size_t>(required - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text, -1, out.data(), required, nullptr, nullptr);
    return out;
}

class IpHelperNetConfig final : public NetConfig {
public:
    Status removeOwnedPhantomInterface(const std::wstring& alias,
                                       const std::wstring& expectedGuid,
                                       std::uint64_t interfaceLuid) override {
        GUID guid{};
        const GUID emptyGuid{};
        if (alias.empty() || alias.size() > IF_MAX_STRING_SIZE ||
            alias.find(L'\0') != std::wstring::npos || !interfaceLuid ||
            expectedGuid.size() != 38 || expectedGuid.front() != L'{' ||
            expectedGuid.back() != L'}' ||
            CLSIDFromString(expectedGuid.c_str(), &guid) != S_OK ||
            IsEqualGUID(guid, emptyGuid)) {
            return Error(ErrorCode::journal_ambiguous, "the recorded interface identity is invalid");
        }
        wchar_t canonicalGuid[39]{};
        if (!StringFromGUID2(guid, canonicalGuid, 39) ||
            !sameOsName(canonicalGuid, expectedGuid.c_str())) {
            return Error(ErrorCode::journal_ambiguous, "the recorded interface GUID is not canonical");
        }
        const std::wstring instanceId = std::wstring(L"SWD\\WINTUN\\") + canonicalGuid;

        MIB_IF_ROW2 row{};
        row.InterfaceLuid.Value = interfaceLuid;
        const DWORD rowResult = GetIfEntry2(&row);
        if (rowResult != NO_ERROR && rowResult != ERROR_FILE_NOT_FOUND) {
            return phantomInspectionError("interface identity inspection", rowResult);
        }
        if (rowResult == NO_ERROR &&
            (row.InterfaceLuid.Value != interfaceLuid || !IsEqualGUID(row.InterfaceGuid, guid) ||
             !sameOsName(row.Alias, alias.c_str()))) {
            return Error(ErrorCode::journal_ambiguous, "the recorded interface does not match");
        }

        // OpenFlags=0 does not cancel a pending removal. This opens only PnP
        // metadata, including non-present devices, never an adapter/driver.
        DeviceInfoSet set(SetupDiCreateDeviceInfoList(nullptr, nullptr));
        if (set.value == INVALID_HANDLE_VALUE) {
            return phantomInspectionError("device information set creation", GetLastError());
        }
        SP_DEVINFO_DATA device{};
        device.cbSize = sizeof(device);
        if (!SetupDiOpenDeviceInfoW(set.value, instanceId.c_str(), nullptr, 0, &device)) {
            const DWORD error = GetLastError();
            if (error == ERROR_NO_SUCH_DEVINST && rowResult == ERROR_FILE_NOT_FOUND) {
                // A missing PnP node alone is insufficient: inspect all rows,
                // including a GUID/alias that might now map to another LUID.
                return verifyNoInterfaceIdentity(alias, guid, interfaceLuid);
            }
            if (error == ERROR_NO_SUCH_DEVINST) {
                return Error(ErrorCode::journal_ambiguous, "the interface has no matching PnP identity");
            }
            return phantomInspectionError("device instance opening", error);
        }
        if (rowResult != NO_ERROR) {
            return Error(ErrorCode::journal_ambiguous, "the PnP device has no matching interface identity");
        }
        PHPVPN_TRY(verifyPhantomDevice(set.value, device, instanceId));

        SP_DEVINSTALL_PARAMS_W install{};
        install.cbSize = sizeof(install);
        if (!SetupDiGetDeviceInstallParamsW(set.value, &device, &install)) {
            return phantomInspectionError("device installation parameters", GetLastError());
        }
        if (install.Flags & (DI_NEEDREBOOT | DI_NEEDRESTART)) {
            return Error(ErrorCode::journal_ambiguous, "the device already requires an administrator restart");
        }
        install.Flags |= DI_QUIETINSTALL;
        if (!SetupDiSetDeviceInstallParamsW(set.value, &device, &install)) {
            return phantomInspectionError("quiet device removal setup", GetLastError());
        }
        SP_REMOVEDEVICE_PARAMS remove{};
        remove.ClassInstallHeader.cbSize = sizeof(SP_CLASSINSTALL_HEADER);
        remove.ClassInstallHeader.InstallFunction = DIF_REMOVE;
        // GLOBAL refers to hardware profiles of this one instance. It does
        // not select other devices, child devices or a driver package.
        remove.Scope = DI_REMOVEDEVICE_GLOBAL;
        if (!SetupDiSetClassInstallParamsW(set.value, &device, &remove.ClassInstallHeader,
                                          sizeof(remove))) {
            return phantomInspectionError("device removal parameters", GetLastError());
        }

        // SetupAPI has no atomic remove-if-phantom primitive. Recheck as late
        // as possible and require callers to serialize this owned lifecycle.
        PHPVPN_TRY(verifyRecordedInterface(alias, guid, interfaceLuid));
        PHPVPN_TRY(verifyPhantomDevice(set.value, device, instanceId));
        if (!SetupDiCallClassInstaller(DIF_REMOVE, set.value, &device)) {
            return phantomInspectionError("owned phantom device removal", GetLastError());
        }
        install = {};
        install.cbSize = sizeof(install);
        if (!SetupDiGetDeviceInstallParamsW(set.value, &device, &install)) {
            return phantomInspectionError("device removal result inspection", GetLastError());
        }
        if (install.Flags & (DI_NEEDREBOOT | DI_NEEDRESTART)) {
            return Error(ErrorCode::journal_ambiguous, "phantom removal requires an administrator restart");
        }
        // Native success does not prove asynchronous OS metadata convergence.
        // The owner must retain its journal until the postcondition is proven.
        return Status::success();
    }

    Result<bool> interfaceExists(std::uint64_t interfaceLuid) override {
        if (!interfaceLuid) return Error(ErrorCode::netconfig_apply_failed);
        MIB_IF_ROW2 row{};
        row.InterfaceLuid.Value = interfaceLuid;
        const auto result = GetIfEntry2(&row);
        if (result == NO_ERROR) return true;
        if (result == ERROR_FILE_NOT_FOUND) return false;
        return Error(ErrorCode::netconfig_apply_failed, "the recorded interface identity could not be inspected");
    }
    Result<NetworkSnapshot> snapshot() override {
        NetworkSnapshot result;

        MIB_UNICASTIPADDRESS_TABLE* addressTable = nullptr;
        if (GetUnicastIpAddressTable(AF_INET, &addressTable) != NO_ERROR ||
            addressTable == nullptr) {
            return Error(ErrorCode::netconfig_apply_failed,
                         "the address table could not be enumerated");
        }
        for (ULONG index = 0; index < addressTable->NumEntries; ++index) {
            const MIB_UNICASTIPADDRESS_ROW& row = addressTable->Table[index];
            UnicastAddressEntry entry;
            entry.interfaceLuid = row.InterfaceLuid.Value;
            entry.address = fromSockaddr(row.Address);
            entry.prefixLength = row.OnLinkPrefixLength;
            result.addresses.push_back(entry);
        }
        FreeMibTable(addressTable);

        MIB_IPFORWARD_TABLE2* routeTable = nullptr;
        if (GetIpForwardTable2(AF_INET, &routeTable) != NO_ERROR || routeTable == nullptr) {
            return Error(ErrorCode::netconfig_apply_failed,
                         "the route table could not be enumerated");
        }
        for (ULONG index = 0; index < routeTable->NumEntries; ++index) {
            const MIB_IPFORWARD_ROW2& row = routeTable->Table[index];
            const Ipv4Address destination = fromSockaddr(row.DestinationPrefix.Prefix);

            RouteEntry entry;
            entry.interfaceLuid = row.InterfaceLuid.Value;
            entry.nextHop = fromSockaddr(row.NextHop);
            entry.metric = row.Metric;

            const std::string prefix = destination.toString() + "/" +
                                       std::to_string(row.DestinationPrefix.PrefixLength);
            const auto parsed = Ipv4Cidr::parse(prefix);
            if (parsed.has_value()) {
                entry.destination = *parsed;
            }
            result.routes.push_back(entry);

            if (row.DestinationPrefix.PrefixLength == 0 && destination.value() == 0) {
                if (!result.hasDefaultRoute || row.Metric < result.defaultRouteMetric) {
                    result.hasDefaultRoute = true;
                    result.defaultRouteInterfaceLuid = row.InterfaceLuid.Value;
                    result.defaultRouteMetric = row.Metric;
                }
            }
        }
        FreeMibTable(routeTable);

        collectDnsServers(result.dnsServers);
        std::sort(result.dnsServers.begin(), result.dnsServers.end());

        return result;
    }

    Status addUnicastAddress(const UnicastAddressEntry& entry) override {
        MIB_UNICASTIPADDRESS_ROW row{};
        InitializeUnicastIpAddressEntry(&row);
        row.InterfaceLuid.Value = entry.interfaceLuid;
        toSockaddr(entry.address, row.Address);
        row.OnLinkPrefixLength = static_cast<UINT8>(entry.prefixLength);
        row.DadState = IpDadStatePreferred;

        const DWORD status = CreateUnicastIpAddressEntry(&row);
        if (status != NO_ERROR && status != ERROR_OBJECT_ALREADY_EXISTS) {
            return Error(ErrorCode::netconfig_apply_failed,
                         "the tunnel address could not be assigned");
        }
        return Status::success();
    }

    Status removeUnicastAddress(const UnicastAddressEntry& entry) override {
        MIB_UNICASTIPADDRESS_ROW row{};
        InitializeUnicastIpAddressEntry(&row);
        row.InterfaceLuid.Value = entry.interfaceLuid;
        toSockaddr(entry.address, row.Address);
        row.OnLinkPrefixLength = static_cast<UINT8>(entry.prefixLength);

        const DWORD status = DeleteUnicastIpAddressEntry(&row);
        if (status != NO_ERROR && status != ERROR_NOT_FOUND && status != ERROR_FILE_NOT_FOUND) {
            return Error(ErrorCode::netconfig_apply_failed,
                         "the tunnel address could not be removed");
        }
        return Status::success();
    }

    Status addRoute(const RouteEntry& entry) override {
        MIB_IPFORWARD_ROW2 row{};
        InitializeIpForwardEntry(&row);
        row.InterfaceLuid.Value = entry.interfaceLuid;
        toSockaddr(entry.destination.network(), row.DestinationPrefix.Prefix);
        row.DestinationPrefix.PrefixLength =
            static_cast<UINT8>(entry.destination.prefixLength());
        toSockaddr(entry.nextHop, row.NextHop);
        row.Metric = entry.metric;
        row.Protocol = MIB_IPPROTO_NETMGMT;
        // The metric is explicit, so Windows must not add the interface metric.
        row.Loopback = FALSE;

        const DWORD status = CreateIpForwardEntry2(&row);
        if (status != NO_ERROR && status != ERROR_OBJECT_ALREADY_EXISTS) {
            return Error(ErrorCode::netconfig_apply_failed, "the tunnel route could not be added");
        }
        return Status::success();
    }

    Status removeRoute(const RouteEntry& entry) override {
        MIB_IPFORWARD_ROW2 row{};
        InitializeIpForwardEntry(&row);
        row.InterfaceLuid.Value = entry.interfaceLuid;
        toSockaddr(entry.destination.network(), row.DestinationPrefix.Prefix);
        row.DestinationPrefix.PrefixLength =
            static_cast<UINT8>(entry.destination.prefixLength());
        toSockaddr(entry.nextHop, row.NextHop);

        const DWORD status = DeleteIpForwardEntry2(&row);
        if (status != NO_ERROR && status != ERROR_NOT_FOUND && status != ERROR_FILE_NOT_FOUND) {
            return Error(ErrorCode::netconfig_apply_failed, "the tunnel route could not be removed");
        }
        return Status::success();
    }

    Result<std::uint64_t> interfaceLuidForAlias(const std::wstring& alias) override {
        NET_LUID luid{};
        if (ConvertInterfaceAliasToLuid(alias.c_str(), &luid) != NO_ERROR) {
            return Error(ErrorCode::netconfig_apply_failed,
                         "the tunnel interface could not be resolved");
        }
        return luid.Value;
    }

    Result<bool> isHostReachableOutsideInterface(const std::string& host,
                                                 std::uint64_t excludeLuid) override {
        ADDRINFOA hints{};
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;

        ADDRINFOA* resolved = nullptr;
        if (getaddrinfo(host.c_str(), nullptr, &hints, &resolved) != 0 || resolved == nullptr) {
            // A name that cannot be resolved is not treated as reachable, so the
            // caller keeps the fail-closed behaviour.
            return false;
        }

        bool reachable = false;
        for (ADDRINFOA* entry = resolved; entry != nullptr; entry = entry->ai_next) {
            if (entry->ai_family != AF_INET) {
                continue;
            }
            SOCKADDR_INET destination{};
            destination.si_family = AF_INET;
            destination.Ipv4 = *reinterpret_cast<const sockaddr_in*>(entry->ai_addr);

            MIB_IPFORWARD_ROW2 best{};
            SOCKADDR_INET source{};
            if (GetBestRoute2(nullptr, 0, nullptr, &destination, 0, &best, &source) == NO_ERROR) {
                if (best.InterfaceLuid.Value != excludeLuid) {
                    reachable = true;
                    break;
                }
            }
        }
        freeaddrinfo(resolved);
        return reachable;
    }

private:
    static void collectDnsServers(std::vector<std::string>& out) {
        ULONG size = 16 * 1024;
        std::vector<std::uint8_t> buffer(size);
        ULONG status = GetAdaptersAddresses(AF_INET, GAA_FLAG_INCLUDE_ALL_INTERFACES, nullptr,
                                            reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data()),
                                            &size);
        if (status == ERROR_BUFFER_OVERFLOW) {
            buffer.resize(size);
            status = GetAdaptersAddresses(AF_INET, GAA_FLAG_INCLUDE_ALL_INTERFACES, nullptr,
                                          reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data()),
                                          &size);
        }
        if (status != NO_ERROR) {
            return;
        }

        for (auto* adapter = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data());
             adapter != nullptr; adapter = adapter->Next) {
            for (auto* server = adapter->FirstDnsServerAddress; server != nullptr;
                 server = server->Next) {
                if (server->Address.lpSockaddr == nullptr ||
                    server->Address.lpSockaddr->sa_family != AF_INET) {
                    continue;
                }
                const auto* address =
                    reinterpret_cast<const sockaddr_in*>(server->Address.lpSockaddr);
                char text[INET_ADDRSTRLEN] = {};
                if (inet_ntop(AF_INET, &address->sin_addr, text, sizeof(text)) != nullptr) {
                    out.emplace_back(text);
                }
            }
            (void)narrow(adapter->FriendlyName);
        }
    }
};

}  // namespace

std::unique_ptr<NetConfig> createPlatformNetConfig() {
    return std::make_unique<IpHelperNetConfig>();
}

}  // namespace phpvpn
