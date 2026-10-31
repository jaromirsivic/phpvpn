// Linux TUN backend over /dev/net/tun (plan-v001 section 4.3).
//
// The interface is created non-persistent with IFF_TUN|IFF_NO_PI and the exact
// configured name. IFF_TUN_EXCL makes an existing interface of that name a
// collision instead of attaching to it. Because the device lives only as long as
// this process holds the descriptor, the kernel removes it together with its
// addresses and routes when the service dies, which is the core of the Linux
// recovery. Right after creation the interface gets the ownership marker as its
// ifalias, the MTU, no IPv6 address generation and IFF_UP.
//
// The packet I/O on the descriptor is the FdTunnel of platform/posix/fd_tunnel.h,
// shared with the Android VpnService descriptor; this file only creates,
// verifies and removes the TUN interface.

#include <fcntl.h>
#include <net/if.h>
#include <sys/eventfd.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <unistd.h>

#include <linux/if_link.h>
#include <linux/if_tun.h>

#include <cerrno>
#include <chrono>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>

#include "core/base/logger.h"
#include "core/base/utf8.h"
#include "platform/iface/tunnel.h"
#include "platform/linux/fd.h"
#include "platform/linux/interface_identity.h"
#include "platform/linux/netlink_socket.h"
#include "platform/posix/fd_tunnel.h"

namespace phpvpn {
namespace {

constexpr const char* kComponent = "tun";
constexpr const char* kTunDevice = "/dev/net/tun";
constexpr unsigned kTunMajor = 10;
constexpr unsigned kTunMinor = 200;
// Kernel default of a TUN device; the Wintun MTU could not be measured in WP00,
// so Linux keeps 1500 (decision recorded in the realization status).
constexpr std::uint32_t kTunnelMtu = 1500;
constexpr auto kDisappearanceWait = std::chrono::milliseconds(2000);
constexpr auto kDisappearancePoll = std::chrono::milliseconds(20);

// The name is passed to TUNSETIFF verbatim, so a '%' pattern or a separator
// must never reach the kernel.
bool validInterfaceName(const std::string& name) {
    if (name.empty() || name.size() >= IFNAMSIZ || name == "." || name == "..") return false;
    for (char character : name) {
        const bool allowed = (character >= 'a' && character <= 'z') ||
                             (character >= 'A' && character <= 'Z') ||
                             (character >= '0' && character <= '9') || character == '-' ||
                             character == '_' || character == '.';
        if (!allowed) return false;
    }
    return true;
}

std::string hex32(std::uint32_t value) {
    static constexpr char kDigits[] = "0123456789abcdef";
    std::string text(8, '0');
    for (int index = 7; index >= 0; --index, value >>= 4) text[index] = kDigits[value & 0xFu];
    return text;
}

class TunDriver final : public TunnelDriver {
public:
    ~TunDriver() override { closeAdapter(); }

    Status loadAndVerify(const std::wstring& installDirectory) override {
        // No library on Linux; the installation directory is unused.
        (void)installDirectory;
        std::lock_guard<std::mutex> lock(mutex_);
        struct stat status{};
        if (::stat(kTunDevice, &status) != 0) {
            return Error(ErrorCode::tunnel_library_missing, "the TUN device is missing");
        }
        if (!S_ISCHR(status.st_mode) || major(status.st_rdev) != kTunMajor ||
            minor(status.st_rdev) != kTunMinor) {
            return Error(ErrorCode::tunnel_library_invalid, "the TUN device node is not the kernel TUN device");
        }
        UniqueFd fd(::open(kTunDevice, O_RDWR | O_CLOEXEC));
        if (!fd) {
            return Error(ErrorCode::tunnel_library_invalid,
                         "the TUN device could not be opened (" + netlink::errnoText(errno) + ")");
        }
        unsigned int features = 0;
        if (::ioctl(fd.get(), TUNGETFEATURES, &features) != 0) {
            return Error(ErrorCode::tunnel_driver_unsupported, "the TUN features could not be queried");
        }
        if ((features & (IFF_TUN | IFF_NO_PI)) != (IFF_TUN | IFF_NO_PI)) {
            return Error(ErrorCode::tunnel_driver_unsupported, "the TUN device does not offer IFF_TUN|IFF_NO_PI");
        }
        PHPVPN_LOG_DEBUG(kComponent, "TUN features 0x" + hex32(features));
        verified_ = true;
        return Status::success();
    }

    Result<TunnelAdapterInfo> createAdapter(const std::wstring& name,
                                            const std::wstring& tunnelType,
                                            const std::wstring& requestedGuid) override {
        // A TUN device has no type string; the name is the whole identity.
        (void)tunnelType;
        std::lock_guard<std::mutex> lock(mutex_);
        if (!verified_) {
            return Error(ErrorCode::tunnel_library_missing, "the TUN device was not verified");
        }
        if (tunFd_) {
            return Error(ErrorCode::state_conflict, "an adapter is already open");
        }

        std::string interfaceName;
        PHPVPN_TRY_ASSIGN(interfaceName, toUtf8(name));
        if (!validInterfaceName(interfaceName)) {
            return Error(ErrorCode::invalid_argument, "the interface name is not a valid Linux name");
        }
        std::string guid;
        PHPVPN_TRY_ASSIGN(guid, canonicalInstallationGuid(requestedGuid));
        const std::string marker = ownershipMarker(guid);
        const std::uint32_t tag = ownerTagForGuid(guid);

        auto opened = netlink::Socket::open(ErrorCode::tunnel_adapter_failed);
        if (!opened.ok()) return opened.error();
        netlink::Socket socket = std::move(opened).value();

        UniqueFd fd(::open(kTunDevice, O_RDWR | O_CLOEXEC | O_NONBLOCK));
        if (!fd) {
            return Error(ErrorCode::tunnel_adapter_failed,
                         "the TUN device could not be opened (" + netlink::errnoText(errno) + ")");
        }
        ifreq request{};
        std::memcpy(request.ifr_name, interfaceName.c_str(), interfaceName.size() + 1);
        // No IFF_MULTI_QUEUE and never TUNSETPERSIST: the device must vanish
        // with this descriptor.
        request.ifr_flags = static_cast<short>(static_cast<std::uint16_t>(IFF_TUN | IFF_NO_PI | IFF_TUN_EXCL));
        if (::ioctl(fd.get(), TUNSETIFF, &request) != 0) {
            const int error = errno;
            if (error == EBUSY || error == EEXIST) {
                return Error(ErrorCode::netconfig_preflight_conflict,
                             "an interface with the tunnel name already exists");
            }
            return Error(ErrorCode::tunnel_adapter_failed,
                         "the TUN interface could not be created (" + netlink::errnoText(error) + ")");
        }
        ifreq actual{};
        if (::ioctl(fd.get(), TUNGETIFF, &actual) != 0 ||
            std::strncmp(actual.ifr_name, interfaceName.c_str(), IFNAMSIZ) != 0 ||
            (actual.ifr_flags & (IFF_PERSIST | IFF_MULTI_QUEUE)) != 0 ||
            (actual.ifr_flags & IFF_TUN) == 0) {
            // Never reached with IFF_TUN_EXCL; closing only detaches this
            // descriptor and leaves a foreign persistent device untouched.
            return Error(ErrorCode::netconfig_preflight_conflict,
                         "the tunnel name belongs to a foreign TUN interface");
        }

        std::optional<netlink::LinkRecord> link;
        PHPVPN_TRY_ASSIGN(link, socket.linkByName(interfaceName));
        if (!link.has_value() || link->kind != "tun" || link->tun.persist ||
            (link->alias.has_value() && !link->alias->empty())) {
            return Error(ErrorCode::tunnel_adapter_failed, "the new TUN interface has an unexpected state");
        }
        const int index = link->index;

        PHPVPN_TRY(configure(socket, index, marker));

        std::optional<netlink::LinkRecord> configured;
        PHPVPN_TRY_ASSIGN(configured, socket.linkByIndex(index));
        if (!configured.has_value() || configured->name != interfaceName ||
            configured->alias.value_or(std::string()) != marker ||
            (configured->flags & IFF_UP) == 0 || configured->mtu.value_or(0) != kTunnelMtu) {
            return Error(ErrorCode::tunnel_adapter_failed, "the TUN interface configuration did not apply");
        }

        tunFd_ = std::move(fd);
        ifindex_ = index;
        ownerTag_ = tag;

        TunnelAdapterInfo info;
        info.interfaceLuid = composeInterfaceIdentity(tag, index);
        // The GUID as configured, like the Windows driver, so the journal
        // comparison of the core stays the same.
        PHPVPN_TRY_ASSIGN(info.adapterGuid, toUtf8(requestedGuid));
        info.created = true;
        PHPVPN_LOG_INFO(kComponent, "the TUN interface is available");
        return info;
    }

    Result<TunnelAdapterInfo> openAdapter(const std::wstring& name) override {
        // The core never opens an existing interface on Linux: an interface of
        // that name that the service does not hold is a collision.
        (void)name;
        return Error(ErrorCode::not_supported, "opening an existing TUN interface is not supported");
    }

    Result<std::shared_ptr<Tunnel>> startSession(std::uint32_t ringCapacity) override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!tunFd_) {
            return Error(ErrorCode::tunnel_adapter_failed, "no virtual adapter is open");
        }
        if (tunnel_) {
            return Error(ErrorCode::state_conflict, "a session is already running");
        }
        if (!ringNoted_) {
            ringNoted_ = true;
            PHPVPN_LOG_DEBUG(kComponent, "the ring capacity " + std::to_string(ringCapacity) +
                                             " has no TUN equivalent and is ignored");
        }
        UniqueFd stopEvent(::eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK));
        if (!stopEvent) {
            return Error(ErrorCode::tunnel_session_failed, "the stop event could not be created");
        }
        tunnel_ = std::make_shared<FdTunnel>(tunFd_.get(), std::move(stopEvent));
        return std::static_pointer_cast<Tunnel>(tunnel_);
    }

    // Stops and detaches the session. The descriptor, and with it the
    // interface, stays until closeAdapter, so the core removes its route and
    // address from a live interface in the same order as on Windows.
    void endSession() override {
        std::lock_guard<std::mutex> lock(mutex_);
        endSessionLocked();
    }

    void closeAdapter() override {
        std::lock_guard<std::mutex> lock(mutex_);
        endSessionLocked();
        if (!tunFd_) return;
        const int index = ifindex_;
        const std::uint32_t tag = ownerTag_;
        tunFd_.reset();
        ifindex_ = 0;
        ownerTag_ = 0;
        waitForDisappearance(index, tag);
    }

    std::uint32_t runningDriverVersion() const override { return 0; }

private:
    void endSessionLocked() {
        if (tunnel_) {
            tunnel_->detach();
            tunnel_.reset();
        }
    }

    static Status configure(netlink::Socket& socket, int index, const std::string& marker) {
        netlink::Reply reply;
        netlink::LinkChange identity;
        identity.index = index;
        identity.alias = marker;
        identity.mtu = kTunnelMtu;
        PHPVPN_TRY_ASSIGN(reply, socket.request([&](std::uint32_t sequence) {
            return netlink::encodeLinkChange(sequence, identity);
        }));
        if (reply.error != 0) {
            return Error(ErrorCode::tunnel_adapter_failed,
                         "the ownership mark could not be set (" + netlink::errnoText(reply.error) + ")");
        }

        // Before IFF_UP, so the kernel never generates a link-local address.
        netlink::LinkChange inet6;
        inet6.index = index;
        inet6.inet6AddrGenMode = IN6_ADDR_GEN_MODE_NONE;
        PHPVPN_TRY_ASSIGN(reply, socket.request([&](std::uint32_t sequence) {
            return netlink::encodeLinkChange(sequence, inet6);
        }));
        if (reply.error == EAFNOSUPPORT || reply.error == EOPNOTSUPP) {
            PHPVPN_LOG_DEBUG(kComponent, "IPv6 is unavailable, no address generation to disable");
        } else if (reply.error != 0) {
            return Error(ErrorCode::tunnel_adapter_failed,
                         "IPv6 address generation could not be disabled (" +
                             netlink::errnoText(reply.error) + ")");
        }

        netlink::LinkChange up;
        up.index = index;
        up.up = true;
        PHPVPN_TRY_ASSIGN(reply, socket.request([&](std::uint32_t sequence) {
            return netlink::encodeLinkChange(sequence, up);
        }));
        if (reply.error != 0) {
            return Error(ErrorCode::tunnel_adapter_failed,
                         "the TUN interface could not be brought up (" + netlink::errnoText(reply.error) + ")");
        }
        return Status::success();
    }

    static void waitForDisappearance(int index, std::uint32_t tag) {
        auto socket = netlink::Socket::open(ErrorCode::tunnel_adapter_failed);
        if (!socket.ok()) {
            PHPVPN_LOG_WARN(kComponent, "the removal of the TUN interface could not be verified");
            return;
        }
        const auto deadline = std::chrono::steady_clock::now() + kDisappearanceWait;
        for (;;) {
            auto link = socket->linkByIndex(index);
            if (!link.ok()) {
                PHPVPN_LOG_WARN(kComponent, "the removal of the TUN interface could not be verified");
                return;
            }
            // Gone, or the index already belongs to an interface without our mark.
            if (!link->has_value() || ownerTagForAlias((*link)->alias.value_or(std::string())) != tag) {
                PHPVPN_LOG_INFO(kComponent, "the TUN interface was removed");
                return;
            }
            if (std::chrono::steady_clock::now() >= deadline) {
                PHPVPN_LOG_ERROR(kComponent, "the TUN interface still exists after its descriptor was closed");
                return;
            }
            std::this_thread::sleep_for(kDisappearancePoll);
        }
    }

    std::mutex mutex_;
    bool verified_ = false;
    bool ringNoted_ = false;
    UniqueFd tunFd_;
    int ifindex_ = 0;
    std::uint32_t ownerTag_ = 0;
    std::shared_ptr<FdTunnel> tunnel_;
};

}  // namespace

std::unique_ptr<TunnelDriver> createPlatformTunnelDriver() {
    return std::make_unique<TunDriver>();
}

}  // namespace phpvpn
