#include "platform/posix/ipc_unix.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <sys/un.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstring>
#include <limits>
#include <mutex>
#include <optional>
#include <thread>

#include "core/ipc/ipc_protocol.h"
#include "platform/iface/ipc_executors.h"
#include "platform/posix/event_wait.h"
#include "platform/posix/fd.h"

namespace phpvpn {
namespace {
constexpr std::size_t kSlots = 32;
constexpr std::size_t kHeaderBytes = 4;
constexpr std::size_t kChunkBytes = 64 * 1024;
constexpr std::int64_t kTickMs = 250;
constexpr std::int64_t kConnectRetryMs = 10;
constexpr std::int64_t kMaxClientTimeoutMs = std::numeric_limits<std::int32_t>::max();
constexpr mode_t kSocketMode = 0666;
static_assert(kIpcMaxMessageBytes <= std::numeric_limits<std::uint32_t>::max());

// A peer that closed must not raise SIGPIPE: Linux asks per send, macOS has no
// usable MSG_NOSIGNAL and sets SO_NOSIGPIPE on every socket instead.
#if defined(__APPLE__)
constexpr int kSendFlags = MSG_DONTWAIT;
#else
constexpr int kSendFlags = MSG_NOSIGNAL | MSG_DONTWAIT;
#endif

std::int64_t nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}
bool validSocketPath(std::string_view path) {
    return path.size() > 1 && path.front() == '/' && path.back() != '/' &&
        path.size() < sizeof(sockaddr_un{}.sun_path) && path.find('\0') == std::string_view::npos;
}
std::string parentDirectory(const std::string& path) {
    const auto slash = path.rfind('/');
    return slash == 0 ? std::string("/") : path.substr(0, slash);
}
socklen_t socketAddress(const std::string& path, sockaddr_un& address) {
    std::memset(&address, 0, sizeof(address));
    address.sun_family = AF_UNIX;
    std::memcpy(address.sun_path, path.data(), path.size());
    const auto length = static_cast<socklen_t>(offsetof(sockaddr_un, sun_path) + path.size() + 1);
#if defined(__APPLE__)
    address.sun_len = static_cast<unsigned char>(length);  // BSD sockaddr length byte
#endif
    return length;
}
void encodeLength(std::size_t length, unsigned char* out) {
    const auto value = static_cast<std::uint32_t>(length);
    out[0] = static_cast<unsigned char>(value >> 24);
    out[1] = static_cast<unsigned char>(value >> 16);
    out[2] = static_cast<unsigned char>(value >> 8);
    out[3] = static_cast<unsigned char>(value);
}
std::uint32_t decodeLength(const unsigned char* in) {
    return (std::uint32_t(in[0]) << 24) | (std::uint32_t(in[1]) << 16) |
        (std::uint32_t(in[2]) << 8) | std::uint32_t(in[3]);
}
#if defined(__APPLE__)
// What SOCK_CLOEXEC|SOCK_NONBLOCK and MSG_NOSIGNAL do on Linux.
bool configureSocket(int fd) {
    if (::fcntl(fd, F_SETFD, FD_CLOEXEC) != 0) return false;
    const int flags = ::fcntl(fd, F_GETFL);
    if (flags < 0 || ::fcntl(fd, F_SETFL, flags | O_NONBLOCK) != 0) return false;
    const int one = 1;
    return ::setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one)) == 0;
}
#endif
// Non-blocking close-on-exec Unix stream socket, -1 on failure.
int openStreamSocket() {
#if defined(__APPLE__)
    UniqueFd fd(::socket(AF_UNIX, SOCK_STREAM, 0));
    if (!fd || !configureSocket(fd.get())) return -1;
    return fd.release();
#else
    return ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
#endif
}
// Non-blocking close-on-exec accepted connection, -1 with errno on failure.
int acceptConnection(int listener) {
#if defined(__APPLE__)
    UniqueFd fd(::accept(listener, nullptr, nullptr));
    if (!fd) return -1;
    if (!configureSocket(fd.get())) {
        errno = ECONNABORTED;  // Dropped like a connection aborted before accept.
        return -1;
    }
    return fd.release();
#else
    return ::accept4(listener, nullptr, nullptr, SOCK_CLOEXEC | SOCK_NONBLOCK);
#endif
}
// True when the peer of a connected socket runs as `expected`.
bool peerHasUid(int fd, uid_t expected) {
#if defined(__APPLE__)
    uid_t uid = 0;
    gid_t gid = 0;
    return ::getpeereid(fd, &uid, &gid) == 0 && uid == expected;
#else
    ucred peer{};
    socklen_t peerSize = sizeof(peer);
    return getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &peer, &peerSize) == 0 && peerSize == sizeof(peer) &&
        peer.uid == expected;
#endif
}
}  // namespace

struct IpcSocketServer::Impl {
    enum class Phase { idle, reading, executing, writing, awaitingClose };
    struct Slot {
        UniqueFd fd;
        Phase phase = Phase::idle;
        bool watched = false;
        std::array<unsigned char, kHeaderBytes> header{};
        std::size_t headerFilled = 0;
        std::string request;
        std::size_t requestFilled = 0;
        std::shared_ptr<IpcJob> job;
        std::size_t written = 0; // Of header plus response.
        std::int64_t deadline = 0;
    };
    // Poller tags beyond the slot indexes. The tick is reported by the poller.
    static constexpr std::uint64_t kListenerTag = kSlots;
    static constexpr std::uint64_t kStopTag = kSlots + 1;
    static constexpr std::uint64_t kCompletedTag = kSlots + 2;

    IpcSocketServerOptions options;
    std::string path;
    std::mutex lifecycle;
    std::atomic<bool> running{false};
    Poller poller;
    Notifier stopEvent, completedEvent;
    UniqueFd listener;
    bool listenerWatched = false;
    std::int64_t listenerRetryAt = 0;
    bool socketBound = false;
    dev_t socketDevice = 0;
    ino_t socketInode = 0;
    std::array<Slot, kSlots> slots;
    IpcExecutors executors;
    std::thread loop;
    std::atomic<std::size_t> allocatedSlots{0};
    std::atomic<std::uint64_t> ioTimeouts{0};

    Impl(IpcHandler h, IpcSocketServerOptions o)
        : options(std::move(o)),
          path(options.socketPath.empty() ? kIpcSocketPath : options.socketPath),
          // No fixed per-connection reservation exists here, so only the size bound applies.
          executors(std::move(h), options, std::numeric_limits<std::size_t>::max(),
                    [this] { completedEvent.signal(); }) {}

    bool watch(int fd, std::uint64_t tag, std::uint32_t interest, bool& watched) {
        if (!(watched ? poller.modify(fd, tag, interest) : poller.add(fd, tag, interest))) return false;
        watched = true;
        return true;
    }
    void unwatch(int fd, bool& watched) {
        if (watched) poller.remove(fd);
        watched = false;
    }
    std::size_t index(const Slot& slot) const { return static_cast<std::size_t>(&slot - slots.data()); }
    bool watchSlot(Slot& slot, std::uint32_t interest) {
        return watch(slot.fd.get(), index(slot), interest, slot.watched);
    }
    void armListener() {
        if (listenerWatched || !listener) return;
        if (!watch(listener.get(), kListenerTag, kPollReadable, listenerWatched)) listenerRetryAt = nowMs() + kTickMs;
    }
    void recycle(Slot& slot) {
        unwatch(slot.fd.get(), slot.watched);
        slot.fd.reset();
        slot.phase = Phase::idle;
        slot.headerFilled = 0;
        slot.requestFilled = 0;
        std::string{}.swap(slot.request);
        slot.job.reset();
        slot.written = 0;
        slot.deadline = 0;
        // A freed slot lets waiting connections in again.
        if (running.load(std::memory_order_acquire)) armListener();
    }
    Slot* freeSlot() {
        for (auto& slot : slots) if (!slot.fd) return &slot;
        return nullptr;
    }
    void accept() {
        // Bounded: at most one accept attempt per slot and pass.
        for (std::size_t attempt = 0; attempt < kSlots; ++attempt) {
            Slot* slot = freeSlot();
            if (!slot) break;
            const int fd = acceptConnection(listener.get());
            if (fd < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) return;
                if (errno == EINTR || errno == ECONNABORTED) continue;
                // Descriptor exhaustion and similar: retry by bounded maintenance.
                unwatch(listener.get(), listenerWatched);
                listenerRetryAt = nowMs() + kTickMs;
                return;
            }
            slot->fd.reset(fd);
            slot->phase = Phase::reading;
            slot->deadline = nowMs() + options.ioDeadlineMs;
            if (!watchSlot(*slot, kPollReadable)) recycle(*slot);
        }
        // All 32 slots are busy. Further clients wait in the listen backlog, as a
        // Windows client waits for a free pipe instance.
        if (!freeSlot()) unwatch(listener.get(), listenerWatched);
    }
    void read(Slot& slot) {
        if (slot.headerFilled < kHeaderBytes) {
            const ssize_t count = recv(slot.fd.get(), slot.header.data() + slot.headerFilled,
                                       kHeaderBytes - slot.headerFilled, 0);
            if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) return;
            if (count <= 0) { recycle(slot); return; }
            slot.headerFilled += static_cast<std::size_t>(count);
            if (slot.headerFilled < kHeaderBytes) return;
            const std::uint32_t length = decodeLength(slot.header.data());
            if (length == 0) { recycle(slot); return; }
            if (length > kIpcMaxMessageBytes) {
                executors.countRejected();
                recycle(slot);
                return;
            }
            slot.request.resize(length);
            return;
        }
        const std::size_t wanted = std::min(kChunkBytes, slot.request.size() - slot.requestFilled);
        const ssize_t count = recv(slot.fd.get(), slot.request.data() + slot.requestFilled, wanted, 0);
        if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) return;
        if (count <= 0) { recycle(slot); return; } // Truncated request.
        slot.requestFilled += static_cast<std::size_t>(count);
        if (slot.requestFilled == slot.request.size()) dispatch(slot);
    }
    void dispatch(Slot& slot) {
        try {
            slot.job = std::make_shared<IpcJob>();
            slot.job->request = std::move(slot.request);
            switch (executors.admit(slot.job)) {
            case IpcAdmission::queued:
                // No readiness is observed while the handler runs.
                unwatch(slot.fd.get(), slot.watched);
                slot.phase = Phase::executing;
                slot.deadline = 0;
                return;
            case IpcAdmission::respond: startWrite(slot); return;
            case IpcAdmission::close: break;
            }
            recycle(slot);
        } catch (...) { recycle(slot); }
    }
    void startWrite(Slot& slot) {
        encodeLength(slot.job->response.size(), slot.header.data());
        slot.written = 0;
        slot.phase = Phase::writing;
        slot.deadline = nowMs() + options.ioDeadlineMs;
        if (!watchSlot(slot, kPollWritable)) { recycle(slot); return; }
        write(slot);
    }
    void write(Slot& slot) {
        const std::string& response = slot.job->response;
        iovec parts[2]{};
        int count = 0;
        if (slot.written < kHeaderBytes) {
            parts[count++] = {slot.header.data() + slot.written, kHeaderBytes - slot.written};
            parts[count++] = {const_cast<char*>(response.data()), response.size()};
        } else {
            const std::size_t offset = slot.written - kHeaderBytes;
            parts[count++] = {const_cast<char*>(response.data()) + offset, response.size() - offset};
        }
        msghdr message{};
        message.msg_iov = parts;
        message.msg_iovlen = static_cast<decltype(message.msg_iovlen)>(count);
        const ssize_t sent = sendmsg(slot.fd.get(), &message, kSendFlags);
        if (sent < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) return;
            recycle(slot);
            return;
        }
        slot.written += static_cast<std::size_t>(sent);
        if (slot.written < kHeaderBytes + response.size()) return;
        // The response is in the kernel. Keep the connection until the client
        // closes after reading or the output deadline expires, as on Windows.
        slot.job.reset();
        shutdown(slot.fd.get(), SHUT_WR);
        slot.phase = Phase::awaitingClose;
        if (!watchSlot(slot, kPollReadable)) recycle(slot);
    }
    void advance(Slot& slot, std::uint32_t flags) {
        switch (slot.phase) {
        case Phase::reading: read(slot); break;
        case Phase::writing:
            if (flags & (kPollWritable | kPollError | kPollHangup)) write(slot);
            break;
        case Phase::awaitingClose: {
            char byte = 0;
            const ssize_t count = recv(slot.fd.get(), &byte, 1, 0);
            if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) break;
            recycle(slot); // Closed, failed, or a second request, which is not a second command.
            break;
        }
        case Phase::idle:
        case Phase::executing: break; // Stale event of a recycled or parked slot.
        }
    }
    void completions() {
        completedEvent.drain();
        for (auto& slot : slots) {
            if (slot.phase != Phase::executing || !slot.job ||
                !slot.job->done.load(std::memory_order_acquire)) continue;
            if (slot.job->closeOnly) recycle(slot);
            else startWrite(slot);
        }
    }
    void runLoop() {
        std::array<PollEvent, kSlots + 4> events{};
        while (running.load(std::memory_order_acquire)) {
            const std::int64_t now = nowMs();
            std::int64_t wake = -1;
            for (auto& slot : slots) {
                if (!slot.fd || !slot.deadline) continue;
                if (now >= slot.deadline) {
                    ioTimeouts.fetch_add(1);
                    recycle(slot);
                } else if (wake < 0 || slot.deadline < wake) wake = slot.deadline;
            }
            if (!listenerWatched && listenerRetryAt && now >= listenerRetryAt) {
                listenerRetryAt = 0;
                if (freeSlot()) armListener();
            }
            const std::int64_t timeout = wake < 0 ? -1 : std::min<std::int64_t>(wake - now, kTickMs);
            const PollWait ready = poller.wait(events, timeout);
            if (ready.error != 0) {
                if (ready.error == EINTR) continue;
                break;
            }
            for (std::size_t i = 0; i < ready.count; ++i) {
                const auto tag = events[i].tag;
                const auto flags = events[i].flags;
                if (tag == kStopTag) return;
                if (tag == kCompletedTag) {
                    completions();
                } else if (tag == kListenerTag) {
                    if (listenerWatched) accept();
                } else if (tag < kSlots) {
                    auto& slot = slots[tag];
                    if (!slot.fd) continue;
                    try { advance(slot, flags); } catch (...) { recycle(slot); }
                }
            }
            if (ready.tick && options.onTick) { try { options.onTick(); } catch (...) {} }
        }
    }
    Status prepareDirectory() {
        const std::string directory = parentDirectory(path);
        struct stat info{};
        if (lstat(directory.c_str(), &info) != 0)
            return Error(ErrorCode::ipc_unavailable, "the socket directory does not exist");
        if (!S_ISDIR(info.st_mode))
            return Error(ErrorCode::ipc_unavailable, "the socket directory is not a directory");
        if (info.st_uid != options.expectedOwnerUid)
            return Error(ErrorCode::ipc_unavailable, "the socket directory has an unexpected owner");
        if (info.st_mode & (S_IWGRP | S_IWOTH))
            return Error(ErrorCode::ipc_unavailable, "the socket directory is writable by other users");
        // Only a stale socket of the expected owner is replaced; anything else
        // in its place means the directory is not in the expected state.
        if (lstat(path.c_str(), &info) == 0) {
            if (!S_ISSOCK(info.st_mode) || info.st_uid != options.expectedOwnerUid)
                return Error(ErrorCode::ipc_unavailable, "an unexpected file occupies the socket path");
            if (unlink(path.c_str()) != 0 && errno != ENOENT)
                return Error(ErrorCode::ipc_unavailable, "the stale socket could not be removed");
        } else if (errno != ENOENT) {
            return Error(ErrorCode::ipc_unavailable, "the socket path could not be inspected");
        }
        return Status::success();
    }
    Status openEndpoint() {
        auto createdPoller = Poller::create();
        auto createdStop = Notifier::create();
        auto createdCompleted = Notifier::create();
        if (!createdPoller || !createdStop || !createdCompleted) return Error(ErrorCode::ipc_unavailable);
        poller = std::move(createdPoller).value();
        stopEvent = std::move(createdStop).value();
        completedEvent = std::move(createdCompleted).value();
        auto prepared = prepareDirectory();
        if (!prepared) return prepared;
        listener.reset(openStreamSocket());
        if (!listener) return Error(ErrorCode::ipc_unavailable);
        sockaddr_un address{};
        const socklen_t length = socketAddress(path, address);
        if (bind(listener.get(), reinterpret_cast<const sockaddr*>(&address), length) != 0)
            return Error(ErrorCode::ipc_unavailable, "the local socket could not be opened");
        struct stat info{};
        if (lstat(path.c_str(), &info) != 0) return Error(ErrorCode::ipc_unavailable);
        socketBound = true;
        socketDevice = info.st_dev;
        socketInode = info.st_ino;
        // Public IPC (client.md): every local process may connect.
        if (chmod(path.c_str(), kSocketMode) != 0 || listen(listener.get(), static_cast<int>(kSlots)) != 0)
            return Error(ErrorCode::ipc_unavailable, "the local socket could not be opened");
        bool stopWatched = false, completedWatched = false;
        if (!watch(stopEvent.fd(), kStopTag, kPollReadable, stopWatched) ||
            !watch(completedEvent.fd(), kCompletedTag, kPollReadable, completedWatched))
            return Error(ErrorCode::ipc_unavailable);
        listenerWatched = false;
        listenerRetryAt = 0;
        armListener();
        if (!listenerWatched) return Error(ErrorCode::ipc_unavailable);
        // First maintenance immediately, then every 250 ms.
        if (!poller.startTick(kTickMs)) return Error(ErrorCode::ipc_unavailable);
        return Status::success();
    }
    Status start() {
        std::lock_guard lock(lifecycle);
        if (running.load()) return Error(ErrorCode::service_already_running);
        if (!executors.hasHandler() || !validSocketPath(path) || options.ioDeadlineMs <= 0)
            return Error(ErrorCode::invalid_argument);
        try {
            auto opened = openEndpoint();
            if (!opened) { cleanup(); return opened; }
            allocatedSlots.store(kSlots);
            running.store(true, std::memory_order_release);
            executors.start();
            loop = std::thread([this] { runLoop(); });
            return Status::success();
        } catch (...) {
            stopLocked();
            return Error(ErrorCode::ipc_unavailable);
        }
    }
    void cleanup() {
        for (auto& slot : slots) {
            slot.watched = false;
            slot.fd.reset();
            slot.phase = Phase::idle;
            slot.headerFilled = 0;
            slot.requestFilled = 0;
            std::string{}.swap(slot.request);
            slot.job.reset();
            slot.written = 0;
            slot.deadline = 0;
        }
        listener.reset();
        listenerWatched = false;
        if (socketBound) {
            // Remove the name only while it still refers to this server's socket.
            struct stat info{};
            if (lstat(path.c_str(), &info) == 0 && S_ISSOCK(info.st_mode) &&
                info.st_dev == socketDevice && info.st_ino == socketInode) unlink(path.c_str());
            socketBound = false;
        }
        allocatedSlots.store(0);
        stopEvent = Notifier{};
        completedEvent = Notifier{};
        poller = Poller{};
    }
    void stopLocked() {
        const bool wasRunning = running.exchange(false, std::memory_order_acq_rel);
        executors.closeAdmission();
        stopEvent.signal();
        // cancelHandlers once, then the executors finish their queues.
        executors.shutdown(wasRunning);
        if (loop.joinable()) loop.join();
        // Accepted jobs and tasks are drained after cancellation, never dropped.
        executors.join();
        cleanup();
    }
    void stop() { std::lock_guard lock(lifecycle); stopLocked(); }
};

IpcSocketServer::IpcSocketServer(IpcHandler handler)
    : IpcSocketServer(std::move(handler), {}) {}
IpcSocketServer::IpcSocketServer(IpcHandler handler, IpcSocketServerOptions options)
    : impl_(std::make_unique<Impl>(std::move(handler), std::move(options))) {}
IpcSocketServer::~IpcSocketServer() { stop(); }
Status IpcSocketServer::start() { return impl_->start(); }
void IpcSocketServer::stop() { impl_->stop(); }
bool IpcSocketServer::tryPostFile(std::function<void()> task) {
    return impl_->executors.tryPost(IpcWorkClass::file, std::move(task));
}
bool IpcSocketServer::tryPostDiagnostic(std::function<void()> task) {
    return impl_->executors.tryPost(IpcWorkClass::diagnostic, std::move(task));
}
IpcServerStats IpcSocketServer::stats() const {
    IpcServerStats result;
    result.connectionSlots = impl_->allocatedSlots.load();
    impl_->executors.fillStats(result);
    result.ioTimeouts = impl_->ioTimeouts.load();
    return result;
}

namespace {
enum class Wait { ready, timeout, cancelled, failed };
// Waits for events on fd, or only for the deadline when fd is negative.
Wait waitFor(int fd, short events, std::int64_t deadline, int cancelFd) {
    for (;;) {
        pollfd fds[2]{};
        nfds_t count = 0;
        if (cancelFd >= 0) fds[count++] = {cancelFd, POLLIN, 0};
        if (fd >= 0) fds[count++] = {fd, events, 0};
        const std::int64_t left = std::max<std::int64_t>(0, deadline - nowMs());
        const int ready = poll(fds, count, static_cast<int>(std::min<std::int64_t>(left, kMaxClientTimeoutMs)));
        if (ready < 0) {
            if (errno == EINTR) continue;
            return Wait::failed;
        }
        if (cancelFd >= 0 && fds[0].revents) return Wait::cancelled;
        if (fd < 0) {
            if (ready == 0) return Wait::ready; // Requested pause elapsed.
            continue;
        }
        if (ready > 0) return Wait::ready;
        if (nowMs() >= deadline) return Wait::timeout;
    }
}
}  // namespace

Result<std::string> IpcSocketClient::exchange(std::string_view request, std::int64_t timeoutMs,
                                              int cancelFd, IpcExchangeOutcome* outcome) const {
    IpcExchangeOutcome local;
    IpcExchangeOutcome& out = outcome ? *outcome : local;
    out = {};
    auto fail = [&out](IpcSocketFailure failure, ErrorCode code, const char* detail = "") {
        out.failure = failure;
        return Result<std::string>(Error(code, detail));
    };
    auto waitFailure = [&](Wait wait) {
        if (wait == Wait::cancelled) return fail(IpcSocketFailure::cancelled, ErrorCode::cancelled);
        if (wait == Wait::timeout) return fail(IpcSocketFailure::timeout, ErrorCode::timeout);
        return fail(IpcSocketFailure::connectionClosed, ErrorCode::ipc_unavailable);
    };
    const std::string path = options_.socketPath.empty() ? kIpcSocketPath : options_.socketPath;
    if (!validSocketPath(path) || timeoutMs <= 0 || timeoutMs > kMaxClientTimeoutMs)
        return fail(IpcSocketFailure::invalidArgument, ErrorCode::invalid_argument);
    if (request.size() > kIpcMaxMessageBytes)
        return fail(IpcSocketFailure::requestTooLarge, ErrorCode::ipc_message_too_large);
    const std::int64_t deadline = nowMs() + timeoutMs;
    UniqueFd fd(openStreamSocket());
    if (!fd) return fail(IpcSocketFailure::connectionClosed, ErrorCode::ipc_unavailable);
    sockaddr_un address{};
    const socklen_t addressLength = socketAddress(path, address);
    for (;;) {
        if (connect(fd.get(), reinterpret_cast<const sockaddr*>(&address), addressLength) == 0) break;
        const int error = errno;
        if (error == EINTR) continue;
        if (error == ENOENT || error == ECONNREFUSED || error == ENOTDIR)
            return fail(IpcSocketFailure::serviceNotRunning, ErrorCode::ipc_unavailable, "the service is not running");
        if (error == EACCES || error == EPERM)
            return fail(IpcSocketFailure::accessDenied, ErrorCode::ipc_unavailable, "access to the service socket was denied");
        if (error == EAGAIN) {
            // Full listen backlog: every server slot is busy. Retry until the deadline.
            const auto wait = waitFor(-1, 0, std::min(deadline, nowMs() + kConnectRetryMs), cancelFd);
            if (wait != Wait::ready) return waitFailure(wait);
            if (nowMs() >= deadline) return fail(IpcSocketFailure::timeout, ErrorCode::timeout);
            continue;
        }
        if (error == EINPROGRESS) {
            const auto wait = waitFor(fd.get(), POLLOUT, deadline, cancelFd);
            if (wait != Wait::ready) return waitFailure(wait);
            int result = 0;
            socklen_t size = sizeof(result);
            if (getsockopt(fd.get(), SOL_SOCKET, SO_ERROR, &result, &size) != 0 || result != 0)
                return fail(IpcSocketFailure::serviceNotRunning, ErrorCode::ipc_unavailable, "the service is not running");
            break;
        }
        return fail(IpcSocketFailure::connectionClosed, ErrorCode::ipc_unavailable);
    }
    // Nothing, in particular no secret, is sent to a peer that is not the service.
    if (!peerHasUid(fd.get(), options_.expectedServerUid))
        return fail(IpcSocketFailure::invalidPeer, ErrorCode::ipc_unavailable,
                    "the socket peer is not the phpvpn service");

    std::array<unsigned char, kHeaderBytes> header{};
    encodeLength(request.size(), header.data());
    std::size_t written = 0;
    const std::size_t total = kHeaderBytes + request.size();
    while (written < total) {
        iovec parts[2]{};
        int count = 0;
        if (written < kHeaderBytes) {
            parts[count++] = {header.data() + written, kHeaderBytes - written};
            parts[count++] = {const_cast<char*>(request.data()), request.size()};
        } else {
            const std::size_t offset = written - kHeaderBytes;
            parts[count++] = {const_cast<char*>(request.data()) + offset, request.size() - offset};
        }
        msghdr message{};
        message.msg_iov = parts;
        message.msg_iovlen = static_cast<decltype(message.msg_iovlen)>(count);
        const ssize_t sent = sendmsg(fd.get(), &message, kSendFlags);
        if (sent >= 0) { written += static_cast<std::size_t>(sent); continue; }
        if (errno == EINTR) continue;
        if (errno != EAGAIN && errno != EWOULDBLOCK)
            return fail(IpcSocketFailure::connectionClosed, ErrorCode::ipc_unavailable);
        const auto wait = waitFor(fd.get(), POLLOUT, deadline, cancelFd);
        if (wait != Wait::ready) return waitFailure(wait);
    }
    out.requestSent = true;

    // Reads exactly size bytes. A close before the first response byte is a lost
    // connection, a close inside the frame is a truncated response.
    std::size_t received = 0;
    auto readExact = [&](char* target, std::size_t size) -> std::optional<Result<std::string>> {
        std::size_t filled = 0;
        while (filled < size) {
            const ssize_t count = recv(fd.get(), target + filled, std::min(kChunkBytes, size - filled), 0);
            if (count > 0) { filled += static_cast<std::size_t>(count); received += static_cast<std::size_t>(count); continue; }
            if (count < 0 && errno == EINTR) continue;
            if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                const auto wait = waitFor(fd.get(), POLLIN, deadline, cancelFd);
                if (wait != Wait::ready) return waitFailure(wait);
                continue;
            }
            if (received == 0) return fail(IpcSocketFailure::connectionClosed, ErrorCode::ipc_unavailable);
            return fail(IpcSocketFailure::truncatedResponse, ErrorCode::ipc_malformed, "the response was truncated");
        }
        return std::nullopt;
    };
    if (auto failed = readExact(reinterpret_cast<char*>(header.data()), kHeaderBytes)) return std::move(*failed);
    const std::uint32_t length = decodeLength(header.data());
    if (length == 0) return fail(IpcSocketFailure::invalidResponse, ErrorCode::ipc_malformed);
    if (length > kIpcMaxMessageBytes) return fail(IpcSocketFailure::responseTooLarge, ErrorCode::ipc_message_too_large);
    std::string response(length, '\0');
    if (auto failed = readExact(response.data(), length)) return std::move(*failed);
    return response;
}

Result<std::string> sendIpcRequest(std::string_view requestJson, std::int64_t timeoutMs) {
    return IpcSocketClient().exchange(requestJson, timeoutMs);
}
}  // namespace phpvpn
