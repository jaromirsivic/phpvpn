// Address and route configuration boundary.
//
// client.md "Vlastnictvi adapteru a session" requires an explicit destination
// prefix, next hop, interface and metric for every route. The implementation
// never looks for a best interface and never changes a global priority.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "core/base/result.h"
#include "core/protocol/ipv4.h"

namespace phpvpn {

struct UnicastAddressEntry {
    std::uint64_t interfaceLuid = 0;
    Ipv4Address address;
    int prefixLength = 0;
};

struct RouteEntry {
    std::uint64_t interfaceLuid = 0;
    Ipv4Cidr destination;
    Ipv4Address nextHop;  // on-link routes use 0.0.0.0
    std::uint32_t metric = 0;
};

// Snapshot used by the fail-closed preflight and by the post change
// verification of the default route and the DNS configuration.
struct NetworkSnapshot {
    std::vector<UnicastAddressEntry> addresses;
    std::vector<RouteEntry> routes;
    std::vector<std::string> dnsServers;
    bool hasDefaultRoute = false;
    std::uint64_t defaultRouteInterfaceLuid = 0;
    std::uint32_t defaultRouteMetric = 0;

    bool sameDefaultRouteAndDns(const NetworkSnapshot& other) const;
};

class NetConfig {
public:
    virtual ~NetConfig() = default;

    virtual Result<NetworkSnapshot> snapshot() = 0;

    virtual Status addUnicastAddress(const UnicastAddressEntry& entry) = 0;
    virtual Status removeUnicastAddress(const UnicastAddressEntry& entry) = 0;

    virtual Status addRoute(const RouteEntry& entry) = 0;
    virtual Status removeRoute(const RouteEntry& entry) = 0;

    // Resolves the interface LUID of an adapter alias, used right after the
    // adapter is created because the Wintun allowlist has no LUID accessor.
    virtual Result<std::uint64_t> interfaceLuidForAlias(const std::wstring& alias) = 0;

    // True when the host is reachable over an interface other than `excludeLuid`.
    virtual Result<bool> isHostReachableOutsideInterface(const std::string& host,
                                                         std::uint64_t excludeLuid) = 0;
};

std::unique_ptr<NetConfig> createPlatformNetConfig();

}  // namespace phpvpn
