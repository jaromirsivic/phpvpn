// Process-wide owner of the Android VPN state (plan-android-v001 points 2, 3,
// 17 and 18).
//
// The core creates NetConfig and TunnelDriver through independent factories,
// but on Android both act on one VpnService.Builder: the address and the route
// must be known before establish() creates the interface, and no change is
// possible afterwards. NetConfig therefore only checks and records them in the
// VpnSpec held here, and the tunnel driver establishes the interface in
// startSession. This object holds, under one mutex, everything the two share:
// the prepared VpnSpec, the synthetic identity of the current adapter, the held
// descriptor, the baseline snapshot and the two hosts implemented in Kotlin.
//
// Lifetime (WP04): jni_entry creates the context in nativeStart, hands it the
// hosts registered so far (jni_vpn_bridge.h) and installs it before
// ServiceRuntime::start; nativeStop uninstalls it after ServiceRuntime::stop.
// The platform factories capture current() when they run, so the objects the
// runtime creates keep the context alive for as long as they exist.
//
// Thread rules (client.md "Běhový model"): the hosts are called only from the
// engine network thread (bootstrap, recovery, startup recovery) and from the
// teardown, never from the tunnel roles and never with a queue lock held. A
// host call may block in the JVM, so a host must never call back into this
// object. The state mutex is held across establish() and the bounded wait for
// the VPN network to vanish; the hosts have their own mutex, so registering or
// unregistering a host from a Kotlin thread never waits for those calls.
#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/base/result.h"
#include "core/protocol/ipv4.h"
#include "platform/iface/netconfig.h"

namespace phpvpn {

// Synthetic identity of the own interface (point 3): (0x414E << 48) | counter,
// 0x414E being "AN". The kernel assigns the tunN name only in establish(), so
// no OS identity exists when the core needs one. The identities of the
// underlying networks are bare ifindex values (upper 32 bits zero, point 4),
// so the two never collide.
inline constexpr std::uint64_t kAndroidIdentityTag = 0x414E;
inline constexpr std::uint64_t kAndroidIdentityCounterMask = (std::uint64_t{1} << 48) - 1;

constexpr std::uint64_t composeAndroidIdentity(std::uint64_t counter) noexcept {
    return (kAndroidIdentityTag << 48) | (counter & kAndroidIdentityCounterMask);
}
constexpr bool isAndroidIdentity(std::uint64_t identity) noexcept {
    return (identity >> 48) == kAndroidIdentityTag && (identity & kAndroidIdentityCounterMask) != 0;
}

// MTU of the VpnService interface: the Linux TUN value (client.md "TUN
// rozhraní a vlastnictví"); a different one would need a measurement.
inline constexpr std::uint32_t kAndroidTunnelMtu = 1500;
inline constexpr const char* kAndroidSessionName = "phpvpn";

// Everything VpnService.Builder needs, serialized to the TunnelHost as JSON
// (serializeVpnSpec). Only the address of the VPN prefix and the single route
// vpnCidr: no default route, no DNS, no IPv6 address (plan, "Chování v síti").
struct VpnSpec {
    Ipv4Address address;
    int prefixLength = 0;
    Ipv4Cidr route;
    std::uint32_t mtu = kAndroidTunnelMtu;
    std::string sessionName = kAndroidSessionName;
    // The own package, so the HTTP requests of the client never enter the
    // tunnel (point 5).
    std::string disallowedApplication;
    // Builder.allowFamily(AF_INET6): without it Android blocks the IPv6
    // traffic of every app the VPN covers, because the VPN adds no IPv6
    // address or route. IPv6 must keep working outside the tunnel.
    bool allowIpv6 = true;
    // Builder.setMetered(false): the VPN then takes the meteredness of the
    // underlying network instead of making every covered app see a metered
    // network (default for apps targeting API 29+).
    bool metered = false;
    // Builder.addDnsServer: empty by decision (no DNS through the tunnel).
    // Reserved for the documented T-A01b fallback with the DNS servers of the
    // active network, which lie outside vpnCidr.
    std::vector<std::string> dnsServers;
};

bool operator==(const VpnSpec& left, const VpnSpec& right);

// UTF-8 JSON for TunnelHost.establish(specJson), members in this order:
// {"version":1,"session":"phpvpn","address":"10.99.99.2","prefixLength":24,
//  "route":"10.99.99.0","routePrefixLength":24,"mtu":1500,
//  "disallowedApplications":["<package>"],"allowIpv6":true,"metered":false,
//  "dnsServers":[]}
std::string serializeVpnSpec(const VpnSpec& spec);

// Process-wide network host (point 18), implemented over the application
// context: lives for the whole process and works without a VpnService.
class AndroidNetworkHost {
public:
    virtual ~AndroidNetworkHost() = default;
    // UTF-8 JSON of every network, see netconfig_android.h for the schema.
    virtual Result<std::string> snapshotJson() = 0;
    // VpnService.prepare(applicationContext) == null.
    virtual Result<bool> isConsentGranted() = 0;
};

// Service host (point 18), implemented by the live VpnService instance, which
// registers it in onCreate and unregisters it in onDestroy.
class AndroidTunnelHost {
public:
    virtual ~AndroidTunnelHost() = default;
    // Builder.establish() for `spec` and ParcelFileDescriptor.detachFd(): the
    // returned descriptor belongs to the caller, Kotlin never closes it.
    // tunnel_permission_missing when the consent is gone, tunnel_adapter_failed
    // for every other refusal.
    virtual Result<int> establish(const VpnSpec& spec) = 0;
    // Waits at most `timeoutMs` until no VPN network of this app exists.
    virtual Result<bool> waitVpnNetworkGone(int timeoutMs) = 0;
};

class AndroidVpnContext {
public:
    struct Options {
        std::string packageName;
        std::string sessionName = kAndroidSessionName;
        std::uint32_t mtu = kAndroidTunnelMtu;
        // Bounded wait for the VPN network to vanish after the descriptor was
        // closed, the Linux value (closeAdapter).
        int networkGoneTimeoutMs = 2000;
    };

    explicit AndroidVpnContext(Options options);
    ~AndroidVpnContext();

    AndroidVpnContext(const AndroidVpnContext&) = delete;
    AndroidVpnContext& operator=(const AndroidVpnContext&) = delete;

    // The installed context or null. Never creates one.
    static std::shared_ptr<AndroidVpnContext> current();
    static void install(std::shared_ptr<AndroidVpnContext> context);
    static void uninstall();

    // Null unregisters. Never blocks on a running host call.
    void setNetworkHost(std::shared_ptr<AndroidNetworkHost> host);
    void setTunnelHost(std::shared_ptr<AndroidTunnelHost> host);
    std::shared_ptr<AndroidNetworkHost> networkHost() const;
    std::shared_ptr<AndroidTunnelHost> tunnelHost() const;

    const Options& options() const noexcept { return options_; }

    // Baseline for the check after establish (point 2): every snapshot taken
    // while no adapter is open replaces the candidate, createAdapter freezes
    // it. TunnelSession takes its baseline and the preflight snapshot right
    // before createAdapter, so the frozen baseline is the state the preflight
    // accepted.
    void noteSnapshot(const NetworkSnapshot& snapshot);
    std::optional<NetworkSnapshot> baseline() const;

    // Allocates the next synthetic identity and starts an empty VpnSpec.
    // state_conflict while a descriptor is held.
    Result<std::uint64_t> createAdapter(std::string adapterName);

    // Records the address and the route of the current identity. Only the
    // exact values the core applies are accepted: a client address of a
    // prefix of 24 to 29 bits, then the route of exactly that prefix with
    // next hop 0.0.0.0. Repeating the same value succeeds, anything else is
    // netconfig_apply_failed. The route metric has no Android counterpart.
    Status recordAddress(std::uint64_t identity, Ipv4Address address, int prefixLength);
    Status recordRoute(std::uint64_t identity, const Ipv4Cidr& destination, Ipv4Address nextHop);

    // Needs the current adapter with both entries recorded and a registered
    // TunnelHost; calls TunnelHost::establish and keeps the descriptor. The
    // returned descriptor stays owned by the context until closeAdapter.
    Result<int> establish();

    // Closes the held descriptor (Android then removes the VPN network), waits
    // at most networkGoneTimeoutMs for the network to vanish and forgets the
    // adapter. The caller must have stopped every user of the descriptor.
    void closeAdapter();

    // True only for the identity whose descriptor is held right now.
    bool holdsInterface(std::uint64_t identity) const;
    // Identity of the open adapter when `alias` is its name.
    std::optional<std::uint64_t> identityForAlias(std::string_view alias) const;
    // 0 when no adapter is open.
    std::uint64_t currentIdentity() const;
    // The VpnSpec of the open adapter, as far as recorded.
    std::optional<VpnSpec> preparedSpec() const;

private:
    void resetLocked();

    const Options options_;

    mutable std::mutex hostsMutex_;
    std::shared_ptr<AndroidNetworkHost> networkHost_;
    std::shared_ptr<AndroidTunnelHost> tunnelHost_;

    mutable std::mutex mutex_;
    std::uint64_t identity_ = 0;
    std::string adapterName_;
    VpnSpec spec_;
    bool specAddressSet_ = false;
    bool specRouteSet_ = false;
    int descriptor_ = -1;
    std::optional<NetworkSnapshot> candidateBaseline_;
    std::optional<NetworkSnapshot> baseline_;
};

}  // namespace phpvpn
