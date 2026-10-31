// AndroidVpnContext, see vpn_context.h (plan-android-v001 points 2, 3, 17, 18).
//
// Pure C++ without JNI, so the state machine and the VpnSpec checks run in the
// host unit tests (T-N02) with fake hosts on Linux and Windows as well.
#include "platform/android/vpn_context.h"

#if defined(_WIN32)
#include <io.h>
#else
#include <unistd.h>
#endif

#include <atomic>
#include <utility>

#include "core/base/logger.h"
#include "core/protocol/json.h"

namespace phpvpn {
namespace {

constexpr const char* kComponent = "android-vpn";

// The descriptor of establish(); the Windows branch only serves the host unit
// tests, which hand out ordinary file descriptors.
void closeDescriptor(int descriptor) noexcept {
#if defined(_WIN32)
    ::_close(descriptor);
#else
    ::close(descriptor);
#endif
}

// Process wide, so a context created after nativeStop and nativeStart never
// repeats an identity an older journal of this process may still name.
std::uint64_t nextIdentity() noexcept {
    static std::atomic<std::uint64_t> counter{0};
    std::uint64_t value = 0;
    do {
        value = counter.fetch_add(1) + 1;
    } while ((value & kAndroidIdentityCounterMask) == 0);
    return composeAndroidIdentity(value);
}

// The prefix of `address` when it is a shared VPN prefix (24 to 29 bits).
std::optional<Ipv4Cidr> prefixOf(Ipv4Address address, int prefixLength) {
    if (prefixLength < Ipv4Cidr::kMinPrefixLength || prefixLength > Ipv4Cidr::kMaxPrefixLength) {
        return std::nullopt;
    }
    const std::uint32_t mask = ~std::uint32_t{0} << (32 - prefixLength);
    return Ipv4Cidr::parse(Ipv4Address(address.value() & mask).toString() + "/" +
                           std::to_string(prefixLength));
}

std::mutex& installMutex() {
    static std::mutex mutex;
    return mutex;
}

std::shared_ptr<AndroidVpnContext>& installedContext() {
    static std::shared_ptr<AndroidVpnContext> context;
    return context;
}

}  // namespace

bool operator==(const VpnSpec& left, const VpnSpec& right) {
    return left.address == right.address && left.prefixLength == right.prefixLength &&
           left.route.network() == right.route.network() &&
           left.route.prefixLength() == right.route.prefixLength() && left.mtu == right.mtu &&
           left.sessionName == right.sessionName &&
           left.disallowedApplication == right.disallowedApplication &&
           left.allowIpv6 == right.allowIpv6 && left.metered == right.metered &&
           left.dnsServers == right.dnsServers;
}

std::string serializeVpnSpec(const VpnSpec& spec) {
    JsonArray disallowed;
    if (!spec.disallowedApplication.empty()) {
        disallowed.push_back(JsonValue::makeString(spec.disallowedApplication));
    }
    JsonArray dns;
    for (const auto& server : spec.dnsServers) dns.push_back(JsonValue::makeString(server));

    JsonObject members;
    members.emplace_back("version", JsonValue::makeInteger(1));
    members.emplace_back("session", JsonValue::makeString(spec.sessionName));
    members.emplace_back("address", JsonValue::makeString(spec.address.toString()));
    members.emplace_back("prefixLength", JsonValue::makeInteger(spec.prefixLength));
    members.emplace_back("route", JsonValue::makeString(spec.route.network().toString()));
    members.emplace_back("routePrefixLength", JsonValue::makeInteger(spec.route.prefixLength()));
    members.emplace_back("mtu", JsonValue::makeInteger(spec.mtu));
    members.emplace_back("disallowedApplications", JsonValue::makeArray(std::move(disallowed)));
    members.emplace_back("allowIpv6", JsonValue::makeBool(spec.allowIpv6));
    members.emplace_back("metered", JsonValue::makeBool(spec.metered));
    members.emplace_back("dnsServers", JsonValue::makeArray(std::move(dns)));
    return Json::serialize(JsonValue::makeObject(std::move(members)));
}

AndroidVpnContext::AndroidVpnContext(Options options) : options_(std::move(options)) {
    resetLocked();
}

AndroidVpnContext::~AndroidVpnContext() {
    // No wait here: the hosts may already be gone with the runtime.
    if (descriptor_ >= 0) closeDescriptor(descriptor_);
}

std::shared_ptr<AndroidVpnContext> AndroidVpnContext::current() {
    std::lock_guard<std::mutex> lock(installMutex());
    return installedContext();
}

void AndroidVpnContext::install(std::shared_ptr<AndroidVpnContext> context) {
    std::lock_guard<std::mutex> lock(installMutex());
    installedContext() = std::move(context);
}

void AndroidVpnContext::uninstall() {
    std::shared_ptr<AndroidVpnContext> released;
    {
        std::lock_guard<std::mutex> lock(installMutex());
        released = std::move(installedContext());
        installedContext().reset();
    }
    // The last reference may close a descriptor; never under the install lock.
    released.reset();
}

void AndroidVpnContext::setNetworkHost(std::shared_ptr<AndroidNetworkHost> host) {
    std::lock_guard<std::mutex> lock(hostsMutex_);
    networkHost_ = std::move(host);
}

void AndroidVpnContext::setTunnelHost(std::shared_ptr<AndroidTunnelHost> host) {
    std::lock_guard<std::mutex> lock(hostsMutex_);
    tunnelHost_ = std::move(host);
}

std::shared_ptr<AndroidNetworkHost> AndroidVpnContext::networkHost() const {
    std::lock_guard<std::mutex> lock(hostsMutex_);
    return networkHost_;
}

std::shared_ptr<AndroidTunnelHost> AndroidVpnContext::tunnelHost() const {
    std::lock_guard<std::mutex> lock(hostsMutex_);
    return tunnelHost_;
}

void AndroidVpnContext::noteSnapshot(const NetworkSnapshot& snapshot) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (identity_ == 0) candidateBaseline_ = snapshot;
}

std::optional<NetworkSnapshot> AndroidVpnContext::baseline() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return baseline_;
}

Result<std::uint64_t> AndroidVpnContext::createAdapter(std::string adapterName) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (identity_ != 0) {
        return Error(ErrorCode::state_conflict, "an adapter is already open");
    }
    auto candidate = std::move(candidateBaseline_);
    resetLocked();
    baseline_ = std::move(candidate);
    identity_ = nextIdentity();
    adapterName_ = std::move(adapterName);
    return identity_;
}

Status AndroidVpnContext::recordAddress(std::uint64_t identity, Ipv4Address address, int prefixLength) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (identity_ == 0 || identity != identity_) {
        return Error(ErrorCode::netconfig_apply_failed, "the address does not belong to the open tunnel adapter");
    }
    const auto prefix = prefixOf(address, prefixLength);
    if (!prefix.has_value() || !prefix->isValidClientAddress(address)) {
        return Error(ErrorCode::netconfig_apply_failed, "the tunnel address is not a client address of a VPN prefix");
    }
    if (specAddressSet_) {
        if (spec_.address == address && spec_.prefixLength == prefixLength) return Status::success();
        return Error(ErrorCode::netconfig_apply_failed, "a different tunnel address was already recorded");
    }
    if (descriptor_ >= 0) {
        return Error(ErrorCode::netconfig_apply_failed, "the VPN interface is already established");
    }
    spec_.address = address;
    spec_.prefixLength = prefixLength;
    specAddressSet_ = true;
    return Status::success();
}

Status AndroidVpnContext::recordRoute(std::uint64_t identity, const Ipv4Cidr& destination, Ipv4Address nextHop) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (identity_ == 0 || identity != identity_) {
        return Error(ErrorCode::netconfig_apply_failed, "the route does not belong to the open tunnel adapter");
    }
    if (!specAddressSet_) {
        return Error(ErrorCode::netconfig_apply_failed, "the tunnel route needs the tunnel address first");
    }
    if (nextHop.value() != 0) {
        return Error(ErrorCode::netconfig_apply_failed, "the tunnel route must be on-link");
    }
    const auto own = prefixOf(spec_.address, spec_.prefixLength);
    if (!own.has_value() || destination.network() != own->network() ||
        destination.prefixLength() != own->prefixLength()) {
        return Error(ErrorCode::netconfig_apply_failed, "only the VPN prefix of the tunnel address can be routed");
    }
    // The destination is fixed by the address, so a repeated route is equal.
    if (specRouteSet_) return Status::success();
    if (descriptor_ >= 0) {
        return Error(ErrorCode::netconfig_apply_failed, "the VPN interface is already established");
    }
    spec_.route = destination;
    specRouteSet_ = true;
    return Status::success();
}

Result<int> AndroidVpnContext::establish() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (identity_ == 0) {
        return Error(ErrorCode::tunnel_adapter_failed, "no virtual adapter is open");
    }
    if (descriptor_ >= 0) {
        return Error(ErrorCode::state_conflict, "the VPN interface is already established");
    }
    if (!specAddressSet_ || !specRouteSet_) {
        return Error(ErrorCode::tunnel_session_failed,
                     "the address and the route must be recorded before the interface is established");
    }
    const auto host = tunnelHost();
    if (!host) {
        return Error(ErrorCode::tunnel_adapter_failed, "the VPN service is not running");
    }
    int descriptor = -1;
    PHPVPN_TRY_ASSIGN(descriptor, host->establish(spec_));
    if (descriptor < 0) {
        return Error(ErrorCode::tunnel_adapter_failed, "the VPN service returned no interface");
    }
    descriptor_ = descriptor;
    PHPVPN_LOG_INFO(kComponent, "the VPN interface was established");
    return descriptor;
}

void AndroidVpnContext::closeAdapter() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (descriptor_ >= 0) {
        closeDescriptor(descriptor_);
        descriptor_ = -1;
        const auto host = tunnelHost();
        if (!host) {
            PHPVPN_LOG_WARN(kComponent, "the removal of the VPN network could not be verified, the VPN service is not running");
        } else {
            const auto gone = host->waitVpnNetworkGone(options_.networkGoneTimeoutMs);
            if (!gone.ok()) {
                PHPVPN_LOG_WARN(kComponent, "the removal of the VPN network could not be verified");
            } else if (!gone.value()) {
                PHPVPN_LOG_ERROR(kComponent, "the VPN network still exists after its descriptor was closed");
            } else {
                PHPVPN_LOG_INFO(kComponent, "the VPN network was removed");
            }
        }
    }
    resetLocked();
}

bool AndroidVpnContext::holdsInterface(std::uint64_t identity) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return identity != 0 && identity == identity_ && descriptor_ >= 0;
}

std::optional<std::uint64_t> AndroidVpnContext::identityForAlias(std::string_view alias) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (identity_ == 0 || alias != adapterName_) return std::nullopt;
    return identity_;
}

std::uint64_t AndroidVpnContext::currentIdentity() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return identity_;
}

std::optional<VpnSpec> AndroidVpnContext::preparedSpec() const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (identity_ == 0) return std::nullopt;
    return spec_;
}

void AndroidVpnContext::resetLocked() {
    identity_ = 0;
    adapterName_.clear();
    spec_ = VpnSpec{};
    spec_.mtu = options_.mtu;
    spec_.sessionName = options_.sessionName;
    spec_.disallowedApplication = options_.packageName;
    specAddressSet_ = false;
    specRouteSet_ = false;
    candidateBaseline_.reset();
    baseline_.reset();
}

}  // namespace phpvpn
