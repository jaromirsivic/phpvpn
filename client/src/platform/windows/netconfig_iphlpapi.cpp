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

#include <algorithm>
#include <vector>

#include "platform/iface/netconfig.h"

#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "ws2_32.lib")

namespace phpvpn {
namespace {

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

bool NetworkSnapshot::sameDefaultRouteAndDns(const NetworkSnapshot& other) const {
    if (hasDefaultRoute != other.hasDefaultRoute) {
        return false;
    }
    if (hasDefaultRoute && (defaultRouteInterfaceLuid != other.defaultRouteInterfaceLuid ||
                            defaultRouteMetric != other.defaultRouteMetric)) {
        return false;
    }
    return dnsServers == other.dnsServers;
}

std::unique_ptr<NetConfig> createPlatformNetConfig() {
    return std::make_unique<IpHelperNetConfig>();
}

}  // namespace phpvpn
