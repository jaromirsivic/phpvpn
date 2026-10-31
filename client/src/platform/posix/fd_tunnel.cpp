// Packet I/O over a TUN-like descriptor, see fd_tunnel.h. Moved unchanged out
// of platform/linux/tunnel_tun.cpp so that Android runs the same code.
//
// A packet never leaves this file by reference: receive copies into the caller
// buffer with readv, send writes one caller packet. Both wait with poll on the
// descriptor together with an eventfd, so signalStop always wakes a worker.
#include "platform/posix/fd_tunnel.h"

#include <poll.h>
#include <sys/uio.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <climits>
#include <mutex>
#include <utility>

namespace phpvpn {
namespace {

// Same bounded write wait as Wintun: 200 rounds of at most 5 ms.
constexpr int kSendAttempts = 200;
constexpr int kSendWaitMs = 5;

}  // namespace

FdTunnel::FdTunnel(int fd, UniqueFd stopEvent) : fd_(fd), stopEvent_(std::move(stopEvent)) {}

Result<TunnelReadResult> FdTunnel::receive(std::span<std::uint8_t> buffer, std::int64_t timeoutMs) {
    std::shared_lock<std::shared_mutex> lock(ioMutex_);
    for (int round = 0; round < 2; ++round) {
        if (stopped_.load() || fd_ < 0) {
            return TunnelReadResult{TunnelReadStatus::stopped, 0};
        }

        // The extra byte detects a packet longer than the caller buffer:
        // the kernel truncates it, and it is counted instead of delivered.
        std::uint8_t overflow = 0;
        iovec vectors[2] = {{buffer.data(), buffer.size()}, {&overflow, 1}};
        const ssize_t count = ::readv(fd_, vectors, 2);
        if (count >= 0) {
            if (static_cast<std::size_t>(count) > buffer.size()) {
                oversized_.fetch_add(1);
                return TunnelReadResult{TunnelReadStatus::empty, 0};
            }
            if (count == 0) return TunnelReadResult{TunnelReadStatus::empty, 0};
            return TunnelReadResult{TunnelReadStatus::packet, static_cast<std::size_t>(count)};
        }
        if (errno == EINTR) return TunnelReadResult{TunnelReadStatus::empty, 0};
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            return Error(ErrorCode::tunnel_io_failed, "the virtual interface could not be read");
        }
        if (round == 1) break;

        pollfd waits[2] = {{fd_, POLLIN, 0}, {stopEvent_.get(), POLLIN, 0}};
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

Status FdTunnel::send(std::span<const std::uint8_t> packet) {
    if (packet.empty() || packet.size() > kMaxTunnelPacketBytes) {
        return Error(ErrorCode::wire_invalid_packet, "the packet length is out of range");
    }
    std::shared_lock<std::shared_mutex> lock(ioMutex_);
    // A full queue produces a bounded wait rather than a busy loop, and the
    // stop event always wins so the worker can be joined.
    for (int attempt = 0; attempt < kSendAttempts; ++attempt) {
        if (stopped_.load() || fd_ < 0) {
            return Error(ErrorCode::cancelled, "the tunnel is stopping");
        }
        const ssize_t written = ::write(fd_, packet.data(), packet.size());
        if (written == static_cast<ssize_t>(packet.size())) return Status::success();
        if (written >= 0) {
            return Error(ErrorCode::tunnel_io_failed, "the virtual interface accepted a partial packet");
        }
        if (errno == EINTR) continue;
        if (errno != EAGAIN && errno != EWOULDBLOCK && errno != ENOBUFS) {
            return Error(ErrorCode::tunnel_io_failed, "the virtual interface could not be written");
        }
        pollfd waits[2] = {{fd_, POLLOUT, 0}, {stopEvent_.get(), POLLIN, 0}};
        const int ready = ::poll(waits, 2, kSendWaitMs);
        if (ready > 0 && waits[1].revents != 0) {
            return Error(ErrorCode::cancelled, "the tunnel is stopping");
        }
        if (ready < 0 && errno != EINTR) {
            return Error(ErrorCode::tunnel_io_failed, "the write wait failed");
        }
    }
    return Error(ErrorCode::busy, "the virtual interface queue stayed full");
}

void FdTunnel::signalStop() {
    stopped_.store(true);
    // The counter is never read, so the eventfd stays readable and every
    // later wait returns immediately as well.
    const std::uint64_t one = 1;
    const ssize_t written = ::write(stopEvent_.get(), &one, sizeof(one));
    (void)written;
}

std::uint64_t FdTunnel::oversizedReceiveCount() const { return oversized_.load(); }

void FdTunnel::detach() {
    signalStop();
    std::unique_lock<std::shared_mutex> lock(ioMutex_);
    fd_ = -1;
}

}  // namespace phpvpn
