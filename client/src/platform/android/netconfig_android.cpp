// AndroidNetConfig, see netconfig_android.h (plan-android-v001 points 2, 4, 17).
//
// The parser, the conversion and the NetConfig over the context are pure C++,
// so they run in the host unit tests (T-N02); only the getaddrinfo resolver and
// the platform factory need the Android build.
#include "platform/android/netconfig_android.h"

#if !defined(_WIN32)
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#endif

#include <algorithm>
#include <initializer_list>
#include <limits>
#include <utility>

#include "core/base/utf8.h"
#include "core/protocol/json.h"

namespace phpvpn {
namespace {

constexpr std::size_t kMaxNetworks = 64;
constexpr std::size_t kMaxRowsPerNetwork = 512;
constexpr std::size_t kMaxDnsServers = 16;
constexpr std::size_t kMaxNameBytes = 64;
constexpr std::size_t kMaxAddressTextBytes = 64;

Error malformed(const char* detail) { return Error(ErrorCode::netconfig_apply_failed, detail); }

Error unsupported() {
    return Error(ErrorCode::tunnel_driver_unsupported, "the Android VPN context is not installed");
}

// The object has exactly these members.
bool exactMembers(const JsonValue& object, std::initializer_list<std::string_view> names) {
    if (!object.isObject() || object.objectValue().size() != names.size()) return false;
    for (const auto name : names) {
        if (object.find(name) == nullptr) return false;
    }
    return true;
}

std::optional<std::int64_t> integerIn(const JsonValue* value, std::int64_t minimum, std::int64_t maximum) {
    if (value == nullptr || !value->isInteger()) return std::nullopt;
    const std::int64_t number = value->integerValue();
    if (number < minimum || number > maximum) return std::nullopt;
    return number;
}

bool boolean(const JsonValue* value, bool& out) {
    if (value == nullptr || !value->isBool()) return false;
    out = value->boolValue();
    return true;
}

// A string of at most `limit` bytes, or null when `nullable`.
bool optionalName(const JsonValue* value, std::size_t limit, bool nullable) {
    if (value == nullptr) return false;
    if (value->isNull()) return nullable;
    return value->isString() && value->stringValue().size() <= limit;
}

// IPv6 text is checked only for its alphabet (hex digits, ':', '.' and a
// "%<interface>" scope); the rows are dropped anyway.
bool plausibleIpv6(const std::string& text) {
    if (text.empty() || text.size() > kMaxAddressTextBytes) return false;
    return std::all_of(text.begin(), text.end(), [](char character) {
        return (character >= '0' && character <= '9') || (character >= 'a' && character <= 'z') ||
               (character >= 'A' && character <= 'Z') || character == ':' || character == '.' ||
               character == '%' || character == '-' || character == '_';
    });
}

// IPv4 address, or nullopt for valid IPv6 text; `valid` is false for anything else.
std::optional<Ipv4Address> parseAddress(const JsonValue* value, bool& valid) {
    valid = false;
    if (value == nullptr || !value->isString()) return std::nullopt;
    const std::string& text = value->stringValue();
    if (text.find(':') != std::string::npos) {
        valid = plausibleIpv6(text);
        return std::nullopt;
    }
    auto parsed = Ipv4Address::parse(text);
    valid = parsed.has_value();
    return parsed;
}

// Like the Linux snapshot: only prefixes Ipv4Cidr can express keep their value.
Ipv4Cidr cidrOf(Ipv4Address network, int prefixLength) {
    const auto parsed = Ipv4Cidr::parse(network.toString() + "/" + std::to_string(prefixLength));
    return parsed.has_value() ? *parsed : Ipv4Cidr();
}

bool routeContains(const AndroidRouteRow& route, Ipv4Address address) {
    if (route.prefixLength <= 0) return true;
    const std::uint32_t mask = ~std::uint32_t{0} << (32 - route.prefixLength);
    return (address.value() & mask) == (route.destination.value() & mask);
}

Status parseAddresses(const JsonValue& array, AndroidNetworkRow& network) {
    if (!array.isArray() || array.arrayValue().size() > kMaxRowsPerNetwork) {
        return malformed("the network addresses are malformed");
    }
    for (const JsonValue& row : array.arrayValue()) {
        if (!exactMembers(row, {"interfaceName", "ifindex", "address", "prefixLength"}) ||
            !optionalName(row.find("interfaceName"), kMaxNameBytes, false)) {
            return malformed("a network address is malformed");
        }
        const auto ifindex = integerIn(row.find("ifindex"), 0, std::numeric_limits<std::int32_t>::max());
        bool valid = false;
        const auto address = parseAddress(row.find("address"), valid);
        const auto prefix = integerIn(row.find("prefixLength"), 0, address.has_value() ? 32 : 128);
        if (!ifindex || !valid || !prefix) return malformed("a network address is malformed");
        if (!address.has_value()) continue;
        network.addresses.push_back(
            AndroidAddressRow{androidUnderlyingIdentity(*ifindex), *address, static_cast<int>(*prefix)});
    }
    return Status::success();
}

Status parseRoutes(const JsonValue& array, AndroidNetworkRow& network) {
    if (!array.isArray() || array.arrayValue().size() > kMaxRowsPerNetwork) {
        return malformed("the network routes are malformed");
    }
    for (const JsonValue& row : array.arrayValue()) {
        if (!exactMembers(row, {"interfaceName", "ifindex", "destination", "prefixLength", "gateway", "type",
                                "isDefault"}) ||
            !optionalName(row.find("interfaceName"), kMaxNameBytes, true)) {
            return malformed("a network route is malformed");
        }
        const auto ifindex = integerIn(row.find("ifindex"), 0, std::numeric_limits<std::int32_t>::max());
        bool valid = false;
        const auto destination = parseAddress(row.find("destination"), valid);
        const auto prefix = integerIn(row.find("prefixLength"), 0, destination.has_value() ? 32 : 128);
        bool isDefault = false;
        if (!ifindex || !valid || !prefix || !boolean(row.find("isDefault"), isDefault)) {
            return malformed("a network route is malformed");
        }
        const JsonValue* type = row.find("type");
        if (!type->isString() || (type->stringValue() != "unicast" && type->stringValue() != "unreachable" &&
                                  type->stringValue() != "throw" && type->stringValue() != "other")) {
            return malformed("a network route type is unknown");
        }
        std::optional<Ipv4Address> gateway;
        const JsonValue* gatewayValue = row.find("gateway");
        if (!gatewayValue->isNull()) {
            bool gatewayValid = false;
            gateway = parseAddress(gatewayValue, gatewayValid);
            if (!gatewayValid) return malformed("a network route gateway is malformed");
        }
        if (!destination.has_value()) continue;
        AndroidRouteRow route;
        route.identity = androidUnderlyingIdentity(*ifindex);
        route.destination = *destination;
        route.prefixLength = static_cast<int>(*prefix);
        route.gateway = gateway.value_or(Ipv4Address(0));
        route.unicast = type->stringValue() == "unicast";
        route.isDefault = isDefault;
        network.routes.push_back(route);
    }
    return Status::success();
}

Status parseNetwork(const JsonValue& value, AndroidNetworkRow& network) {
    if (!exactMembers(value, {"handle", "interfaceName", "vpn", "transports", "addresses", "routes", "dnsServers"}) ||
        !optionalName(value.find("interfaceName"), kMaxNameBytes, true) || !boolean(value.find("vpn"), network.vpn)) {
        return malformed("a network is malformed");
    }
    const auto handle = integerIn(value.find("handle"), 0, std::numeric_limits<std::int64_t>::max());
    if (!handle) return malformed("a network handle is malformed");
    network.handle = *handle;

    const JsonValue* transports = value.find("transports");
    if (!transports->isArray() || transports->arrayValue().size() > 8) return malformed("the transports are malformed");
    for (const JsonValue& transport : transports->arrayValue()) {
        static constexpr std::string_view kKnown[] = {"wifi", "cellular", "ethernet", "vpn", "bluetooth", "other"};
        if (!transport.isString() ||
            std::find(std::begin(kKnown), std::end(kKnown), transport.stringValue()) == std::end(kKnown)) {
            return malformed("a transport is unknown");
        }
    }

    PHPVPN_TRY(parseAddresses(*value.find("addresses"), network));
    PHPVPN_TRY(parseRoutes(*value.find("routes"), network));

    const JsonValue* dns = value.find("dnsServers");
    if (!dns->isArray() || dns->arrayValue().size() > kMaxDnsServers) return malformed("the DNS servers are malformed");
    for (const JsonValue& server : dns->arrayValue()) {
        bool valid = false;
        const auto ipv4 = parseAddress(&server, valid);
        if (!valid) return malformed("a DNS server is malformed");
        // The canonical spelling for IPv4, the host's spelling for IPv6.
        network.dnsServers.push_back(ipv4.has_value() ? ipv4->toString() : server.stringValue());
    }
    return Status::success();
}

class AndroidNetConfig final : public NetConfig {
public:
    AndroidNetConfig(std::shared_ptr<AndroidVpnContext> context, AndroidHostResolver resolver)
        : context_(std::move(context)), resolver_(std::move(resolver)) {}

    Result<NetworkSnapshot> snapshot() override {
        if (!context_) return unsupported();
        return androidNetworkSnapshot(*context_);
    }

    Result<bool> interfaceExists(std::uint64_t interfaceLuid) override {
        if (!context_) return unsupported();
        if (interfaceLuid == 0) return Error(ErrorCode::netconfig_apply_failed);
        return context_->holdsInterface(interfaceLuid);
    }

    // The interface of a dead process vanished with its descriptor, so absence
    // is success. A descriptor this process still holds is not a phantom.
    Status removeOwnedPhantomInterface(const std::wstring& alias, const std::wstring& expectedGuid,
                                       std::uint64_t interfaceLuid) override {
        (void)alias;
        (void)expectedGuid;
        if (!context_) return unsupported();
        if (context_->holdsInterface(interfaceLuid)) {
            return Error(ErrorCode::journal_ambiguous, "the recorded interface is still held by this process");
        }
        return Status::success();
    }

    Status addUnicastAddress(const UnicastAddressEntry& entry) override {
        if (!context_) return unsupported();
        if (entry.prefixLength < 0 || entry.prefixLength > 32) {
            return Error(ErrorCode::invalid_argument, "the prefix length is out of range");
        }
        return context_->recordAddress(entry.interfaceLuid, entry.address, entry.prefixLength);
    }

    // Closing the descriptor removes the address with the interface.
    Status removeUnicastAddress(const UnicastAddressEntry& entry) override {
        if (!context_) return unsupported();
        if (entry.prefixLength < 0 || entry.prefixLength > 32) {
            return Error(ErrorCode::invalid_argument, "the prefix length is out of range");
        }
        return ownedOnly(entry.interfaceLuid);
    }

    Status addRoute(const RouteEntry& entry) override {
        if (!context_) return unsupported();
        return context_->recordRoute(entry.interfaceLuid, entry.destination, entry.nextHop);
    }

    // Closing the descriptor removes the route with the interface.
    Status removeRoute(const RouteEntry& entry) override {
        if (!context_) return unsupported();
        return ownedOnly(entry.interfaceLuid);
    }

    Result<std::uint64_t> interfaceLuidForAlias(const std::wstring& alias) override {
        if (!context_) return unsupported();
        std::string name;
        PHPVPN_TRY_ASSIGN(name, toUtf8(alias));
        const auto identity = context_->identityForAlias(name);
        if (!identity.has_value()) {
            return Error(ErrorCode::netconfig_apply_failed, "the tunnel interface could not be resolved");
        }
        return *identity;
    }

    // The client's own traffic never enters the VPN (point 5), so a host is
    // reachable outside `excludeLuid` when the longest matching IPv4 route of
    // the active underlying network leaves over another interface.
    Result<bool> isHostReachableOutsideInterface(const std::string& host, std::uint64_t excludeLuid) override {
        if (!context_) return unsupported();
        const std::vector<Ipv4Address> addresses = resolver_ ? resolver_(host) : std::vector<Ipv4Address>{};
        // An unresolvable name is not reachable, the caller stays fail-closed.
        if (addresses.empty()) return false;
        AndroidNetworkState state;
        PHPVPN_TRY_ASSIGN(state, queryAndroidNetworkState(*context_));
        const AndroidNetworkRow* active = state.activeUnderlying();
        if (active == nullptr) return false;
        for (const Ipv4Address address : addresses) {
            const AndroidRouteRow* best = nullptr;
            for (const AndroidRouteRow& route : active->routes) {
                if (!route.unicast || !routeContains(route, address)) continue;
                if (best == nullptr || route.prefixLength > best->prefixLength) best = &route;
            }
            if (best != nullptr && best->identity != excludeLuid) return true;
        }
        return false;
    }

private:
    static Status ownedOnly(std::uint64_t identity) {
        if (!isAndroidIdentity(identity)) {
            return Error(ErrorCode::netconfig_apply_failed, "the interface identity is not owned by this service");
        }
        return Status::success();
    }

    std::shared_ptr<AndroidVpnContext> context_;
    AndroidHostResolver resolver_;
};

}  // namespace

const AndroidNetworkRow* AndroidNetworkState::activeUnderlying() const {
    if (!activeNetwork.has_value()) return nullptr;
    for (const AndroidNetworkRow& network : networks) {
        if (network.handle == *activeNetwork) return network.vpn ? nullptr : &network;
    }
    return nullptr;
}

std::uint64_t androidUnderlyingIdentity(std::int64_t ifindex) noexcept {
    if (ifindex <= 0 || ifindex >= static_cast<std::int64_t>(kUnknownAndroidInterfaceIdentity)) {
        return kUnknownAndroidInterfaceIdentity;
    }
    return static_cast<std::uint64_t>(ifindex);
}

Result<AndroidNetworkState> parseAndroidNetworkState(std::string_view json) {
    if (json.size() > kMaxAndroidSnapshotBytes) return malformed("the network snapshot is too large");
    auto document = Json::parse(json);
    if (!document.ok()) return malformed("the network snapshot is not valid JSON");
    const JsonValue& root = document.value();
    if (!exactMembers(root, {"version", "activeNetwork", "networks"})) {
        return malformed("the network snapshot has unexpected members");
    }
    if (integerIn(root.find("version"), 1, 1) != std::optional<std::int64_t>(1)) {
        return malformed("the network snapshot version is not supported");
    }

    AndroidNetworkState state;
    const JsonValue* active = root.find("activeNetwork");
    if (!active->isNull()) {
        const auto handle = integerIn(active, 0, std::numeric_limits<std::int64_t>::max());
        if (!handle) return malformed("the active network handle is malformed");
        state.activeNetwork = *handle;
    }

    const JsonValue* networks = root.find("networks");
    if (!networks->isArray() || networks->arrayValue().size() > kMaxNetworks) {
        return malformed("the network list is malformed");
    }
    for (const JsonValue& value : networks->arrayValue()) {
        AndroidNetworkRow network;
        PHPVPN_TRY(parseNetwork(value, network));
        const bool duplicate = std::any_of(state.networks.begin(), state.networks.end(),
                                           [&](const AndroidNetworkRow& row) { return row.handle == network.handle; });
        if (duplicate) return malformed("a network handle is duplicated");
        state.networks.push_back(std::move(network));
    }
    return state;
}

NetworkSnapshot toNetworkSnapshot(const AndroidNetworkState& state) {
    NetworkSnapshot snapshot;
    for (const AndroidNetworkRow& network : state.networks) {
        if (network.vpn) continue;
        for (const AndroidAddressRow& address : network.addresses) {
            snapshot.addresses.push_back(UnicastAddressEntry{address.identity, address.address, address.prefixLength});
        }
        for (const AndroidRouteRow& route : network.routes) {
            if (!route.unicast) continue;
            RouteEntry entry;
            entry.interfaceLuid = route.identity;
            entry.destination = cidrOf(route.destination, route.prefixLength);
            entry.nextHop = route.gateway;
            entry.metric = 0;
            snapshot.routes.push_back(entry);
        }
    }
    if (const AndroidNetworkRow* active = state.activeUnderlying()) {
        for (const AndroidRouteRow& route : active->routes) {
            if (route.unicast && route.isDefault && route.prefixLength == 0) {
                snapshot.hasDefaultRoute = true;
                snapshot.defaultRouteInterfaceLuid = route.identity;
                snapshot.defaultRouteMetric = 0;
                break;
            }
        }
        snapshot.dnsServers = active->dnsServers;
    }
    return snapshot;
}

Result<AndroidNetworkState> queryAndroidNetworkState(AndroidVpnContext& context) {
    const auto host = context.networkHost();
    if (!host) return Error(ErrorCode::netconfig_apply_failed, "the network host is not registered");
    std::string json;
    PHPVPN_TRY_ASSIGN(json, host->snapshotJson());
    return parseAndroidNetworkState(json);
}

Result<NetworkSnapshot> androidNetworkSnapshot(AndroidVpnContext& context) {
    AndroidNetworkState state;
    PHPVPN_TRY_ASSIGN(state, queryAndroidNetworkState(context));
    NetworkSnapshot snapshot = toNetworkSnapshot(state);
    context.noteSnapshot(snapshot);
    return snapshot;
}

#if !defined(_WIN32)
std::vector<Ipv4Address> resolveIpv4Addresses(const std::string& host) {
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* resolved = nullptr;
    std::vector<Ipv4Address> addresses;
    if (getaddrinfo(host.c_str(), nullptr, &hints, &resolved) != 0 || resolved == nullptr) return addresses;
    for (const addrinfo* entry = resolved; entry != nullptr; entry = entry->ai_next) {
        if (entry->ai_family != AF_INET || entry->ai_addr == nullptr) continue;
        const auto* address = reinterpret_cast<const sockaddr_in*>(entry->ai_addr);
        addresses.emplace_back(ntohl(address->sin_addr.s_addr));
    }
    freeaddrinfo(resolved);
    return addresses;
}
#endif

std::unique_ptr<NetConfig> createAndroidNetConfig(std::shared_ptr<AndroidVpnContext> context,
                                                  AndroidHostResolver resolver) {
    return std::make_unique<AndroidNetConfig>(std::move(context), std::move(resolver));
}

#if defined(__ANDROID__)
// The context must be installed before the runtime creates its platform
// objects (WP04, nativeStart); without it every operation is unsupported.
std::unique_ptr<NetConfig> createPlatformNetConfig() {
    return createAndroidNetConfig(AndroidVpnContext::current(), resolveIpv4Addresses);
}
#endif

}  // namespace phpvpn
