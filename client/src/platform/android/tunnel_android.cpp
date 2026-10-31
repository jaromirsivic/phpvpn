// AndroidTunnelDriver, see tunnel_android.h (plan-android-v001 WP03).
//
// The driver logic is pure C++ over the context, so the host unit tests run it
// with fake hosts (T-N02); the descriptor session needs Linux or Android and
// the platform factory the Android build.
#include "platform/android/tunnel_android.h"

#if defined(__linux__)
#include <fcntl.h>
#include <sys/eventfd.h>

#include "platform/posix/fd.h"
#include "platform/posix/fd_tunnel.h"
#endif

#include <mutex>
#include <string>
#include <utility>

#include "core/base/logger.h"
#include "core/base/utf8.h"
#include "platform/android/netconfig_android.h"

namespace phpvpn {
namespace {

constexpr const char* kComponent = "android-tunnel";

Error unsupported() {
    return Error(ErrorCode::tunnel_driver_unsupported, "the Android VPN context is not installed");
}

class AndroidTunnelDriver final : public TunnelDriver {
public:
    AndroidTunnelDriver(std::shared_ptr<AndroidVpnContext> context, AndroidTunnelFactory factory)
        : context_(std::move(context)), factory_(std::move(factory)) {}

    ~AndroidTunnelDriver() override { closeAdapter(); }

    // No library on Android; the installation directory is unused. The
    // consent comes first, it is the failure the user can act on (screen 37).
    Status loadAndVerify(const std::wstring& installDirectory) override {
        (void)installDirectory;
        if (!context_) return unsupported();
        std::lock_guard<std::mutex> lock(mutex_);
        verified_ = false;
        const auto network = context_->networkHost();
        if (!network) {
            return Error(ErrorCode::tunnel_driver_unsupported, "the network host is not registered");
        }
        bool granted = false;
        PHPVPN_TRY_ASSIGN(granted, network->isConsentGranted());
        if (!granted) {
            return Error(ErrorCode::tunnel_permission_missing, "the VPN consent is missing");
        }
        // Never triggered by the UI, Connect always passes the service
        // (plan, "Životní cyklus", step 1), but a defined error, not a crash.
        if (!context_->tunnelHost()) {
            return Error(ErrorCode::tunnel_adapter_failed, "the VPN service is not running");
        }
        verified_ = true;
        return Status::success();
    }

    // The interface name tunN exists only after establish(), so the adapter is
    // the synthetic identity of the context (point 3).
    Result<TunnelAdapterInfo> createAdapter(const std::wstring& name, const std::wstring& tunnelType,
                                            const std::wstring& requestedGuid) override {
        (void)tunnelType;
        if (!context_) return unsupported();
        std::lock_guard<std::mutex> lock(mutex_);
        if (!verified_) {
            return Error(ErrorCode::tunnel_adapter_failed, "the VPN service was not verified");
        }
        std::string adapterName;
        PHPVPN_TRY_ASSIGN(adapterName, toUtf8(name));
        TunnelAdapterInfo info;
        // The GUID as configured, like Linux, so the journal comparison of the
        // core stays the same; Android has no place to mark an interface.
        PHPVPN_TRY_ASSIGN(info.adapterGuid, toUtf8(requestedGuid));
        PHPVPN_TRY_ASSIGN(info.interfaceLuid, context_->createAdapter(std::move(adapterName)));
        info.created = true;
        ownsAdapter_ = true;
        return info;
    }

    Result<TunnelAdapterInfo> openAdapter(const std::wstring& name) override {
        (void)name;
        if (!context_) return unsupported();
        return Error(ErrorCode::not_supported, "opening an existing VPN interface is not supported");
    }

    Result<std::shared_ptr<Tunnel>> startSession(std::uint32_t ringCapacity) override {
        if (!context_) return unsupported();
        std::lock_guard<std::mutex> lock(mutex_);
        if (!ownsAdapter_ || context_->currentIdentity() == 0) {
            return Error(ErrorCode::tunnel_adapter_failed, "no virtual adapter is open");
        }
        if (session_.tunnel) {
            return Error(ErrorCode::state_conflict, "a session is already running");
        }
        if (!ringNoted_) {
            ringNoted_ = true;
            PHPVPN_LOG_DEBUG(kComponent, "the ring capacity " + std::to_string(ringCapacity) +
                                             " has no VpnService equivalent and is ignored");
        }

        int descriptor = -1;
        PHPVPN_TRY_ASSIGN(descriptor, context_->establish());

        if (auto verified = verifyUnderlyingNetwork(); !verified.ok()) {
            context_->closeAdapter();
            ownsAdapter_ = false;
            return verified.error();
        }
        if (!factory_) {
            context_->closeAdapter();
            ownsAdapter_ = false;
            return Error(ErrorCode::tunnel_session_failed, "no packet session is available");
        }
        auto made = factory_(descriptor);
        if (!made.ok()) {
            context_->closeAdapter();
            ownsAdapter_ = false;
            return made.error();
        }
        session_ = std::move(made).value();
        return session_.tunnel;
    }

    // Stops and detaches the session; the descriptor, and with it the VPN
    // network, stays until closeAdapter, the same order as Linux.
    void endSession() override {
        std::lock_guard<std::mutex> lock(mutex_);
        endSessionLocked();
    }

    void closeAdapter() override {
        std::lock_guard<std::mutex> lock(mutex_);
        endSessionLocked();
        if (context_ && ownsAdapter_) context_->closeAdapter();
        ownsAdapter_ = false;
    }

    std::uint32_t runningDriverVersion() const override { return 0; }

private:
    void endSessionLocked() {
        if (session_.detach) session_.detach();
        session_ = AndroidDescriptorSession{};
    }

    // Point 2: the core compared its snapshots before the interface existed,
    // so the comparison that matters runs right after establish().
    Status verifyUnderlyingNetwork() {
        const auto baseline = context_->baseline();
        if (!baseline.has_value()) {
            return Error(ErrorCode::netconfig_verify_failed, "no network baseline was taken before the adapter");
        }
        auto after = androidNetworkSnapshot(*context_);
        if (!after.ok()) {
            return Error(ErrorCode::netconfig_verify_failed, "the network state after establish could not be read");
        }
        if (!baseline->sameDefaultRouteAndDns(after.value())) {
            PHPVPN_LOG_WARN(kComponent, "the underlying default route or DNS changed while the VPN was established");
            return Error(ErrorCode::netconfig_verify_failed,
                         "the default route or the DNS configuration changed unexpectedly");
        }
        return Status::success();
    }

    const std::shared_ptr<AndroidVpnContext> context_;
    const AndroidTunnelFactory factory_;
    std::mutex mutex_;
    bool verified_ = false;
    bool ownsAdapter_ = false;
    bool ringNoted_ = false;
    AndroidDescriptorSession session_;
};

}  // namespace

#if defined(__linux__)
Result<AndroidDescriptorSession> makeFdTunnelSession(int descriptor) {
    const int statusFlags = ::fcntl(descriptor, F_GETFL);
    if (statusFlags < 0 || ::fcntl(descriptor, F_SETFL, statusFlags | O_NONBLOCK) != 0) {
        return Error(ErrorCode::tunnel_session_failed, "the VPN descriptor could not be made non-blocking");
    }
    const int descriptorFlags = ::fcntl(descriptor, F_GETFD);
    if (descriptorFlags < 0 || ::fcntl(descriptor, F_SETFD, descriptorFlags | FD_CLOEXEC) != 0) {
        return Error(ErrorCode::tunnel_session_failed, "the VPN descriptor could not be marked close-on-exec");
    }
    UniqueFd stopEvent(::eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK));
    if (!stopEvent) {
        return Error(ErrorCode::tunnel_session_failed, "the stop event could not be created");
    }
    auto tunnel = std::make_shared<FdTunnel>(descriptor, std::move(stopEvent));
    AndroidDescriptorSession session;
    session.tunnel = tunnel;
    session.detach = [tunnel] { tunnel->detach(); };
    return session;
}
#endif

std::unique_ptr<TunnelDriver> createAndroidTunnelDriver(std::shared_ptr<AndroidVpnContext> context,
                                                        AndroidTunnelFactory factory) {
    return std::make_unique<AndroidTunnelDriver>(std::move(context), std::move(factory));
}

#if defined(__ANDROID__)
// The context must be installed before the runtime creates its platform
// objects (WP04, nativeStart); without it every operation is unsupported.
std::unique_ptr<TunnelDriver> createPlatformTunnelDriver() {
    return createAndroidTunnelDriver(AndroidVpnContext::current(), makeFdTunnelSession);
}
#endif

}  // namespace phpvpn
