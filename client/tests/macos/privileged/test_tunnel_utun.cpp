// Privileged utun tests owned by WP04 (plan-macos-v001 step 7).
//
// Root only: the binary creates utun interfaces through the platform driver,
// assigns the address itself with SIOCAIFADDR, adds the VPN route with
// route(8) and exchanges packets with the kernel. CI runs it with sudo. Started
// without root every case returns early with a log line and passes, so the
// binary can also be run unprivileged. The kernel removes a utun together with
// its address and routes when the driver closes the socket, so every path
// cleans up through the driver; the route is also deleted explicitly.
#include "harness.h"

#include <arpa/inet.h>
#include <net/if.h>
#include <netinet/in.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/wait.h>
#include <unistd.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <future>
#include <span>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "platform/iface/tunnel.h"
#include "platform/macos/interface_identity.h"

namespace phpvpn {
namespace {

// A GUID of the tests only, never the installation GUID of a running service.
constexpr const char* kGuidText = "{5b0e9d6c-2f41-4c7a-8e93-1d6a7b4c2e58}";
constexpr const wchar_t* kGuid = L"{5b0e9d6c-2f41-4c7a-8e93-1d6a7b4c2e58}";
constexpr const char* kMarker = "phpvpn-owner:{5b0e9d6c-2f41-4c7a-8e93-1d6a7b4c2e58}";
constexpr const char* kLocal = "10.99.99.2";
constexpr const char* kPeer = "10.99.99.3";
constexpr const char* kPrefix = "10.99.99.0/24";
constexpr std::uint16_t kDiscardPort = 9;
constexpr std::uint32_t kRing = 4u * 1024 * 1024;

// ---- environment -------------------------------------------------------------

bool privileged() {
    if (::geteuid() == 0) return true;
    std::cout << "    skipped: needs root, run phpvpn-utun-tests with sudo\n";
    return false;
}

#define PHPVPN_REQUIRE_ROOT()          \
    do {                               \
        if (!privileged()) return;     \
    } while (false)

int run(const std::string& command) {
    const int status = std::system((command + " >/dev/null 2>&1").c_str());
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

std::string capture(const std::string& command) {
    std::string output;
    FILE* pipe = ::popen((command + " 2>/dev/null").c_str(), "r");
    if (pipe == nullptr) return output;
    char buffer[512];
    while (std::fgets(buffer, sizeof(buffer), pipe) != nullptr) output += buffer;
    ::pclose(pipe);
    return output;
}

bool contains(const std::string& text, const std::string& part) {
    return text.find(part) != std::string::npos;
}

Ipv4Address ip(const char* text) { return *Ipv4Address::parse(text); }

bool interfaceExists(const std::string& name) { return ::if_nametoindex(name.c_str()) != 0; }

sockaddr_in inetAddress(const char* text, std::uint16_t port = 0) {
    sockaddr_in address{};
    address.sin_len = static_cast<std::uint8_t>(sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    ::inet_pton(AF_INET, text, &address.sin_addr);
    return address;
}

// Socket for the test's own interface ioctls, closed on scope exit.
struct InetSocket {
    int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
    InetSocket() = default;
    InetSocket(const InetSocket&) = delete;
    InetSocket& operator=(const InetSocket&) = delete;
    ~InetSocket() {
        if (fd >= 0) ::close(fd);
    }
};

bool readInterface(unsigned long request, const std::string& name, ifreq& data) {
    InetSocket socket;
    if (socket.fd < 0 || name.size() >= IFNAMSIZ) return false;
    std::memcpy(data.ifr_name, name.c_str(), name.size() + 1);
    return ::ioctl(socket.fd, request, &data) == 0;
}

// What the network configuration does on macOS: the address as local and
// destination address of the point-to-point interface, the mask of the prefix.
bool assignAddress(const std::string& name) {
    InetSocket socket;
    if (socket.fd < 0 || name.size() >= IFNAMSIZ) return false;
    ifaliasreq request{};
    std::memcpy(request.ifra_name, name.c_str(), name.size() + 1);
    const sockaddr_in local = inetAddress(kLocal);
    const sockaddr_in mask = inetAddress("255.255.255.0");
    static_assert(sizeof(request.ifra_addr) == sizeof(sockaddr_in));
    std::memcpy(&request.ifra_addr, &local, sizeof(local));
    std::memcpy(&request.ifra_broadaddr, &local, sizeof(local));
    std::memcpy(&request.ifra_mask, &mask, sizeof(mask));
    return ::ioctl(socket.fd, SIOCAIFADDR, &request) == 0;
}

// ---- packets -----------------------------------------------------------------

std::uint16_t checksum(const std::uint8_t* data, std::size_t size) {
    std::uint32_t sum = 0;
    for (std::size_t index = 0; index + 1 < size; index += 2) {
        sum += static_cast<std::uint32_t>(data[index] << 8 | data[index + 1]);
    }
    if (size % 2 != 0) sum += static_cast<std::uint32_t>(data[size - 1] << 8);
    while (sum >> 16) sum = (sum & 0xFFFFu) + (sum >> 16);
    return static_cast<std::uint16_t>(~sum);
}

void put16(std::uint8_t* at, std::uint16_t value) {
    at[0] = static_cast<std::uint8_t>(value >> 8);
    at[1] = static_cast<std::uint8_t>(value);
}

std::uint16_t get16(const std::uint8_t* at) {
    return static_cast<std::uint16_t>(at[0] << 8 | at[1]);
}

// A complete IPv4 ICMP echo request with correct header and ICMP checksums.
std::vector<std::uint8_t> echoRequest(const char* source, const char* destination, std::uint16_t id) {
    std::vector<std::uint8_t> packet(20 + 8 + 16, 0x5A);
    packet[0] = 0x45;
    packet[1] = 0;
    put16(&packet[2], static_cast<std::uint16_t>(packet.size()));
    put16(&packet[4], id);
    packet[6] = packet[7] = 0;
    packet[8] = 64;
    packet[9] = IPPROTO_ICMP;
    packet[10] = packet[11] = 0;
    ip(source).toNetworkBytes(&packet[12]);
    ip(destination).toNetworkBytes(&packet[16]);
    put16(&packet[10], checksum(packet.data(), 20));
    packet[20] = 8;  // echo request
    packet[21] = 0;
    packet[22] = packet[23] = 0;
    put16(&packet[24], id);
    put16(&packet[26], 1);
    put16(&packet[22], checksum(packet.data() + 20, packet.size() - 20));
    return packet;
}

std::size_t headerLength(std::span<const std::uint8_t> packet) {
    return static_cast<std::size_t>(packet[0] & 0x0F) * 4;
}

bool isIpv4(std::span<const std::uint8_t> packet, std::uint8_t protocol, std::size_t minimumPayload) {
    if (packet.size() < 20 || (packet[0] >> 4) != 4 || packet[9] != protocol) return false;
    const std::size_t header = headerLength(packet);
    return header >= 20 && packet.size() >= header + minimumPayload;
}

bool isUdpToPeer(std::span<const std::uint8_t> packet) {
    if (!isIpv4(packet, IPPROTO_UDP, 8)) return false;
    const std::size_t header = headerLength(packet);
    return Ipv4Address::fromNetworkBytes(packet.data() + 16) == ip(kPeer) &&
           get16(packet.data() + header + 2) == kDiscardPort;
}

bool isEchoReplyToPeer(std::span<const std::uint8_t> packet, std::uint16_t id) {
    if (!isIpv4(packet, IPPROTO_ICMP, 8)) return false;
    const std::size_t header = headerLength(packet);
    return Ipv4Address::fromNetworkBytes(packet.data() + 12) == ip(kLocal) &&
           Ipv4Address::fromNetworkBytes(packet.data() + 16) == ip(kPeer) &&
           packet[header] == 0 && get16(packet.data() + header + 4) == id;
}

// Receives until `match` accepts a packet; packets the kernel sends into the
// interface on its own (multicast, mDNS) are skipped.
template <class Match>
std::vector<std::uint8_t> receiveMatching(Tunnel& tunnel, std::chrono::milliseconds limit, Match match) {
    std::vector<std::uint8_t> buffer(kMaxTunnelPacketBytes);
    const auto deadline = std::chrono::steady_clock::now() + limit;
    while (std::chrono::steady_clock::now() < deadline) {
        auto read = tunnel.receive(std::span<std::uint8_t>(buffer.data(), buffer.size()), 250);
        if (!read.ok() || read->status == TunnelReadStatus::stopped) break;
        if (read->status != TunnelReadStatus::packet) continue;
        const std::span<const std::uint8_t> packet(buffer.data(), read->size);
        if (match(packet)) return std::vector<std::uint8_t>(packet.begin(), packet.end());
    }
    return {};
}

// UDP datagram to the discard port of the peer, forced onto the utun.
bool sendUdpToPeer(const std::string& name, std::size_t payload) {
    InetSocket socket;
    if (socket.fd < 0) return false;
    const unsigned int index = ::if_nametoindex(name.c_str());
    if (index == 0 ||
        ::setsockopt(socket.fd, IPPROTO_IP, IP_BOUND_IF, &index, static_cast<socklen_t>(sizeof(index))) != 0) {
        return false;
    }
    const std::vector<std::uint8_t> data(payload, 0x42);
    const sockaddr_in peer = inetAddress(kPeer, kDiscardPort);
    return ::sendto(socket.fd, data.data(), data.size(), 0, reinterpret_cast<const sockaddr*>(&peer),
                    static_cast<socklen_t>(sizeof(peer))) == static_cast<ssize_t>(data.size());
}

// A created and marked utun, optionally with address, route and session, as
// the core would leave it; always released through the driver.
struct Fixture {
    std::unique_ptr<TunnelDriver> driver = createPlatformTunnelDriver();
    TunnelAdapterInfo info;
    std::string name;
    std::shared_ptr<Tunnel> tunnel;
    bool routeAdded = false;

    Status up(bool withSession) {
        PHPVPN_TRY(driver->loadAndVerify(L""));
        PHPVPN_TRY_ASSIGN(info, driver->createAdapter(L"phpvpn", L"utun", kGuid));
        PHPVPN_TRY_ASSIGN(name, interfaceNameForIdentity(info.interfaceLuid));
        if (withSession) PHPVPN_TRY_ASSIGN(tunnel, driver->startSession(kRing));
        return Status::success();
    }

    bool addAddressAndRoute() {
        if (!assignAddress(name)) return false;
        routeAdded = run("route -n add -net " + std::string(kPrefix) + " -interface " + name) == 0;
        return routeAdded;
    }

    ~Fixture() {
        driver->endSession();
        if (routeAdded) run("route -n delete -net " + std::string(kPrefix) + " -interface " + name);
        driver->closeAdapter();
    }
};

std::uint32_t ourTag() { return ownerTagForGuid(kGuidText); }

}  // namespace

// ---- 1. interface creation ---------------------------------------------------

PHPVPN_TEST(UtunTunnel, CreatesMarkedInterfaceWithMtuUpAndOwnedIdentity) {
    PHPVPN_REQUIRE_ROOT();
    auto driver = createPlatformTunnelDriver();
    PHPVPN_REQUIRE_OK(driver->loadAndVerify(L""));
    PHPVPN_CHECK_EQ(driver->runningDriverVersion(), 0u);
    PHPVPN_REQUIRE_ERROR(driver->openAdapter(L"phpvpn"), ErrorCode::not_supported);
    PHPVPN_REQUIRE_ERROR(driver->createAdapter(L"phpvpn", L"utun", L"not-a-guid"), ErrorCode::invalid_argument);

    auto info = driver->createAdapter(L"phpvpn", L"utun", kGuid);
    PHPVPN_REQUIRE_OK(info);
    PHPVPN_CHECK(info->created);
    PHPVPN_CHECK_EQ(info->adapterGuid, std::string(kGuidText));
    auto resolved = interfaceNameForIdentity(info->interfaceLuid);
    PHPVPN_REQUIRE_OK(resolved);
    const std::string name = resolved.value();
    std::cout << "    created " << name << "\n";
    PHPVPN_CHECK(name.size() > 4 && name.compare(0, 4, "utun") == 0);
    const int index = static_cast<int>(::if_nametoindex(name.c_str()));
    PHPVPN_CHECK(index > 0);
    PHPVPN_CHECK_EQ(info->interfaceLuid, composeInterfaceIdentity(ourTag(), index));
    PHPVPN_CHECK_NE(identityOwnerTag(info->interfaceLuid), 0u);

    auto byName = interfaceIdentityForName(name);
    PHPVPN_REQUIRE_OK(byName);
    PHPVPN_CHECK_EQ(byName.value(), info->interfaceLuid);
    auto carried = interfaceCarriesIdentity(info->interfaceLuid);
    PHPVPN_REQUIRE_OK(carried);
    PHPVPN_CHECK(carried.value());
    // The bare ifindex (a foreign identity) is not this interface.
    carried = interfaceCarriesIdentity(static_cast<std::uint64_t>(index));
    PHPVPN_REQUIRE_OK(carried);
    PHPVPN_CHECK(!carried.value());

    auto description = readInterfaceDescription(name);
    PHPVPN_REQUIRE_OK(description);
    PHPVPN_CHECK_EQ(description.value(), std::string(kMarker));
    ifreq mtu{};
    PHPVPN_CHECK(readInterface(SIOCGIFMTU, name, mtu));
    PHPVPN_CHECK_EQ(mtu.ifr_mtu, 1500);
    ifreq flags{};
    PHPVPN_CHECK(readInterface(SIOCGIFFLAGS, name, flags));
    PHPVPN_CHECK((flags.ifr_flags & IFF_UP) != 0);
    // ifconfig as an independent view: up, MTU 1500, no IPv6 address at all.
    const std::string shown = capture("ifconfig " + name);
    PHPVPN_CHECK(contains(shown, "<UP"));
    PHPVPN_CHECK(contains(shown, "mtu 1500"));
    PHPVPN_CHECK(!contains(shown, "inet6"));

    PHPVPN_REQUIRE_ERROR(driver->createAdapter(L"phpvpn", L"utun", kGuid), ErrorCode::state_conflict);

    driver->closeAdapter();
    PHPVPN_CHECK(!interfaceExists(name));
    carried = interfaceCarriesIdentity(info->interfaceLuid);
    PHPVPN_REQUIRE_OK(carried);
    PHPVPN_CHECK(!carried.value());
    PHPVPN_REQUIRE_ERROR(driver->startSession(kRing), ErrorCode::tunnel_adapter_failed);

    // The same driver creates a fresh interface after the first one is gone.
    auto again = driver->createAdapter(L"phpvpn", L"utun", kGuid);
    PHPVPN_REQUIRE_OK(again);
    carried = interfaceCarriesIdentity(again->interfaceLuid);
    PHPVPN_REQUIRE_OK(carried);
    PHPVPN_CHECK(carried.value());
    driver->closeAdapter();
}

// ---- 2. packets --------------------------------------------------------------

PHPVPN_TEST(UtunTunnel, SendsIntoTheKernelAndReceivesRoutedPackets) {
    PHPVPN_REQUIRE_ROOT();
    Fixture fixture;
    PHPVPN_REQUIRE_OK(fixture.up(true));
    PHPVPN_CHECK(fixture.addAddressAndRoute());
    if (!fixture.routeAdded) return;
    Tunnel& tunnel = *fixture.tunnel;

    const std::vector<std::uint8_t> empty;
    PHPVPN_REQUIRE_ERROR(tunnel.send(empty), ErrorCode::wire_invalid_packet);

    // A valid echo request from the peer to the local address; a correct
    // header checksums to zero.
    const std::vector<std::uint8_t> request = echoRequest(kPeer, kLocal, 0x4242);
    PHPVPN_CHECK_EQ(checksum(request.data(), 20), 0);
    PHPVPN_REQUIRE_OK(tunnel.send(request));
    // The kernel's reply proves that the family header was understood. An
    // application firewall in stealth mode drops echo requests, so the reply
    // is only reported.
    const auto reply = receiveMatching(tunnel, std::chrono::milliseconds(2000),
                                       [](std::span<const std::uint8_t> packet) {
                                           return isEchoReplyToPeer(packet, 0x4242);
                                       });
    std::cout << (reply.empty() ? "    note: no echo reply to the injected request (ICMP filtered?)\n"
                                : "    the kernel answered the injected echo request\n");

    // A datagram the kernel routes into the interface reaches receive.
    PHPVPN_CHECK(sendUdpToPeer(fixture.name, 32));
    const auto datagram = receiveMatching(tunnel, std::chrono::milliseconds(2000), isUdpToPeer);
    PHPVPN_CHECK(!datagram.empty());
    if (!datagram.empty()) {
        PHPVPN_CHECK(Ipv4Address::fromNetworkBytes(datagram.data() + 12) == ip(kLocal));
        PHPVPN_CHECK_EQ(datagram.size(), std::size_t{20 + 8 + 32});
    }
    PHPVPN_CHECK_EQ(tunnel.oversizedReceiveCount(), 0u);
}

PHPVPN_TEST(UtunTunnel, OversizedPacketIsCountedAndNotDelivered) {
    PHPVPN_REQUIRE_ROOT();
    Fixture fixture;
    PHPVPN_REQUIRE_OK(fixture.up(true));
    PHPVPN_CHECK(fixture.addAddressAndRoute());
    if (!fixture.routeAdded) return;
    Tunnel& tunnel = *fixture.tunnel;

    // 428 bytes on the wire against a 64 byte buffer.
    PHPVPN_CHECK(sendUdpToPeer(fixture.name, 400));
    std::vector<std::uint8_t> small(64);
    bool ownDelivered = false;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (tunnel.oversizedReceiveCount() == 0 && std::chrono::steady_clock::now() < deadline) {
        auto read = tunnel.receive(std::span<std::uint8_t>(small.data(), small.size()), 250);
        PHPVPN_REQUIRE_OK(read);
        if (read->status == TunnelReadStatus::packet &&
            isUdpToPeer(std::span<const std::uint8_t>(small.data(), read->size))) {
            ownDelivered = true;
        }
    }
    PHPVPN_CHECK(tunnel.oversizedReceiveCount() >= 1u);
    PHPVPN_CHECK(!ownDelivered);

    // The next packet that fits is delivered whole.
    PHPVPN_CHECK(sendUdpToPeer(fixture.name, 4));
    const auto fitting = receiveMatching(tunnel, std::chrono::milliseconds(2000),
                                         [](std::span<const std::uint8_t> packet) {
                                             return isUdpToPeer(packet) && packet.size() == 20u + 8u + 4u;
                                         });
    PHPVPN_CHECK(!fitting.empty());
}

// ---- 3. stop and teardown ----------------------------------------------------

// Receives until the stop arrives; packets the kernel sends on its own are
// skipped. Returns whether the stop arrived and how long it took.
static std::pair<bool, std::chrono::steady_clock::duration> waitForStop(Tunnel& tunnel) {
    std::vector<std::uint8_t> buffer(kMaxTunnelPacketBytes);
    const auto started = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - started < std::chrono::seconds(10)) {
        auto read = tunnel.receive(std::span<std::uint8_t>(buffer.data(), buffer.size()), 20000);
        if (!read.ok()) break;
        if (read->status == TunnelReadStatus::stopped) {
            return {true, std::chrono::steady_clock::now() - started};
        }
    }
    return {false, std::chrono::steady_clock::now() - started};
}

PHPVPN_TEST(UtunTunnel, StopWakesBlockedReceiveAndEndSessionKeepsTheInterface) {
    PHPVPN_REQUIRE_ROOT();
    Fixture fixture;
    PHPVPN_REQUIRE_OK(fixture.up(true));
    std::shared_ptr<Tunnel> tunnel = fixture.tunnel;

    auto blocked = std::async(std::launch::async, [tunnel] { return waitForStop(*tunnel); });
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    tunnel->signalStop();
    PHPVPN_CHECK(blocked.wait_for(std::chrono::seconds(3)) == std::future_status::ready);
    const auto result = blocked.get();
    PHPVPN_CHECK(result.first);
    PHPVPN_CHECK(result.second < std::chrono::seconds(3));

    const std::vector<std::uint8_t> packet = echoRequest(kPeer, kLocal, 1);
    PHPVPN_REQUIRE_ERROR(tunnel->send(packet), ErrorCode::cancelled);

    // endSession wakes a receive of a fresh session from another thread, and a
    // worker that still holds the detached tunnel only ever sees `stopped`.
    PHPVPN_REQUIRE_ERROR(fixture.driver->startSession(kRing), ErrorCode::state_conflict);
    fixture.driver->endSession();
    auto session = fixture.driver->startSession(kRing);
    PHPVPN_REQUIRE_OK(session);
    std::shared_ptr<Tunnel> second = session.value();
    auto waiting = std::async(std::launch::async, [second] { return waitForStop(*second).first; });
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    fixture.driver->endSession();
    PHPVPN_CHECK(waiting.wait_for(std::chrono::seconds(3)) == std::future_status::ready);
    PHPVPN_CHECK(waiting.get());
    std::vector<std::uint8_t> buffer(64);
    auto late = second->receive(std::span<std::uint8_t>(buffer.data(), buffer.size()), 10);
    PHPVPN_REQUIRE_OK(late);
    PHPVPN_CHECK(late->status == TunnelReadStatus::stopped);
    PHPVPN_REQUIRE_ERROR(second->send(packet), ErrorCode::cancelled);

    // The interface stays until closeAdapter removes it.
    PHPVPN_CHECK(interfaceExists(fixture.name));
    fixture.driver->closeAdapter();
    PHPVPN_CHECK(!interfaceExists(fixture.name));
}

PHPVPN_TEST(UtunTunnel, ClosingTheSocketRemovesInterfaceAddressAndRoute) {
    PHPVPN_REQUIRE_ROOT();
    Fixture fixture;
    PHPVPN_REQUIRE_OK(fixture.up(false));
    PHPVPN_CHECK(fixture.addAddressAndRoute());
    if (!fixture.routeAdded) return;
    const std::string name = fixture.name;
    PHPVPN_CHECK(contains(capture("ifconfig " + name), "inet 10.99.99.2"));
    PHPVPN_CHECK(contains(capture("netstat -rn -f inet"), "10.99.99"));

    // Like a dying service: neither the route nor the address is removed, only
    // the socket closes. The kernel must take both with the interface, or the
    // next start would find a foreign route into the VPN prefix.
    fixture.routeAdded = false;
    fixture.driver->closeAdapter();
    PHPVPN_CHECK(!interfaceExists(name));
    // The interface leaves the list synchronously, its addresses and routes go
    // with the final detach on a kernel thread a moment later.
    bool routeLeft = true;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (routeLeft && std::chrono::steady_clock::now() < deadline) {
        routeLeft = contains(capture("netstat -rn -f inet"), "10.99.99");
        if (routeLeft) std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    PHPVPN_CHECK(!routeLeft);
    if (routeLeft) run("route -n delete -net " + std::string(kPrefix));
}

}  // namespace phpvpn
