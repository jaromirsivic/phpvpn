// FdTunnel (platform/posix/fd_tunnel.h) without a TUN device, shared by the
// Linux suite and the Android tests (plan-android-v001 WP02). A non-blocking
// AF_UNIX SOCK_SEQPACKET pair keeps the packet boundaries of a TUN or
// VpnService descriptor, so receive, the oversized count, the stop event and
// the bounded send wait run without privileges. The netns tests cover the same
// class on a real TUN interface, except the full queue, which a TUN write
// practically never reports.
#include "harness.h"

#include <fcntl.h>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstdint>
#include <thread>
#include <vector>

#include "platform/posix/fd.h"
#include "platform/posix/fd_tunnel.h"

namespace phpvpn {
namespace {

struct Pair {
    UniqueFd tunnelSide;
    UniqueFd peer;
};

Pair makePair() {
    int fds[2] = {-1, -1};
    if (::socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_NONBLOCK | SOCK_CLOEXEC, 0, fds) != 0) return {};
    return Pair{UniqueFd(fds[0]), UniqueFd(fds[1])};
}

UniqueFd makeStopEvent() { return UniqueFd(::eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK)); }

std::vector<std::uint8_t> packet(std::size_t size, std::uint8_t seed) {
    std::vector<std::uint8_t> bytes(size);
    for (std::size_t index = 0; index < size; ++index) bytes[index] = static_cast<std::uint8_t>(seed + index);
    return bytes;
}

}  // namespace

PHPVPN_TEST(FdTunnel, PacketsKeepTheirBoundariesInBothDirections) {
    Pair pair = makePair();
    PHPVPN_CHECK(pair.tunnelSide.valid() && pair.peer.valid());
    if (!pair.tunnelSide) return;
    FdTunnel tunnel(pair.tunnelSide.get(), makeStopEvent());

    const auto outbound = packet(1400, 7);
    PHPVPN_REQUIRE_OK(tunnel.send(outbound));
    std::vector<std::uint8_t> received(2000);
    PHPVPN_CHECK_EQ(::read(pair.peer.get(), received.data(), received.size()), static_cast<ssize_t>(outbound.size()));
    received.resize(outbound.size());
    PHPVPN_CHECK(received == outbound);

    const auto first = packet(60, 1);
    const auto second = packet(1500, 2);
    PHPVPN_CHECK_EQ(::write(pair.peer.get(), first.data(), first.size()), static_cast<ssize_t>(first.size()));
    PHPVPN_CHECK_EQ(::write(pair.peer.get(), second.data(), second.size()), static_cast<ssize_t>(second.size()));
    std::vector<std::uint8_t> buffer(1500);
    auto one = tunnel.receive(buffer, 1000);
    PHPVPN_REQUIRE_OK(one);
    PHPVPN_CHECK(one->status == TunnelReadStatus::packet);
    PHPVPN_CHECK_EQ(one->size, first.size());
    PHPVPN_CHECK(std::vector<std::uint8_t>(buffer.begin(), buffer.begin() + 60) == first);
    // A packet of exactly the buffer size is delivered, not counted.
    auto two = tunnel.receive(buffer, 1000);
    PHPVPN_REQUIRE_OK(two);
    PHPVPN_CHECK(two->status == TunnelReadStatus::packet);
    PHPVPN_CHECK_EQ(two->size, second.size());
    PHPVPN_CHECK(buffer == second);
    PHPVPN_CHECK_EQ(tunnel.oversizedReceiveCount(), std::uint64_t(0));
}

PHPVPN_TEST(FdTunnel, TimeoutIsEmptyAndOversizedPacketsAreCountedNotDelivered) {
    Pair pair = makePair();
    PHPVPN_CHECK(pair.tunnelSide.valid() && pair.peer.valid());
    if (!pair.tunnelSide) return;
    FdTunnel tunnel(pair.tunnelSide.get(), makeStopEvent());
    std::vector<std::uint8_t> buffer(100);

    const auto started = std::chrono::steady_clock::now();
    auto idle = tunnel.receive(buffer, 30);
    PHPVPN_REQUIRE_OK(idle);
    PHPVPN_CHECK(idle->status == TunnelReadStatus::empty);
    PHPVPN_CHECK(std::chrono::steady_clock::now() - started >= std::chrono::milliseconds(25));

    const auto large = packet(101, 3);
    const auto small = packet(20, 4);
    PHPVPN_CHECK_EQ(::write(pair.peer.get(), large.data(), large.size()), static_cast<ssize_t>(large.size()));
    PHPVPN_CHECK_EQ(::write(pair.peer.get(), small.data(), small.size()), static_cast<ssize_t>(small.size()));
    auto dropped = tunnel.receive(buffer, 1000);
    PHPVPN_REQUIRE_OK(dropped);
    PHPVPN_CHECK(dropped->status == TunnelReadStatus::empty);
    PHPVPN_CHECK_EQ(tunnel.oversizedReceiveCount(), std::uint64_t(1));
    auto next = tunnel.receive(buffer, 1000);
    PHPVPN_REQUIRE_OK(next);
    PHPVPN_CHECK(next->status == TunnelReadStatus::packet);
    PHPVPN_CHECK_EQ(next->size, small.size());
}

PHPVPN_TEST(FdTunnel, PacketLengthOutOfRangeIsRejectedBeforeWriting) {
    Pair pair = makePair();
    PHPVPN_CHECK(pair.tunnelSide.valid() && pair.peer.valid());
    if (!pair.tunnelSide) return;
    FdTunnel tunnel(pair.tunnelSide.get(), makeStopEvent());
    PHPVPN_REQUIRE_ERROR(tunnel.send({}), ErrorCode::wire_invalid_packet);
    const std::vector<std::uint8_t> huge(kMaxTunnelPacketBytes + 1, 0x45);
    PHPVPN_REQUIRE_ERROR(tunnel.send(huge), ErrorCode::wire_invalid_packet);
    std::uint8_t byte = 0;
    PHPVPN_CHECK_EQ(::read(pair.peer.get(), &byte, 1), ssize_t(-1));
}

PHPVPN_TEST(FdTunnel, SignalStopWakesABlockedReceiveAndCancelsSend) {
    Pair pair = makePair();
    PHPVPN_CHECK(pair.tunnelSide.valid() && pair.peer.valid());
    if (!pair.tunnelSide) return;
    FdTunnel tunnel(pair.tunnelSide.get(), makeStopEvent());
    std::vector<std::uint8_t> buffer(100);
    TunnelReadStatus status = TunnelReadStatus::packet;
    bool succeeded = false;
    std::thread worker([&] {
        auto result = tunnel.receive(buffer, -1);
        succeeded = result.ok();
        if (result.ok()) status = result->status;
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    tunnel.signalStop();
    worker.join();
    PHPVPN_CHECK(succeeded);
    PHPVPN_CHECK(status == TunnelReadStatus::stopped);
    PHPVPN_REQUIRE_ERROR(tunnel.send(packet(20, 5)), ErrorCode::cancelled);
    // detach after the stop leaves the descriptor alone.
    tunnel.detach();
    auto after = tunnel.receive(buffer, 0);
    PHPVPN_REQUIRE_OK(after);
    PHPVPN_CHECK(after->status == TunnelReadStatus::stopped);
    PHPVPN_CHECK(::fcntl(pair.tunnelSide.get(), F_GETFD) != -1);
}

PHPVPN_TEST(FdTunnel, FullQueueIsABoundedWaitThatTheStopEventInterrupts) {
    Pair pair = makePair();
    PHPVPN_CHECK(pair.tunnelSide.valid() && pair.peer.valid());
    if (!pair.tunnelSide) return;
    FdTunnel tunnel(pair.tunnelSide.get(), makeStopEvent());
    const auto filler = packet(1000, 6);
    int written = 0;
    while (::write(pair.tunnelSide.get(), filler.data(), filler.size()) == static_cast<ssize_t>(filler.size()))
        ++written;
    PHPVPN_CHECK(errno == EAGAIN || errno == EWOULDBLOCK);
    PHPVPN_CHECK(written > 0);

    // 200 rounds of at most 5 ms, never a busy loop without end.
    const auto started = std::chrono::steady_clock::now();
    PHPVPN_REQUIRE_ERROR(tunnel.send(filler), ErrorCode::busy);
    const auto elapsed = std::chrono::steady_clock::now() - started;
    PHPVPN_CHECK(elapsed >= std::chrono::milliseconds(500));
    PHPVPN_CHECK(elapsed < std::chrono::seconds(10));

    Status cancelled = Status::success();
    std::thread worker([&] { cancelled = tunnel.send(filler); });
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    tunnel.signalStop();
    worker.join();
    PHPVPN_REQUIRE_ERROR(cancelled, ErrorCode::cancelled);
}

}  // namespace phpvpn
