// macOS tunnel backend over the utun kernel control (plan-macos-v001,
// limitation 1 and step 4), the counterpart of platform/linux/tunnel_tun.cpp.
//
// A utun interface is created by connecting a PF_SYSTEM/SYSPROTO_CONTROL
// datagram socket to the kernel control "com.apple.net.utun_control". The
// kernel names the interface itself (utunN); unit 0 lets it pick the first free
// unit, so the configured adapter name ("phpvpn") is only the logical name used
// in the journal and the log, and the interface is never renamed. The interface
// lives only as long as this process holds the socket: closing it, or the death
// of the service, makes the kernel detach the interface together with its
// addresses and routes, which is the core of the recovery exactly as on Linux.
// There is no persistent utun.
//
// Right after creation the interface gets the ownership marker as its
// description (platform/macos/interface_identity.h), the MTU and IFF_UP; the
// result is read back and verified. No IPv6 configuration is attempted: the
// kernel creates utun with IFEF_NOAUTOIPV6LL, so a fresh utun has no address
// at all, not even an IPv6 link-local one.
//
// Every datagram on the socket carries a 4 byte protocol family in network
// byte order in front of the IP packet. receive reads the header and the
// packet into the caller buffer with one recvmsg; a packet longer than the
// buffer is reported by MSG_TRUNC (kernel control datagram sockets are
// PR_ATOMIC, so the rest of the record is dropped) and counted instead of
// delivered. Packets of another family than AF_INET are dropped. send writes
// the header and one caller packet with one writev. Both wait with poll on the
// socket together with a stop pipe, so signalStop always wakes a worker.

#include <sys/types.h>

#include <arpa/inet.h>
#include <fcntl.h>
#include <net/if.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/kern_control.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/sys_domain.h>
#include <sys/uio.h>
#include <unistd.h>

#if __has_include(<net/if_utun.h>)
#include <net/if_utun.h>
#endif
// Public ABI values of <net/if_utun.h>, for an SDK that does not ship it.
#ifndef UTUN_CONTROL_NAME
#define UTUN_CONTROL_NAME "com.apple.net.utun_control"
#endif
#ifndef UTUN_OPT_IFNAME
#define UTUN_OPT_IFNAME 2
#endif

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <climits>
#include <cstring>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <thread>
#include <utility>

#include "core/base/logger.h"
#include "core/base/utf8.h"
#include "platform/iface/tunnel.h"
#include "platform/macos/interface_identity.h"

namespace phpvpn {
namespace {

constexpr const char* kComponent = "utun";
// The kernel default of utun and the Linux TUN value (see tunnel_tun.cpp).
constexpr std::uint32_t kTunnelMtu = 1500;
// Same bounded write wait as Wintun and Linux: 200 rounds of at most 5 ms.
constexpr int kSendAttempts = 200;
constexpr int kSendWaitMs = 5;
constexpr auto kDisappearanceWait = std::chrono::milliseconds(2000);
constexpr auto kDisappearancePoll = std::chrono::milliseconds(20);
// The protocol family in front of every packet on the utun socket.
constexpr std::size_t kFamilyHeaderBytes = sizeof(std::uint32_t);
// Packets of a foreign family dropped per read round before the worker gets
// control back, so a flood cannot hold it.
constexpr int kMaxDroppedPerRound = 64;
// Units tried one by one when the kernel refuses unit 0 (utun0 .. utun254).
constexpr std::uint32_t kLastFallbackUnit = 255;

static_assert(sizeof(UTUN_CONTROL_NAME) <= sizeof(ctl_info::ctl_name));

// Owning descriptor, local so this file depends on no other platform file.
class OwnedFd {
public:
    OwnedFd() noexcept = default;
    explicit OwnedFd(int fd) noexcept : fd_(fd) {}
    OwnedFd(OwnedFd&& other) noexcept : fd_(std::exchange(other.fd_, -1)) {}
    OwnedFd& operator=(OwnedFd&& other) noexcept {
        if (this != &other) {
            reset();
            fd_ = std::exchange(other.fd_, -1);
        }
        return *this;
    }
    OwnedFd(const OwnedFd&) = delete;
    OwnedFd& operator=(const OwnedFd&) = delete;
    ~OwnedFd() { reset(); }

    int get() const noexcept { return fd_; }
    bool valid() const noexcept { return fd_ >= 0; }
    void reset() noexcept {
        if (fd_ >= 0) ::close(fd_);
        fd_ = -1;
    }

private:
    int fd_ = -1;
};

// Only the errno number, never the localized system text.
std::string errnoText(int error) { return "errno " + std::to_string(error); }

bool rootRequired(int error) noexcept { return error == EPERM || error == EACCES; }

// macOS has no SOCK_CLOEXEC, pipe2 or accept4: the flags are set right after
// the descriptor exists.
bool setCloseOnExec(int fd) noexcept {
    const int flags = ::fcntl(fd, F_GETFD);
    return flags >= 0 && ::fcntl(fd, F_SETFD, flags | FD_CLOEXEC) == 0;
}

bool setNonBlocking(int fd) noexcept {
    const int flags = ::fcntl(fd, F_GETFL);
    return flags >= 0 && ::fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

Error adapterFailed(const std::string& what, const Error& cause) {
    return Error(ErrorCode::tunnel_adapter_failed,
                 cause.detail.empty() ? what : what + ": " + cause.detail);
}

Error adapterFailed(const std::string& what, int error) {
    return Error(ErrorCode::tunnel_adapter_failed, what + " (" + errnoText(error) + ")");
}

// Looks up the id of the utun kernel control on a PF_SYSTEM socket.
bool utunControlId(int fd, std::uint32_t& id) noexcept {
    ctl_info info{};
    std::memcpy(info.ctl_name, UTUN_CONTROL_NAME, sizeof(UTUN_CONTROL_NAME));
    if (::ioctl(fd, CTLIOCGINFO, &info) != 0) return false;
    id = info.ctl_id;
    return true;
}

// Opens a close-on-exec kernel control socket and connects it to utun `unit`
// (0: the kernel picks the first free unit). The connect creates the
// interface. An invalid descriptor comes back with the errno in `error`.
OwnedFd connectUnit(std::uint32_t unit, int& error) {
    error = 0;
    OwnedFd fd(::socket(PF_SYSTEM, SOCK_DGRAM, SYSPROTO_CONTROL));
    if (!fd.valid() || !setCloseOnExec(fd.get())) {
        error = errno;
        return OwnedFd();
    }
    std::uint32_t id = 0;
    if (!utunControlId(fd.get(), id)) {
        error = errno;
        return OwnedFd();
    }
    sockaddr_ctl address{};
    // The kernel accepts only sc_len == sizeof(struct sockaddr_ctl).
    address.sc_len = static_cast<std::uint8_t>(sizeof(address));
    address.sc_family = static_cast<std::uint8_t>(AF_SYSTEM);
    address.ss_sysaddr = static_cast<std::uint16_t>(AF_SYS_CONTROL);
    address.sc_id = id;
    address.sc_unit = unit;
    if (::connect(fd.get(), reinterpret_cast<const sockaddr*>(&address),
                  static_cast<socklen_t>(sizeof(address))) != 0) {
        error = errno;
        return OwnedFd();
    }
    return fd;
}

// Creates a new utun interface. Current kernels allocate a free unit for
// unit 0 (the utun control registers a setup callback); only when that is
// refused are the units 1..255 tried, skipping the ones in use.
Result<OwnedFd> createUtun() {
    int error = 0;
    OwnedFd fd = connectUnit(0, error);
    if (fd.valid()) return Result<OwnedFd>(std::move(fd));
    if (rootRequired(error)) {
        return adapterFailed("creating a utun interface requires root privileges", error);
    }
    PHPVPN_LOG_DEBUG(kComponent, "the kernel refused utun unit 0 (" + errnoText(error) +
                                     "), trying explicit units");
    for (std::uint32_t unit = 1; unit <= kLastFallbackUnit; ++unit) {
        fd = connectUnit(unit, error);
        if (fd.valid()) return Result<OwnedFd>(std::move(fd));
        if (rootRequired(error)) {
            return adapterFailed("creating a utun interface requires root privileges", error);
        }
        if (error != EBUSY && error != EADDRINUSE) break;
    }
    return adapterFailed("no utun interface could be created", error);
}

// The name the kernel gave the interface, "utun" followed by the unit digits.
Result<std::string> utunInterfaceName(int fd) {
    char buffer[IFNAMSIZ + 1] = {};
    socklen_t length = IFNAMSIZ;
    if (::getsockopt(fd, SYSPROTO_CONTROL, UTUN_OPT_IFNAME, buffer, &length) != 0) {
        return adapterFailed("the utun interface name could not be read", errno);
    }
    const std::string name(buffer, ::strnlen(buffer, IFNAMSIZ));
    bool valid = name.size() > 4 && name.size() < IFNAMSIZ && name.compare(0, 4, "utun") == 0;
    for (std::size_t index = 4; valid && index < name.size(); ++index) {
        valid = name[index] >= '0' && name[index] <= '9';
    }
    if (!valid) {
        return Error(ErrorCode::tunnel_adapter_failed, "the kernel returned an unexpected utun interface name");
    }
    return name;
}

// The socket the interface ioctls (MTU, flags) are issued on.
Result<OwnedFd> openInetSocket() {
    OwnedFd fd(::socket(AF_INET, SOCK_DGRAM, 0));
    if (!fd.valid() || !setCloseOnExec(fd.get())) {
        return adapterFailed("the interface control socket could not be opened", errno);
    }
    return Result<OwnedFd>(std::move(fd));
}

// One ifreq ioctl on `name`; `name` is a validated utun name (< IFNAMSIZ).
bool interfaceRequest(int socket, unsigned long request, const std::string& name, ifreq& data) noexcept {
    std::memcpy(data.ifr_name, name.data(), name.size());
    data.ifr_name[name.size()] = 0;
    return ::ioctl(socket, request, &data) == 0;
}

// Marker, MTU and IFF_UP, in that order, so the interface is never up without
// its ownership mark.
Status configureInterface(const std::string& name, const std::string& marker) {
    auto described = writeInterfaceDescription(name, marker);
    if (!described.ok()) return adapterFailed("the ownership mark could not be set", described.error());

    OwnedFd socket;
    PHPVPN_TRY_ASSIGN(socket, openInetSocket());

    ifreq mtu{};
    mtu.ifr_mtu = static_cast<int>(kTunnelMtu);
    if (!interfaceRequest(socket.get(), SIOCSIFMTU, name, mtu)) {
        return adapterFailed("the utun MTU could not be set", errno);
    }

    ifreq flags{};
    if (!interfaceRequest(socket.get(), SIOCGIFFLAGS, name, flags)) {
        return adapterFailed("the utun flags could not be read", errno);
    }
    flags.ifr_flags = static_cast<short>(flags.ifr_flags | IFF_UP);
    if (!interfaceRequest(socket.get(), SIOCSIFFLAGS, name, flags)) {
        return adapterFailed("the utun interface could not be brought up", errno);
    }
    return Status::success();
}

// Reads back the description, the MTU and IFF_UP.
Status verifyInterface(const std::string& name, const std::string& marker) {
    auto description = readInterfaceDescription(name);
    if (!description.ok()) {
        return adapterFailed("the ownership mark could not be read back", description.error());
    }
    if (description.value() != marker) {
        return Error(ErrorCode::tunnel_adapter_failed, "the utun interface does not carry the ownership mark");
    }

    OwnedFd socket;
    PHPVPN_TRY_ASSIGN(socket, openInetSocket());

    ifreq mtu{};
    if (!interfaceRequest(socket.get(), SIOCGIFMTU, name, mtu)) {
        return adapterFailed("the utun MTU could not be read", errno);
    }
    if (mtu.ifr_mtu != static_cast<int>(kTunnelMtu)) {
        return Error(ErrorCode::tunnel_adapter_failed, "the utun MTU did not apply");
    }

    ifreq flags{};
    if (!interfaceRequest(socket.get(), SIOCGIFFLAGS, name, flags)) {
        return adapterFailed("the utun flags could not be read", errno);
    }
    if ((flags.ifr_flags & IFF_UP) == 0) {
        return Error(ErrorCode::tunnel_adapter_failed, "the utun interface is not up");
    }
    return Status::success();
}

class UtunTunnel final : public Tunnel {
public:
    UtunTunnel(int fd, OwnedFd stopRead, OwnedFd stopWrite)
        : fd_(fd), stopRead_(std::move(stopRead)), stopWrite_(std::move(stopWrite)) {}

    Result<TunnelReadResult> receive(std::span<std::uint8_t> buffer,
                                     std::int64_t timeoutMs) override {
        std::shared_lock<std::shared_mutex> lock(ioMutex_);
        for (int round = 0; round < 2; ++round) {
            for (int dropped = 0; dropped < kMaxDroppedPerRound; ++dropped) {
                if (stopped_.load() || fd_ < 0) {
                    return TunnelReadResult{TunnelReadStatus::stopped, 0};
                }

                // Header and packet in one datagram read. A packet longer than
                // the caller buffer is truncated by the kernel, flagged with
                // MSG_TRUNC and counted instead of delivered.
                std::uint32_t family = 0;
                iovec vectors[2] = {{&family, sizeof(family)}, {buffer.data(), buffer.size()}};
                msghdr message{};
                message.msg_iov = vectors;
                message.msg_iovlen = 2;
                const ssize_t count = ::recvmsg(fd_, &message, 0);
                if (count >= 0) {
                    if ((message.msg_flags & MSG_TRUNC) != 0) {
                        oversized_.fetch_add(1);
                        return TunnelReadResult{TunnelReadStatus::empty, 0};
                    }
                    const auto size = static_cast<std::size_t>(count);
                    if (size <= kFamilyHeaderBytes) return TunnelReadResult{TunnelReadStatus::empty, 0};
                    if (ntohl(family) != static_cast<std::uint32_t>(AF_INET)) {
                        // IPv6 or anything else never reaches the core.
                        foreignFamily_.fetch_add(1);
                        continue;
                    }
                    return TunnelReadResult{TunnelReadStatus::packet, size - kFamilyHeaderBytes};
                }
                if (errno == EINTR) return TunnelReadResult{TunnelReadStatus::empty, 0};
                if (errno != EAGAIN && errno != EWOULDBLOCK) {
                    return Error(ErrorCode::tunnel_io_failed, "the virtual interface could not be read");
                }
                break;
            }
            if (round == 1) break;

            pollfd waits[2] = {{fd_, POLLIN, 0}, {stopRead_.get(), POLLIN, 0}};
            const int timeout = timeoutMs < 0 ? -1
                                              : static_cast<int>(std::min<std::int64_t>(timeoutMs, INT_MAX));
            const int ready = ::poll(waits, 2, timeout);
            if (ready < 0) {
                if (errno == EINTR) break;
                return Error(ErrorCode::tunnel_io_failed, "the read wait failed");
            }
            if (ready == 0) break;
            if (waits[1].revents != 0) return TunnelReadResult{TunnelReadStatus::stopped, 0};
            if ((waits[0].revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
                return Error(ErrorCode::tunnel_io_failed, "the virtual interface reported an error");
            }
        }
        return TunnelReadResult{TunnelReadStatus::empty, 0};
    }

    Status send(std::span<const std::uint8_t> packet) override {
        if (packet.empty() || packet.size() > kMaxTunnelPacketBytes) {
            return Error(ErrorCode::wire_invalid_packet, "the packet length is out of range");
        }
        std::shared_lock<std::shared_mutex> lock(ioMutex_);
        std::uint32_t family = htonl(static_cast<std::uint32_t>(AF_INET));
        const std::size_t expected = kFamilyHeaderBytes + packet.size();
        // A full queue produces a bounded wait rather than a busy loop, and the
        // stop pipe always wins so the worker can be joined.
        for (int attempt = 0; attempt < kSendAttempts; ++attempt) {
            if (stopped_.load() || fd_ < 0) {
                return Error(ErrorCode::cancelled, "the tunnel is stopping");
            }
            iovec vectors[2] = {{&family, sizeof(family)},
                                {const_cast<std::uint8_t*>(packet.data()), packet.size()}};
            const ssize_t written = ::writev(fd_, vectors, 2);
            if (written >= 0 && static_cast<std::size_t>(written) == expected) return Status::success();
            if (written >= 0) {
                return Error(ErrorCode::tunnel_io_failed, "the virtual interface accepted a partial packet");
            }
            const int error = errno;
            if (error == EINTR) continue;
            if (error != EAGAIN && error != EWOULDBLOCK && error != ENOBUFS) {
                return Error(ErrorCode::tunnel_io_failed, "the virtual interface could not be written");
            }
            // EAGAIN: the socket buffer is full and POLLOUT reports space.
            // ENOBUFS: the kernel could not queue the packet, which no
            // descriptor reports, so only the time bound and the stop pipe end
            // that wait.
            pollfd waits[2] = {{stopRead_.get(), POLLIN, 0}, {fd_, POLLOUT, 0}};
            const nfds_t watched = error == ENOBUFS ? 1 : 2;
            const int ready = ::poll(waits, watched, kSendWaitMs);
            if (ready > 0 && waits[0].revents != 0) {
                return Error(ErrorCode::cancelled, "the tunnel is stopping");
            }
            if (ready < 0 && errno != EINTR) {
                return Error(ErrorCode::tunnel_io_failed, "the write wait failed");
            }
        }
        return Error(ErrorCode::busy, "the virtual interface queue stayed full");
    }

    void signalStop() override {
        stopped_.store(true);
        // The pipe is never drained, so it stays readable and every later wait
        // returns immediately as well. A full pipe is already readable.
        const char one = 1;
        const ssize_t written = ::write(stopWrite_.get(), &one, 1);
        (void)written;
    }

    std::uint64_t oversizedReceiveCount() const override { return oversized_.load(); }

    std::uint64_t foreignFamilyCount() const noexcept { return foreignFamily_.load(); }

    // Called by the driver: waits until no receive or send uses the socket any
    // more, so the driver can close it safely even while a worker still holds
    // this object.
    void detach() {
        signalStop();
        std::unique_lock<std::shared_mutex> lock(ioMutex_);
        fd_ = -1;
    }

private:
    std::shared_mutex ioMutex_;
    int fd_ = -1;  // owned by the driver
    OwnedFd stopRead_;
    OwnedFd stopWrite_;
    std::atomic<bool> stopped_{false};
    std::atomic<std::uint64_t> oversized_{0};
    std::atomic<std::uint64_t> foreignFamily_{0};
};

class UtunDriver final : public TunnelDriver {
public:
    ~UtunDriver() override { closeAdapter(); }

    Status loadAndVerify(const std::wstring& installDirectory) override {
        // No library on macOS; the installation directory is unused. Nothing
        // is connected, so no interface is created here.
        (void)installDirectory;
        std::lock_guard<std::mutex> lock(mutex_);
        OwnedFd control(::socket(PF_SYSTEM, SOCK_DGRAM, SYSPROTO_CONTROL));
        if (!control.valid()) {
            const int error = errno;
            if (rootRequired(error)) {
                return Error(ErrorCode::tunnel_library_invalid,
                             "the kernel control socket requires root privileges (" + errnoText(error) + ")");
            }
            return Error(ErrorCode::tunnel_library_missing,
                         "the kernel control socket is unavailable (" + errnoText(error) + ")");
        }
        std::uint32_t id = 0;
        if (!utunControlId(control.get(), id)) {
            const int error = errno;
            if (rootRequired(error)) {
                return Error(ErrorCode::tunnel_library_invalid,
                             "the utun kernel control requires root privileges (" + errnoText(error) + ")");
            }
            if (error == ENOENT) {
                return Error(ErrorCode::tunnel_library_missing, "the utun kernel control is not registered");
            }
            return Error(ErrorCode::tunnel_library_invalid,
                         "the utun kernel control could not be queried (" + errnoText(error) + ")");
        }
        PHPVPN_LOG_DEBUG(kComponent, "utun kernel control id " + std::to_string(id));
        verified_ = true;
        return Status::success();
    }

    Result<TunnelAdapterInfo> createAdapter(const std::wstring& name,
                                            const std::wstring& tunnelType,
                                            const std::wstring& requestedGuid) override {
        // utun has no type string; the kernel chooses the interface name.
        (void)tunnelType;
        std::lock_guard<std::mutex> lock(mutex_);
        if (!verified_) {
            return Error(ErrorCode::tunnel_library_missing, "the utun kernel control was not verified");
        }
        if (utunFd_.valid()) {
            return Error(ErrorCode::state_conflict, "an adapter is already open");
        }

        // Everything that can fail without side effects is done before the
        // interface exists.
        std::string logicalName;
        PHPVPN_TRY_ASSIGN(logicalName, toUtf8(name));
        if (logicalName.empty()) {
            return Error(ErrorCode::invalid_argument, "the logical interface name is empty");
        }
        std::string guid;
        PHPVPN_TRY_ASSIGN(guid, canonicalInstallationGuid(requestedGuid));
        const std::string marker = ownershipMarker(guid);
        const std::uint32_t tag = ownerTagForGuid(guid);
        TunnelAdapterInfo info;
        // The GUID as configured, like the Windows and Linux drivers, so the
        // journal comparison of the core stays the same.
        PHPVPN_TRY_ASSIGN(info.adapterGuid, toUtf8(requestedGuid));

        // From here on every early return closes the socket, and the kernel
        // removes the half configured interface with it.
        OwnedFd fd;
        PHPVPN_TRY_ASSIGN(fd, createUtun());
        std::string interfaceName;
        PHPVPN_TRY_ASSIGN(interfaceName, utunInterfaceName(fd.get()));
        if (!setNonBlocking(fd.get())) {
            return adapterFailed("the utun socket could not be made non-blocking", errno);
        }
        // A write after the interface vanished must fail with EPIPE instead of
        // killing the process; the service ignores SIGPIPE anyway.
        const int noSigpipe = 1;
        if (::setsockopt(fd.get(), SOL_SOCKET, SO_NOSIGPIPE, &noSigpipe,
                         static_cast<socklen_t>(sizeof(noSigpipe))) != 0) {
            PHPVPN_LOG_WARN(kComponent, "SIGPIPE could not be suppressed on the utun socket (" +
                                            errnoText(errno) + ")");
        }

        PHPVPN_TRY(configureInterface(interfaceName, marker));
        PHPVPN_TRY(verifyInterface(interfaceName, marker));

        errno = 0;
        const unsigned int index = ::if_nametoindex(interfaceName.c_str());
        if (index == 0 || index > static_cast<unsigned int>(INT_MAX)) {
            return adapterFailed("the utun interface index could not be resolved", errno);
        }
        const std::uint64_t identity = composeInterfaceIdentity(tag, static_cast<int>(index));
        // The same index -> name -> description path the network
        // configuration uses for every later change.
        auto carried = interfaceCarriesIdentity(identity);
        if (!carried.ok()) {
            return adapterFailed("the utun interface identity could not be verified", carried.error());
        }
        if (!carried.value()) {
            return Error(ErrorCode::tunnel_adapter_failed, "the utun interface identity did not verify");
        }

        utunFd_ = std::move(fd);
        interfaceName_ = interfaceName;
        identity_ = identity;

        info.interfaceLuid = identity;
        info.created = true;
        PHPVPN_LOG_INFO(kComponent, "the utun interface " + interfaceName + " is available as " + logicalName);
        return info;
    }

    Result<TunnelAdapterInfo> openAdapter(const std::wstring& name) override {
        // utun interfaces exist only while their creator holds the socket, so
        // there is never an existing interface to open.
        (void)name;
        return Error(ErrorCode::not_supported, "opening an existing utun interface is not supported");
    }

    Result<std::shared_ptr<Tunnel>> startSession(std::uint32_t ringCapacity) override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!utunFd_.valid()) {
            return Error(ErrorCode::tunnel_adapter_failed, "no virtual adapter is open");
        }
        if (tunnel_) {
            return Error(ErrorCode::state_conflict, "a session is already running");
        }
        if (!ringNoted_) {
            ringNoted_ = true;
            PHPVPN_LOG_DEBUG(kComponent, "the ring capacity " + std::to_string(ringCapacity) +
                                             " has no utun equivalent and is ignored");
        }
        // No eventfd on macOS: one pipe per session, the write end signals.
        int ends[2] = {-1, -1};
        if (::pipe(ends) != 0) {
            return Error(ErrorCode::tunnel_session_failed, "the stop pipe could not be created");
        }
        OwnedFd stopRead(ends[0]);
        OwnedFd stopWrite(ends[1]);
        if (!setCloseOnExec(stopRead.get()) || !setCloseOnExec(stopWrite.get()) ||
            !setNonBlocking(stopRead.get()) || !setNonBlocking(stopWrite.get())) {
            return Error(ErrorCode::tunnel_session_failed, "the stop pipe could not be configured");
        }
        tunnel_ = std::make_shared<UtunTunnel>(utunFd_.get(), std::move(stopRead), std::move(stopWrite));
        return std::static_pointer_cast<Tunnel>(tunnel_);
    }

    // Stops and detaches the session. The socket, and with it the interface,
    // stays until closeAdapter, so the core removes its route and address from
    // a live interface in the same order as on Windows.
    void endSession() override {
        std::lock_guard<std::mutex> lock(mutex_);
        endSessionLocked();
    }

    void closeAdapter() override {
        std::lock_guard<std::mutex> lock(mutex_);
        endSessionLocked();
        if (!utunFd_.valid()) return;
        const std::string name = interfaceName_;
        const std::uint64_t identity = identity_;
        utunFd_.reset();
        interfaceName_.clear();
        identity_ = 0;
        waitForDisappearance(name, identity);
    }

    std::uint32_t runningDriverVersion() const override { return 0; }

private:
    void endSessionLocked() {
        if (tunnel_) {
            tunnel_->detach();
            const std::uint64_t dropped = tunnel_->foreignFamilyCount();
            if (dropped != 0) {
                PHPVPN_LOG_DEBUG(kComponent, std::to_string(dropped) +
                                                 " packet(s) of another protocol family were dropped");
            }
            tunnel_.reset();
        }
    }

    // The kernel detaches utun while the socket closes and clears the
    // description on detach. The interface counts as removed when its index is
    // gone or no longer carries the mark; a utunN name alone proves nothing,
    // because the next utun of any process may receive the same name at once.
    static void waitForDisappearance(const std::string& name, std::uint64_t identity) {
        const auto deadline = std::chrono::steady_clock::now() + kDisappearanceWait;
        for (;;) {
            auto present = interfaceCarriesIdentity(identity);
            if (!present.ok()) {
                PHPVPN_LOG_WARN(kComponent, "the removal of the utun interface could not be verified");
                return;
            }
            if (!present.value()) {
                PHPVPN_LOG_INFO(kComponent, "the utun interface " + name + " was removed");
                return;
            }
            if (std::chrono::steady_clock::now() >= deadline) {
                PHPVPN_LOG_ERROR(kComponent, "the utun interface " + name +
                                                 " still exists after its socket was closed");
                return;
            }
            std::this_thread::sleep_for(kDisappearancePoll);
        }
    }

    std::mutex mutex_;
    bool verified_ = false;
    bool ringNoted_ = false;
    OwnedFd utunFd_;
    std::string interfaceName_;
    std::uint64_t identity_ = 0;
    std::shared_ptr<UtunTunnel> tunnel_;
};

}  // namespace

std::unique_ptr<TunnelDriver> createPlatformTunnelDriver() {
    return std::make_unique<UtunDriver>();
}

}  // namespace phpvpn
