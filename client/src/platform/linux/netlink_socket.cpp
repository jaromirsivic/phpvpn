#include "platform/linux/netlink_socket.h"

#include <sys/socket.h>
#include <sys/time.h>

#include <linux/netlink.h>
#include <linux/rtnetlink.h>

#include <cerrno>
#include <chrono>
#include <cstring>

namespace phpvpn::netlink {
namespace {

// One receive may block at most this long; a whole request at most the total.
constexpr int kReceiveTimeoutSeconds = 2;
constexpr auto kRequestBudget = std::chrono::seconds(10);
constexpr int kDumpAttempts = 3;
// Kernel dump datagrams are at most 32 KiB; a larger buffer never truncates.
constexpr std::size_t kReceiveBufferBytes = 64 * 1024;

}  // namespace

// Only the number, like the Windows code, so no localized system text reaches
// logs or IPC error envelopes.
std::string errnoText(int error) { return "errno " + std::to_string(error); }

Result<Socket> Socket::open(ErrorCode failure) {
    UniqueFd fd(::socket(AF_NETLINK, SOCK_RAW | SOCK_CLOEXEC, NETLINK_ROUTE));
    if (!fd) {
        return Error(failure, "the netlink socket could not be opened: " + errnoText(errno));
    }
    timeval timeout{};
    timeout.tv_sec = kReceiveTimeoutSeconds;
    if (::setsockopt(fd.get(), SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) != 0 ||
        ::setsockopt(fd.get(), SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) != 0) {
        return Error(failure, "the netlink socket timeout could not be set: " + errnoText(errno));
    }
    sockaddr_nl local{};
    local.nl_family = AF_NETLINK;
    if (::bind(fd.get(), reinterpret_cast<const sockaddr*>(&local), sizeof(local)) != 0) {
        return Error(failure, "the netlink socket could not be bound: " + errnoText(errno));
    }
    // Distinct starting points keep answers to requests of an earlier socket
    // from matching by accident.
    const auto seed = static_cast<std::uint32_t>(
        std::chrono::steady_clock::now().time_since_epoch().count());
    return Socket(std::move(fd), failure, seed);
}

Result<Reply> Socket::transact(const std::function<Bytes(std::uint32_t)>& build, bool dump,
                               bool& interrupted) {
    const std::uint32_t sequence = ++sequence_;
    const Bytes message = build(sequence);

    sockaddr_nl kernel{};
    kernel.nl_family = AF_NETLINK;
    for (;;) {
        const ssize_t sent = ::sendto(fd_.get(), message.data(), message.size(), 0,
                                      reinterpret_cast<const sockaddr*>(&kernel), sizeof(kernel));
        if (sent == static_cast<ssize_t>(message.size())) break;
        if (sent < 0 && errno == EINTR) continue;
        return Error(failure_, "the netlink request could not be sent: " +
                                   errnoText(sent < 0 ? errno : EMSGSIZE));
    }

    ReplyCollector collector(sequence, dump);
    Bytes buffer(kReceiveBufferBytes);
    const auto deadline = std::chrono::steady_clock::now() + kRequestBudget;
    while (!collector.complete()) {
        if (std::chrono::steady_clock::now() >= deadline) {
            return Error(failure_, "the netlink answer did not complete in time");
        }
        sockaddr_nl source{};
        iovec vector{buffer.data(), buffer.size()};
        msghdr header{};
        header.msg_name = &source;
        header.msg_namelen = sizeof(source);
        header.msg_iov = &vector;
        header.msg_iovlen = 1;
        const ssize_t received = ::recvmsg(fd_.get(), &header, 0);
        if (received < 0) {
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return Error(failure_, "the netlink answer timed out");
            }
            return Error(failure_, "the netlink answer could not be received: " + errnoText(errno));
        }
        if ((header.msg_flags & MSG_TRUNC) != 0) {
            return Error(failure_, "the netlink answer was truncated");
        }
        // Only the kernel may answer; anything else on the socket is ignored.
        if (header.msg_namelen != sizeof(source) || source.nl_pid != 0) continue;
        auto fed = collector.feed(std::span<const std::uint8_t>(
            buffer.data(), static_cast<std::size_t>(received)));
        if (!fed.ok()) return Error(failure_, fed.error().detail);
    }
    interrupted = collector.interrupted();
    Reply reply;
    reply.error = collector.error();
    reply.messages = collector.messages();
    return reply;
}

Result<Reply> Socket::request(const std::function<Bytes(std::uint32_t)>& build) {
    bool interrupted = false;
    return transact(build, false, interrupted);
}

Result<std::vector<Bytes>> Socket::dump(const std::function<Bytes(std::uint32_t)>& build) {
    for (int attempt = 0; attempt < kDumpAttempts; ++attempt) {
        bool interrupted = false;
        Reply reply;
        PHPVPN_TRY_ASSIGN(reply, transact(build, true, interrupted));
        if (reply.error != 0) {
            return Error(failure_, "the netlink dump failed: " + errnoText(reply.error));
        }
        if (!interrupted) return std::move(reply.messages);
    }
    return Error(failure_, "the netlink dump stayed inconsistent after repeated attempts");
}

Result<std::optional<LinkRecord>> Socket::singleLink(Result<Reply> reply) {
    if (!reply.ok()) return reply.error();
    if (reply->error == ENODEV) return std::optional<LinkRecord>();
    if (reply->error != 0) {
        return Error(failure_, "the interface could not be queried: " + errnoText(reply->error));
    }
    for (const Bytes& message : reply->messages) {
        auto link = decodeLink(message);
        if (!link.ok()) return Error(failure_, link.error().detail);
        return std::optional<LinkRecord>(std::move(link).value());
    }
    return Error(failure_, "the interface query returned no interface");
}

Result<std::optional<LinkRecord>> Socket::linkByIndex(int index) {
    if (index <= 0) return std::optional<LinkRecord>();
    return singleLink(request([index](std::uint32_t sequence) {
        return encodeLinkGetByIndex(sequence, index);
    }));
}

Result<std::optional<LinkRecord>> Socket::linkByName(const std::string& name) {
    return singleLink(request([&name](std::uint32_t sequence) {
        return encodeLinkGetByName(sequence, name);
    }));
}

}  // namespace phpvpn::netlink
