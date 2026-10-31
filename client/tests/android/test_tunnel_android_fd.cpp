// T-N02 (plan-android-v001 WP03): the production packet session of the Android
// driver, FdTunnel over the established descriptor, without a VpnService. A
// blocking AF_UNIX SOCK_SEQPACKET pair stands in for the descriptor of
// Builder.establish(): it keeps the packet boundaries like a TUN device.
// Linux and Android only (eventfd, the FdTunnel of platform/posix).
#include "harness.h"

#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>

#include <filesystem>
#include <memory>
#include <vector>

#include "core/config/config.h"
#include "core/runtime/tunnel_session.h"
#include "fake_vpn_hosts.h"
#include "platform/android/netconfig_android.h"
#include "platform/android/tunnel_android.h"
#include "platform/posix/fd.h"

namespace phpvpn {
namespace {

struct Pair {
    int tunnelSide = -1;  // handed to the context, which closes it
    UniqueFd peer;
};

Pair blockingPair() {
    int fds[2] = {-1, -1};
    if (::socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, fds) != 0) return {};
    return Pair{fds[0], UniqueFd(fds[1])};
}

std::vector<std::uint8_t> bytes(std::size_t size, std::uint8_t seed) {
    std::vector<std::uint8_t> data(size);
    for (std::size_t index = 0; index < size; ++index) data[index] = static_cast<std::uint8_t>(seed + index);
    return data;
}

}  // namespace

PHPVPN_TEST(AndroidTunnel, DescriptorSessionIsNonBlockingAndKeepsPacketBoundaries) {
    Pair pair = blockingPair();
    PHPVPN_CHECK(pair.tunnelSide >= 0 && pair.peer.valid());
    if (pair.tunnelSide < 0) return;
    UniqueFd owned(pair.tunnelSide);
    // detachFd() hands over a blocking descriptor; the session needs O_NONBLOCK.
    PHPVPN_CHECK((::fcntl(owned.get(), F_GETFL) & O_NONBLOCK) == 0);

    auto session = makeFdTunnelSession(owned.get());
    PHPVPN_REQUIRE_OK(session);
    PHPVPN_CHECK((::fcntl(owned.get(), F_GETFL) & O_NONBLOCK) != 0);
    PHPVPN_CHECK((::fcntl(owned.get(), F_GETFD) & FD_CLOEXEC) != 0);

    const auto outbound = bytes(1400, 3);
    PHPVPN_REQUIRE_OK(session->tunnel->send(outbound));
    std::vector<std::uint8_t> received(2000);
    PHPVPN_CHECK_EQ(::read(pair.peer.get(), received.data(), received.size()), static_cast<ssize_t>(outbound.size()));

    const auto inbound = bytes(60, 9);
    PHPVPN_CHECK_EQ(::write(pair.peer.get(), inbound.data(), inbound.size()), static_cast<ssize_t>(inbound.size()));
    std::vector<std::uint8_t> buffer(1500);
    const auto read = session->tunnel->receive(buffer, 1000);
    PHPVPN_REQUIRE_OK(read);
    PHPVPN_CHECK(read->status == TunnelReadStatus::packet);
    PHPVPN_CHECK_EQ(read->size, inbound.size());

    // detach wakes and stops every user before the descriptor closes.
    session->detach();
    const auto stopped = session->tunnel->receive(buffer, 1000);
    PHPVPN_REQUIRE_OK(stopped);
    PHPVPN_CHECK(stopped->status == TunnelReadStatus::stopped);
    PHPVPN_REQUIRE_ERROR(makeFdTunnelSession(-1), ErrorCode::tunnel_session_failed);
}

// The whole core path of plan step "Životní cyklus" 2 with the production
// session factory: TunnelSession over AndroidNetConfig and the driver, packets
// in both directions, then the release of the descriptor.
PHPVPN_TEST(AndroidTunnel, TunnelSessionMovesPacketsOverTheEstablishedDescriptor) {
    Pair pair = blockingPair();
    PHPVPN_CHECK(pair.tunnelSide >= 0);
    if (pair.tunnelSide < 0) return;
    const int handedOut = pair.tunnelSide;

    auto context = std::make_shared<AndroidVpnContext>(test::testContextOptions());
    context->setNetworkHost(std::make_shared<test::FakeNetworkHost>(
        test::snapshotJson(test::emulatorWifi().handle, {test::emulatorWifi()})));
    auto tunnelHost = std::make_shared<test::FakeTunnelHost>([handedOut] { return handedOut; });
    context->setTunnelHost(tunnelHost);
    auto netConfig = createAndroidNetConfig(context, nullptr);
    auto driver = createAndroidTunnelDriver(context, makeFdTunnelSession);

    const auto directory = std::filesystem::temp_directory_path() / "phpvpn-android-fd-session";
    std::filesystem::create_directories(directory);
    TunnelSessionSettings settings;
    settings.journalPath = directory / "tunnel-journal.json";
    auto config = std::make_shared<ClientConfig>();
    config->vpnCidr = *Ipv4Cidr::parse("10.99.99.0/24");
    config->virtualIp = *Ipv4Address::parse("10.99.99.2");

    {
        TunnelSession session(*driver, *netConfig);
        PHPVPN_REQUIRE_OK(session.start(*config, settings));
        const auto outbound = bytes(84, 1);
        PHPVPN_REQUIRE_OK(session.tunnel()->send(outbound));
        std::vector<std::uint8_t> received(2000);
        PHPVPN_CHECK_EQ(::read(pair.peer.get(), received.data(), received.size()), static_cast<ssize_t>(outbound.size()));
        const auto inbound = bytes(84, 2);
        PHPVPN_CHECK_EQ(::write(pair.peer.get(), inbound.data(), inbound.size()), static_cast<ssize_t>(inbound.size()));
        std::vector<std::uint8_t> buffer(1500);
        const auto read = session.tunnel()->receive(buffer, 1000);
        PHPVPN_REQUIRE_OK(read);
        PHPVPN_CHECK(read->status == TunnelReadStatus::packet);
        PHPVPN_REQUIRE_OK(session.stop());
    }
    // The context closed the descriptor: the peer sees the end of the stream.
    PHPVPN_CHECK(!test::descriptorIsOpen(handedOut));
    std::uint8_t byte = 0;
    PHPVPN_CHECK_EQ(::read(pair.peer.get(), &byte, 1), static_cast<ssize_t>(0));
    std::error_code code;
    std::filesystem::remove_all(directory, code);
}

}  // namespace phpvpn
