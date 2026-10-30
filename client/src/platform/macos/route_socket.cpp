#include "platform/macos/route_socket.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/sysctl.h>
#include <sys/time.h>

#include <fcntl.h>
#include <net/route.h>
#include <poll.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <span>
#include <utility>

namespace phpvpn::routing {
namespace {

// The whole wait for one answer.
constexpr auto kAnswerTimeout = std::chrono::seconds(2);
constexpr int kSendTimeoutSeconds = 2;
constexpr int kDumpAttempts = 3;
// A routing message is the header and at most eight socket addresses of at
// most 255 bytes each, so this buffer never truncates one.
constexpr std::size_t kReceiveBufferBytes = 8 * 1024;
// Room for routes added between the size query and the read of a dump.
constexpr std::size_t kDumpHeadroomBytes = 4 * 1024;

// `what` is a literal, so reading errno for `error` cannot be disturbed by an
// allocation for the message.
Error failure(const char* what, int error) {
    return Error(ErrorCode::netconfig_apply_failed, std::string(what) + ": " + errnoText(error));
}

}  // namespace

std::string errnoText(int error) { return "errno " + std::to_string(error); }

Result<RouteSocket> RouteSocket::open() {
    UniqueFd fd(::socket(PF_ROUTE, SOCK_RAW, AF_INET));
    if (!fd) return failure("the routing socket could not be opened", errno);
    // Darwin has no SOCK_CLOEXEC, so the flag follows right after creation.
    if (::fcntl(fd.get(), F_SETFD, FD_CLOEXEC) != 0) {
        return failure("the routing socket could not be marked close-on-exec", errno);
    }
    timeval timeout{};
    timeout.tv_sec = kSendTimeoutSeconds;
    if (::setsockopt(fd.get(), SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) != 0) {
        return failure("the routing socket timeout could not be set", errno);
    }
    // Distinct starting points keep answers to requests of an earlier socket
    // of this process from matching by accident.
    const auto seed = static_cast<std::uint32_t>(
        std::chrono::steady_clock::now().time_since_epoch().count());
    return RouteSocket(std::move(fd), seed, static_cast<int>(::getpid()));
}

Result<RouteMessage> RouteSocket::request(RouteRequest query) {
    // Positive sequence numbers only, never 0 (the value of kernel events).
    sequence_ = (sequence_ + 1u) & 0x7FFFFFFFu;
    if (sequence_ == 0) sequence_ = 1;
    query.sequence = static_cast<int>(sequence_);
    query.pid = pid_;
    const Bytes message = encode(query);

    for (;;) {
        const ssize_t written = ::write(fd_.get(), message.data(), message.size());
        if (written == static_cast<ssize_t>(message.size())) break;
        if (written < 0 && errno == EINTR) continue;
        if (written < 0) {
            // route_output() refused the request and returned its verdict as
            // the errno of write(); the echoed copy is left unread.
            RouteMessage refused;
            refused.errnoValue = errno;
            refused.version = RTM_VERSION;
            refused.type = query.type;
            refused.sequence = query.sequence;
            refused.pid = query.pid;
            return refused;
        }
        return Error(ErrorCode::netconfig_apply_failed, "the routing request was written partially");
    }

    Bytes buffer(kReceiveBufferBytes);
    const auto deadline = std::chrono::steady_clock::now() + kAnswerTimeout;
    for (;;) {
        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline) {
            return Error(ErrorCode::netconfig_apply_failed, "the routing answer timed out");
        }
        const auto remaining =
            std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count();
        pollfd entry{};
        entry.fd = fd_.get();
        entry.events = POLLIN;
        // One extra millisecond so a sub-millisecond remainder does not spin.
        const int ready = ::poll(&entry, 1, static_cast<int>(remaining) + 1);
        if (ready < 0) {
            if (errno == EINTR) continue;
            return failure("the routing answer could not be awaited", errno);
        }
        if (ready == 0) continue;

        const ssize_t received = ::recv(fd_.get(), buffer.data(), buffer.size(), MSG_DONTWAIT);
        if (received < 0) {
            // ENOBUFS: the socket overflowed with foreign messages; the
            // answer may still follow, otherwise the deadline ends the wait.
            if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK || errno == ENOBUFS) {
                continue;
            }
            return failure("the routing answer could not be received", errno);
        }
        const std::span<const std::uint8_t> datagram(buffer.data(),
                                                     static_cast<std::size_t>(received));
        if (!isReplyTo(datagram, query.type, query.sequence, query.pid)) continue;
        return decode(datagram);
    }
}

Result<std::vector<RouteMessage>> RouteSocket::dump() {
    int mib[6] = {CTL_NET, PF_ROUTE, 0, AF_INET, NET_RT_DUMP, 0};
    constexpr unsigned int kMibLength = 6;
    for (int attempt = 0; attempt < kDumpAttempts; ++attempt) {
        std::size_t size = 0;
        if (::sysctl(mib, kMibLength, nullptr, &size, nullptr, 0) != 0) {
            return failure("the routing table size could not be read", errno);
        }
        size += size / 8 + kDumpHeadroomBytes;
        Bytes buffer(size);
        if (::sysctl(mib, kMibLength, buffer.data(), &size, nullptr, 0) != 0) {
            if (errno == ENOMEM) continue;  // the table outgrew the buffer meanwhile
            return failure("the routing table could not be read", errno);
        }
        buffer.resize(size);
        return decodeDump(buffer);
    }
    return Error(ErrorCode::netconfig_apply_failed,
                 "the routing table kept growing during repeated reads");
}

}  // namespace phpvpn::routing
