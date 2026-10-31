// Network configuration of Android over AndroidVpnContext (plan-android-v001
// points 2, 4 and 17).
//
// Apps targeting API 30+ may not dump routes or links over netlink, so the
// snapshot comes from ConnectivityManager through the NetworkHost (JNI). The
// address and the route are not applied to an interface here: VpnService only
// takes them before establish(), so addUnicastAddress and addRoute check and
// record them in the VpnSpec of the context and the tunnel driver establishes
// the interface in startSession. Removing them is success without a system
// call, closing the descriptor removes everything (documented deviation, like
// the route metric on macOS).
//
// Snapshot JSON (NetworkHost.snapshotNetworks, UTF-8, at most 1 MiB), version 1.
// Every member is required, unknown members are an error:
//
//   {"version": 1,
//    "activeNetwork": <handle of ConnectivityManager.getActiveNetwork()> | null,
//    "networks": [
//      {"handle": <Network.getNetworkHandle()>,
//       "interfaceName": "<LinkProperties.getInterfaceName()>" | null,
//       "vpn": <NetworkCapabilities.hasTransport(TRANSPORT_VPN)>,
//       "transports": ["wifi" | "cellular" | "ethernet" | "vpn" | "bluetooth" | "other", ...],
//       "addresses": [{"interfaceName": "<link>", "ifindex": <NetworkInterface index or 0>,
//                      "address": "<IPv4 or IPv6 text>", "prefixLength": <0..128>}, ...],
//       "routes": [{"interfaceName": "<RouteInfo.getInterface()>" | null, "ifindex": <index or 0>,
//                   "destination": "<IpPrefix address>", "prefixLength": <0..128>,
//                   "gateway": "<address>" | null, "type": "unicast" | "unreachable" | "throw" | "other",
//                   "isDefault": <RouteInfo.isDefaultRoute()>}, ...],
//       "dnsServers": ["<address>", ...]}, ...]}
//
// Addresses and routes include the stacked links (464XLAT clat); each row
// names its own interface. IPv6 rows are validated and then ignored, because
// the tunnel and the snapshot of the core are IPv4 only.
//
// Snapshot semantics (point 4):
// - every network with "vpn": true is left out. Our own VPN is not looked up
//   by name: a foreign VPN is replaced by our establish() anyway and its
//   routes must not fail the preflight;
// - the identity of an underlying row is its ifindex with zero upper bits; an
//   unknown ifindex (0) becomes 0xFFFFFFFF, never 0, because the preflight
//   skips identity 0 as the own interface;
// - only "unicast" IPv4 routes; destinations the shared Ipv4Cidr type cannot
//   express (other than /24 to /29) keep the default prefix, like Linux;
// - the default route is the first IPv4 "isDefault" route of the active
//   network; defaultRouteMetric is always 0 (Android exposes no metric);
// - dnsServers are those of the active network in system order, IPv4 and IPv6
//   as reported; a VPN or missing active network has no default route and no
//   DNS servers.
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/base/result.h"
#include "core/protocol/ipv4.h"
#include "platform/android/vpn_context.h"
#include "platform/iface/netconfig.h"

namespace phpvpn {

inline constexpr std::size_t kMaxAndroidSnapshotBytes = 1024 * 1024;
// Identity of an underlying row whose ifindex the host could not resolve.
inline constexpr std::uint64_t kUnknownAndroidInterfaceIdentity = 0xFFFFFFFFu;

struct AndroidAddressRow {
    std::uint64_t identity = 0;
    Ipv4Address address;
    int prefixLength = 0;
};

struct AndroidRouteRow {
    std::uint64_t identity = 0;
    Ipv4Address destination;
    int prefixLength = 0;
    Ipv4Address gateway;  // 0.0.0.0 when on-link
    bool unicast = false;
    bool isDefault = false;
};

struct AndroidNetworkRow {
    std::int64_t handle = 0;
    bool vpn = false;
    // IPv4 rows only.
    std::vector<AndroidAddressRow> addresses;
    std::vector<AndroidRouteRow> routes;
    std::vector<std::string> dnsServers;
};

struct AndroidNetworkState {
    std::optional<std::int64_t> activeNetwork;
    std::vector<AndroidNetworkRow> networks;

    // The active network unless it is missing or a VPN.
    const AndroidNetworkRow* activeUnderlying() const;
};

std::uint64_t androidUnderlyingIdentity(std::int64_t ifindex) noexcept;

// Strict parser of the schema above; netconfig_apply_failed for malformed input.
Result<AndroidNetworkState> parseAndroidNetworkState(std::string_view json);
NetworkSnapshot toNetworkSnapshot(const AndroidNetworkState& state);

// The parsed state through the NetworkHost of `context`.
Result<AndroidNetworkState> queryAndroidNetworkState(AndroidVpnContext& context);
// The snapshot of the core; also noted as the baseline candidate of the context.
Result<NetworkSnapshot> androidNetworkSnapshot(AndroidVpnContext& context);

// IPv4 addresses of `host` (numeric or resolved). Injected so the host unit
// tests do not depend on a resolver.
using AndroidHostResolver = std::function<std::vector<Ipv4Address>(const std::string& host)>;

#if !defined(_WIN32)
// getaddrinfo with AF_INET, like the Linux netconfig.
std::vector<Ipv4Address> resolveIpv4Addresses(const std::string& host);
#endif

// Over `context`; a null context gives a NetConfig whose every operation fails
// with tunnel_driver_unsupported (point 17).
std::unique_ptr<NetConfig> createAndroidNetConfig(std::shared_ptr<AndroidVpnContext> context,
                                                  AndroidHostResolver resolver);

}  // namespace phpvpn
