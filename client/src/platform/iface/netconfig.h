// Address and route configuration boundary.
//
// client.md "Vlastnictvi adapteru a session" requires an explicit destination
// prefix, next hop, interface and metric for every route. The implementation
// never looks for a best interface and never changes a global priority.
//
// Platform meaning of the fields (the names follow the Windows origin):
// - `interfaceLuid` is the platform interface identity. Windows: NET_LUID.
//   Linux: (uint64(ownerTag) << 32) | ifindex, where ownerTag is non-zero only
//   for an interface whose ifalias is the ownership mark "phpvpn-owner:<GUID>"
//   (platform/linux/interface_identity.h), so a recycled ifindex never matches.
// - an "alias" is the interface name (Windows alias, Linux ifname) and the
//   "GUID" is the installation GUID (Windows adapter GUID, Linux ownership mark).
// - route metrics are the Windows route metric or the Linux RTA_PRIORITY.
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

    // Read-only existence of this exact OS identity. False means positively
    // absent, lookup failure must remain an error. Never opens an adapter.
    virtual Result<bool> interfaceExists(std::uint64_t interfaceLuid) = 0;

    // Removes only the exact owned, non-present Wintun PnP instance, after
    // matching its GUID, alias and LUID against OS metadata. Live devices and
    // uncertain/mismatched identities fail closed. No Wintun API is invoked.
    // Success means absent or removal accepted without a restart requirement;
    // the caller must still verify disappearance of the LUID and owned rows.
    // The caller serializes this instance's lifecycle. Native SetupAPI removal
    // is synchronous, has no cancellation/deadline, and cannot atomically
    // condition removal on IsPresent remaining false against another admin.
    // Linux: removes (RTM_DELLINK) only a persistent, detached (carrier off),
    // root owned single queue TUN interface whose name, ownership mark and
    // identity all match; the client never creates persistent interfaces.
    virtual Status removeOwnedPhantomInterface(const std::wstring& alias,
                                               const std::wstring& expectedGuid,
                                               std::uint64_t interfaceLuid) = 0;

    virtual Status addUnicastAddress(const UnicastAddressEntry& entry) = 0;
    virtual Status removeUnicastAddress(const UnicastAddressEntry& entry) = 0;

    virtual Status addRoute(const RouteEntry& entry) = 0;
    virtual Status removeRoute(const RouteEntry& entry) = 0;

    // Resolves the interface LUID of an adapter alias, used right after the
    // adapter is created because the Wintun allowlist has no LUID accessor.
    // Linux: identity of the interface with that name (tag 0 when unmarked).
    virtual Result<std::uint64_t> interfaceLuidForAlias(const std::wstring& alias) = 0;

    // True when the host is reachable over an interface other than `excludeLuid`.
    virtual Result<bool> isHostReachableOutsideInterface(const std::string& host,
                                                         std::uint64_t excludeLuid) = 0;
};

std::unique_ptr<NetConfig> createPlatformNetConfig();

}  // namespace phpvpn
