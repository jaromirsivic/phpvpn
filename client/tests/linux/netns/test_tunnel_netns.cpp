// Privileged network namespace tests owned by WP04 (plan-v001 section 9.4).
//
// The binary creates TUN and dummy interfaces, addresses, routes and a bind
// mount over /etc/resolv.conf. It refuses to run unless it is root inside a
// private network and mount namespace, which run-netns-tests.sh provides; see
// README.md next to this file. Helper commands use iproute2 (`ip`).
#include "harness.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <net/if.h>
#include <netinet/ip.h>
#include <netinet/ip_icmp.h>
#include <poll.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <unistd.h>

#include <linux/if_tun.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <future>
#include <sstream>
#include <thread>

#include "core/config/config.h"
#include "core/runtime/tunnel_session.h"
#include "platform/iface/netconfig.h"
#include "platform/iface/tunnel.h"
#include "platform/linux/fd.h"
#include "platform/linux/interface_identity.h"
#include "platform/linux/netconfig_netlink.h"

namespace phpvpn {
namespace {

constexpr const char* kGuidText = "{3f6c2a91-5b7e-4d0a-9c1e-8a2b7d4e6f10}";
constexpr const wchar_t* kGuid = L"{3f6c2a91-5b7e-4d0a-9c1e-8a2b7d4e6f10}";
constexpr const char* kMarker = "phpvpn-owner:{3f6c2a91-5b7e-4d0a-9c1e-8a2b7d4e6f10}";
constexpr const char* kOtherMarker = "phpvpn-owner:{00000000-0000-0000-0000-000000000001}";

// ---- environment -------------------------------------------------------------

ino_t namespaceInode(const char* path) {
    struct stat status{};
    return ::stat(path, &status) == 0 ? status.st_ino : 0;
}

// Root, a network namespace and a mount namespace different from PID 1, and the
// marker of the runner script. Anything else would touch the host network.
bool isolated() {
    static const bool value = [] {
        const char* marker = std::getenv("PHPVPN_NETNS_ISOLATED");
        const bool ok = ::geteuid() == 0 && marker != nullptr && std::string(marker) == "1" &&
                        namespaceInode("/proc/self/ns/net") != 0 &&
                        namespaceInode("/proc/self/ns/net") != namespaceInode("/proc/1/ns/net") &&
                        namespaceInode("/proc/self/ns/mnt") != namespaceInode("/proc/1/ns/mnt");
        if (!ok) {
            std::cout << "    refusing to run: needs root in a private network and mount namespace,"
                         " use tests/linux/netns/run-netns-tests.sh\n";
        }
        return ok;
    }();
    return value;
}

#define PHPVPN_REQUIRE_ISOLATED()      \
    do {                               \
        PHPVPN_CHECK(isolated());      \
        if (!isolated()) return;       \
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

std::string readSys(const std::string& interfaceName, const char* attribute) {
    std::ifstream stream("/sys/class/net/" + interfaceName + "/" + attribute);
    std::string value;
    std::getline(stream, value);
    return value;
}

bool linkExists(const char* name) { return ::if_nametoindex(name) != 0; }

// Leaves the namespace as the runner created it: only lo.
void resetNamespace() {
    for (const char* name : {"phpvpn", "dummy0", "foreign0"}) {
        run(std::string("ip link del ") + name);
    }
    run("ip route flush table main");
}

Ipv4Address ip(const char* text) { return *Ipv4Address::parse(text); }

std::shared_ptr<ClientConfig> vpnConfig() {
    auto config = std::make_shared<ClientConfig>();
    config->vpnCidr = *Ipv4Cidr::parse("10.99.99.0/24");
    config->virtualIp = ip("10.99.99.2");
    return config;
}

struct TemporaryDirectory {
    std::filesystem::path path;
    TemporaryDirectory() {
        path = std::filesystem::temp_directory_path() /
               ("phpvpn-netns-" + std::to_string(::getpid()) + "-" +
                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(path);
    }
    ~TemporaryDirectory() {
        std::error_code code;
        std::filesystem::remove_all(path, code);
    }
};

TunnelSessionSettings sessionSettings(const TemporaryDirectory& directory) {
    TunnelSessionSettings settings;
    settings.journalPath = directory.path / "tunnel-journal.json";
    settings.recoveryWaitMs = 3000;
    return settings;
}

std::uint32_t ourTag() { return ownerTagForGuid(kGuidText); }

// A created, marked and configured tunnel with address and route, as the
// session would leave it, with the session started.
struct Fixture {
    std::unique_ptr<TunnelDriver> driver = createPlatformTunnelDriver();
    std::unique_ptr<NetConfig> net = createPlatformNetConfig();
    std::uint64_t identity = 0;
    std::shared_ptr<Tunnel> tunnel;

    Status up(bool withSession = true) {
        PHPVPN_TRY(driver->loadAndVerify(L""));
        TunnelAdapterInfo info;
        PHPVPN_TRY_ASSIGN(info, driver->createAdapter(L"phpvpn", L"phpvpn", kGuid));
        identity = info.interfaceLuid;
        PHPVPN_TRY(net->addUnicastAddress({identity, ip("10.99.99.2"), 24}));
        PHPVPN_TRY(net->addRoute({identity, *Ipv4Cidr::parse("10.99.99.0/24"), Ipv4Address(0), 5}));
        if (withSession) PHPVPN_TRY_ASSIGN(tunnel, driver->startSession(4u * 1024 * 1024));
        return Status::success();
    }
    ~Fixture() {
        driver->endSession();
        driver->closeAdapter();
    }
};

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

void putChecksum(std::uint8_t* at, std::uint16_t value) {
    at[0] = static_cast<std::uint8_t>(value >> 8);
    at[1] = static_cast<std::uint8_t>(value);
}

UniqueFd icmpSocket() {
    UniqueFd fd(::socket(AF_INET, SOCK_RAW | SOCK_CLOEXEC, IPPROTO_ICMP));
    timeval timeout{2, 0};
    ::setsockopt(fd.get(), SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    return fd;
}

bool sendEcho(int socket, const char* destination, std::uint16_t id, std::size_t payload) {
    std::vector<std::uint8_t> packet(8 + payload, 0x5A);
    packet[0] = ICMP_ECHO;
    packet[1] = 0;
    packet[2] = packet[3] = 0;
    packet[4] = static_cast<std::uint8_t>(id >> 8);
    packet[5] = static_cast<std::uint8_t>(id);
    packet[6] = 0;
    packet[7] = 1;
    putChecksum(&packet[2], checksum(packet.data(), packet.size()));
    sockaddr_in address{};
    address.sin_family = AF_INET;
    ::inet_pton(AF_INET, destination, &address.sin_addr);
    return ::sendto(socket, packet.data(), packet.size(), 0,
                    reinterpret_cast<const sockaddr*>(&address), sizeof(address)) ==
           static_cast<ssize_t>(packet.size());
}

// Receives from the tunnel until an ICMP echo request to `destination` arrives.
std::vector<std::uint8_t> receiveEcho(Tunnel& tunnel, const char* destination) {
    std::vector<std::uint8_t> buffer(kMaxTunnelPacketBytes);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    const Ipv4Address wanted = ip(destination);
    while (std::chrono::steady_clock::now() < deadline) {
        auto read = tunnel.receive(std::span<std::uint8_t>(buffer.data(), buffer.size()), 250);
        if (!read.ok() || read->status == TunnelReadStatus::stopped) break;
        if (read->status != TunnelReadStatus::packet || read->size < 28) continue;
        const std::uint8_t* packet = buffer.data();
        if ((packet[0] >> 4) != 4 || packet[9] != IPPROTO_ICMP) continue;
        if (Ipv4Address::fromNetworkBytes(packet + 16) != wanted) continue;
        const std::size_t headerLength = static_cast<std::size_t>(packet[0] & 0x0F) * 4;
        if (packet[headerLength] != ICMP_ECHO) continue;
        return std::vector<std::uint8_t>(buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(read->size));
    }
    return {};
}

std::vector<std::uint8_t> makeEchoReply(std::vector<std::uint8_t> request) {
    const std::size_t headerLength = static_cast<std::size_t>(request[0] & 0x0F) * 4;
    std::uint8_t source[4];
    std::memcpy(source, &request[12], 4);
    std::memcpy(&request[12], &request[16], 4);
    std::memcpy(&request[16], source, 4);
    request[8] = 64;
    request[10] = request[11] = 0;
    putChecksum(&request[10], checksum(request.data(), headerLength));
    request[headerLength] = ICMP_ECHOREPLY;
    request[headerLength + 2] = request[headerLength + 3] = 0;
    putChecksum(&request[headerLength + 2],
                checksum(request.data() + headerLength, request.size() - headerLength));
    return request;
}

bool socketReceivesReply(int socket, const char* source, std::uint16_t id) {
    std::uint8_t buffer[2048];
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (std::chrono::steady_clock::now() < deadline) {
        const ssize_t count = ::recv(socket, buffer, sizeof(buffer), 0);
        if (count < 28) {
            if (count < 0 && errno != EINTR) return false;
            continue;
        }
        const std::size_t headerLength = static_cast<std::size_t>(buffer[0] & 0x0F) * 4;
        if (Ipv4Address::fromNetworkBytes(buffer + 12) != ip(source)) continue;
        if (buffer[headerLength] != ICMP_ECHOREPLY) continue;
        const std::uint16_t replyId =
            static_cast<std::uint16_t>(buffer[headerLength + 4] << 8 | buffer[headerLength + 5]);
        if (replyId == id) return true;
    }
    return false;
}

bool hasAddress(const NetworkSnapshot& snapshot, std::uint64_t identity, const char* address,
                int prefix) {
    for (const auto& entry : snapshot.addresses) {
        if (entry.interfaceLuid == identity && entry.address == ip(address) &&
            entry.prefixLength == prefix) {
            return true;
        }
    }
    return false;
}

bool hasRoute(const NetworkSnapshot& snapshot, std::uint64_t identity, const char* prefix,
              std::uint32_t metric) {
    for (const auto& entry : snapshot.routes) {
        if (entry.interfaceLuid == identity && entry.destination.toString() == prefix &&
            entry.metric == metric && entry.nextHop.value() == 0) {
            return true;
        }
    }
    return false;
}

bool anyRowOf(const NetworkSnapshot& snapshot, std::uint64_t identity) {
    for (const auto& entry : snapshot.addresses) if (entry.interfaceLuid == identity) return true;
    for (const auto& entry : snapshot.routes) if (entry.interfaceLuid == identity) return true;
    return false;
}

// Attaches this process to an existing persistent TUN device of that name.
UniqueFd attachToPersistent(const char* name) {
    UniqueFd fd(::open("/dev/net/tun", O_RDWR | O_CLOEXEC));
    ifreq request{};
    std::strncpy(request.ifr_name, name, IFNAMSIZ - 1);
    request.ifr_flags = IFF_TUN | IFF_NO_PI;
    if (!fd || ::ioctl(fd.get(), TUNSETIFF, &request) != 0) return UniqueFd();
    return fd;
}

bool waitUntilGone(const char* name, std::chrono::milliseconds limit) {
    const auto deadline = std::chrono::steady_clock::now() + limit;
    while (linkExists(name)) {
        if (std::chrono::steady_clock::now() >= deadline) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return true;
}

}  // namespace

// ---- 1. interface creation ---------------------------------------------------

PHPVPN_TEST(NetnsTunnel, CreatesMarkedInterfaceWithMtuWithoutIpv6AndUp) {
    PHPVPN_REQUIRE_ISOLATED();
    resetNamespace();
    auto driver = createPlatformTunnelDriver();
    PHPVPN_REQUIRE_OK(driver->loadAndVerify(L""));
    PHPVPN_CHECK_EQ(driver->runningDriverVersion(), 0u);
    PHPVPN_REQUIRE_ERROR(driver->openAdapter(L"phpvpn"), ErrorCode::not_supported);

    auto info = driver->createAdapter(L"phpvpn", L"phpvpn", kGuid);
    PHPVPN_REQUIRE_OK(info);
    PHPVPN_CHECK(info->created);
    PHPVPN_CHECK_EQ(info->adapterGuid, std::string(kGuidText));
    const int index = static_cast<int>(::if_nametoindex("phpvpn"));
    PHPVPN_CHECK(index > 0);
    PHPVPN_CHECK_EQ(info->interfaceLuid, composeInterfaceIdentity(ourTag(), index));

    // Give addrconf the time it would need to add a link-local address.
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    PHPVPN_CHECK_EQ(readSys("phpvpn", "ifalias"), std::string(kMarker));
    PHPVPN_CHECK_EQ(readSys("phpvpn", "mtu"), std::string("1500"));
    const unsigned long flags = std::strtoul(readSys("phpvpn", "flags").c_str(), nullptr, 16);
    PHPVPN_CHECK((flags & IFF_UP) != 0);
    const std::string details = capture("ip -d link show dev phpvpn");
    PHPVPN_CHECK(contains(details, "tun type tun"));
    PHPVPN_CHECK(contains(details, "persist off"));
    PHPVPN_CHECK(contains(details, "addrgenmode none"));
    PHPVPN_CHECK(capture("ip -6 addr show dev phpvpn").empty());
    PHPVPN_CHECK(!contains(capture("cat /proc/net/if_inet6"), "phpvpn"));

    auto net = createPlatformNetConfig();
    auto exists = net->interfaceExists(info->interfaceLuid);
    PHPVPN_REQUIRE_OK(exists);
    PHPVPN_CHECK(exists.value());
    auto byAlias = net->interfaceLuidForAlias(L"phpvpn");
    PHPVPN_REQUIRE_OK(byAlias);
    PHPVPN_CHECK_EQ(byAlias.value(), info->interfaceLuid);

    PHPVPN_REQUIRE_ERROR(driver->createAdapter(L"phpvpn", L"phpvpn", kGuid), ErrorCode::state_conflict);
    driver->closeAdapter();
    PHPVPN_CHECK(!linkExists("phpvpn"));
    exists = net->interfaceExists(info->interfaceLuid);
    PHPVPN_REQUIRE_OK(exists);
    PHPVPN_CHECK(!exists.value());

    // A name that would be a TUNSETIFF pattern never reaches the kernel.
    PHPVPN_REQUIRE_ERROR(driver->createAdapter(L"phpvpn%d", L"phpvpn", kGuid), ErrorCode::invalid_argument);
    PHPVPN_REQUIRE_ERROR(driver->createAdapter(L"phpvpn", L"phpvpn", L"not-a-guid"), ErrorCode::invalid_argument);
}

// ---- 2. address and route ----------------------------------------------------

PHPVPN_TEST(NetnsTunnel, AddressWithoutPrefixRouteAndRouteWithMetricFive) {
    PHPVPN_REQUIRE_ISOLATED();
    resetNamespace();
    Fixture fixture;
    PHPVPN_REQUIRE_OK(fixture.up(false));

    const std::string addresses = capture("ip -4 addr show dev phpvpn");
    PHPVPN_CHECK(contains(addresses, "10.99.99.2/24"));
    PHPVPN_CHECK(contains(addresses, "noprefixroute"));
    const std::string routes = capture("ip -4 route show table all dev phpvpn");
    PHPVPN_CHECK(contains(routes, "10.99.99.0/24 proto static scope link metric 5"));
    // Exactly one route into the prefix: no kernel prefix route with metric 0.
    PHPVPN_CHECK(!contains(capture("ip -4 route show table main proto kernel"), "10.99.99.0/24"));

    auto snapshot = fixture.net->snapshot();
    PHPVPN_REQUIRE_OK(snapshot);
    PHPVPN_CHECK(hasAddress(snapshot.value(), fixture.identity, "10.99.99.2", 24));
    PHPVPN_CHECK(hasRoute(snapshot.value(), fixture.identity, "10.99.99.0/24", 5));
    // Local and broadcast table entries are not part of the snapshot.
    for (const auto& route : snapshot->routes) {
        PHPVPN_CHECK(route.destination.toString() != "10.99.99.255/32");
    }

    // The exact existing row is success, like ERROR_OBJECT_ALREADY_EXISTS.
    PHPVPN_REQUIRE_OK(fixture.net->addUnicastAddress({fixture.identity, ip("10.99.99.2"), 24}));
    PHPVPN_REQUIRE_OK(fixture.net->addRoute({fixture.identity, *Ipv4Cidr::parse("10.99.99.0/24"), Ipv4Address(0), 5}));
    // A foreign (untagged) identity is never changed.
    const std::uint64_t foreign = static_cast<std::uint64_t>(identityIfindex(fixture.identity));
    PHPVPN_REQUIRE_ERROR(fixture.net->addUnicastAddress({foreign, ip("10.99.99.3"), 24}),
                         ErrorCode::netconfig_apply_failed);

    const RouteEntry route{fixture.identity, *Ipv4Cidr::parse("10.99.99.0/24"), Ipv4Address(0), 5};
    PHPVPN_REQUIRE_OK(fixture.net->removeRoute(route));
    PHPVPN_REQUIRE_OK(fixture.net->removeRoute(route));  // not found is success
    PHPVPN_REQUIRE_OK(fixture.net->removeUnicastAddress({fixture.identity, ip("10.99.99.2"), 24}));
    PHPVPN_REQUIRE_OK(fixture.net->removeUnicastAddress({fixture.identity, ip("10.99.99.2"), 24}));
    snapshot = fixture.net->snapshot();
    PHPVPN_REQUIRE_OK(snapshot);
    PHPVPN_CHECK(!anyRowOf(snapshot.value(), fixture.identity));

    // Removal after the interface vanished is success, adding is an error.
    fixture.driver->closeAdapter();
    PHPVPN_REQUIRE_OK(fixture.net->removeRoute(route));
    PHPVPN_REQUIRE_ERROR(fixture.net->addRoute(route), ErrorCode::netconfig_apply_failed);
}

// ---- 3. packets --------------------------------------------------------------

PHPVPN_TEST(NetnsTunnel, PingToPeerReachesReceiveAndWrittenReplyReachesSocket) {
    PHPVPN_REQUIRE_ISOLATED();
    resetNamespace();
    Fixture fixture;
    PHPVPN_REQUIRE_OK(fixture.up());
    UniqueFd socket = icmpSocket();
    PHPVPN_CHECK(socket.valid());
    if (!socket.valid()) return;

    PHPVPN_CHECK(sendEcho(socket.get(), "10.99.99.7", 0x4242, 32));
    const auto request = receiveEcho(*fixture.tunnel, "10.99.99.7");
    PHPVPN_CHECK(!request.empty());
    if (request.empty()) return;
    PHPVPN_CHECK(Ipv4Address::fromNetworkBytes(request.data() + 12) == ip("10.99.99.2"));

    PHPVPN_REQUIRE_OK(fixture.tunnel->send(makeEchoReply(request)));
    PHPVPN_CHECK(socketReceivesReply(socket.get(), "10.99.99.7", 0x4242));
    PHPVPN_CHECK_EQ(fixture.tunnel->oversizedReceiveCount(), 0u);

    const std::vector<std::uint8_t> empty;
    PHPVPN_REQUIRE_ERROR(fixture.tunnel->send(empty), ErrorCode::wire_invalid_packet);
}

PHPVPN_TEST(NetnsTunnel, OversizedPacketIsCountedAndNotDelivered) {
    PHPVPN_REQUIRE_ISOLATED();
    resetNamespace();
    Fixture fixture;
    PHPVPN_REQUIRE_OK(fixture.up());
    UniqueFd socket = icmpSocket();
    PHPVPN_CHECK(sendEcho(socket.get(), "10.99.99.8", 0x0101, 400));

    std::vector<std::uint8_t> small(64);
    bool packetDelivered = false;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (fixture.tunnel->oversizedReceiveCount() == 0 && std::chrono::steady_clock::now() < deadline) {
        auto read = fixture.tunnel->receive(std::span<std::uint8_t>(small.data(), small.size()), 250);
        PHPVPN_REQUIRE_OK(read);
        if (read->status == TunnelReadStatus::packet) packetDelivered = true;
    }
    PHPVPN_CHECK_EQ(fixture.tunnel->oversizedReceiveCount(), 1u);
    PHPVPN_CHECK(!packetDelivered);

    // The next packet that fits is delivered normally.
    PHPVPN_CHECK(sendEcho(socket.get(), "10.99.99.8", 0x0102, 8));
    PHPVPN_CHECK(!receiveEcho(*fixture.tunnel, "10.99.99.8").empty());
    PHPVPN_CHECK_EQ(fixture.tunnel->oversizedReceiveCount(), 1u);
}

PHPVPN_TEST(NetnsTunnel, StopWakesBlockedReceiveAndCancelsSend) {
    PHPVPN_REQUIRE_ISOLATED();
    resetNamespace();
    Fixture fixture;
    PHPVPN_REQUIRE_OK(fixture.up());
    auto tunnel = fixture.tunnel;

    auto blocked = std::async(std::launch::async, [tunnel] {
        std::vector<std::uint8_t> buffer(2048);
        const auto started = std::chrono::steady_clock::now();
        auto read = tunnel->receive(std::span<std::uint8_t>(buffer.data(), buffer.size()), 20000);
        const auto elapsed = std::chrono::steady_clock::now() - started;
        return std::make_pair(read.ok() && read->status == TunnelReadStatus::stopped, elapsed);
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    tunnel->signalStop();
    PHPVPN_CHECK(blocked.wait_for(std::chrono::seconds(3)) == std::future_status::ready);
    const auto result = blocked.get();
    PHPVPN_CHECK(result.first);
    PHPVPN_CHECK(result.second < std::chrono::seconds(3));

    const std::vector<std::uint8_t> packet(40, 0x45);
    PHPVPN_REQUIRE_ERROR(tunnel->send(packet), ErrorCode::cancelled);

    // endSession wakes a receive of a fresh session from another thread and a
    // worker that still holds the detached tunnel only ever sees `stopped`.
    PHPVPN_REQUIRE_ERROR(fixture.driver->startSession(4u * 1024 * 1024), ErrorCode::state_conflict);
    fixture.driver->endSession();
    auto session = fixture.driver->startSession(4u * 1024 * 1024);
    PHPVPN_REQUIRE_ERROR(fixture.driver->startSession(4u * 1024 * 1024), ErrorCode::state_conflict);
    PHPVPN_REQUIRE_OK(session);
    auto second = session.value();
    auto waiting = std::async(std::launch::async, [second] {
        std::vector<std::uint8_t> buffer(2048);
        auto read = second->receive(std::span<std::uint8_t>(buffer.data(), buffer.size()), 20000);
        return read.ok() && read->status == TunnelReadStatus::stopped;
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    fixture.driver->endSession();
    PHPVPN_CHECK(waiting.wait_for(std::chrono::seconds(3)) == std::future_status::ready);
    PHPVPN_CHECK(waiting.get());
    std::vector<std::uint8_t> buffer(64);
    auto late = second->receive(std::span<std::uint8_t>(buffer.data(), buffer.size()), 10);
    PHPVPN_REQUIRE_OK(late);
    PHPVPN_CHECK(late->status == TunnelReadStatus::stopped);
    // The interface stays until closeAdapter.
    PHPVPN_CHECK(linkExists("phpvpn"));
}

// ---- 4. preflight and collisions --------------------------------------------

PHPVPN_TEST(NetnsTunnel, PreflightRejectsForeignAddressRouteAndInterfaceName) {
    PHPVPN_REQUIRE_ISOLATED();
    resetNamespace();
    TemporaryDirectory directory;
    const auto settings = sessionSettings(directory);
    auto config = vpnConfig();
    auto driver = createPlatformTunnelDriver();
    auto net = createPlatformNetConfig();

    PHPVPN_CHECK_EQ(run("ip link add dummy0 type dummy"), 0);
    PHPVPN_CHECK_EQ(run("ip link set dummy0 up"), 0);
    PHPVPN_CHECK_EQ(run("ip addr add 10.99.99.9/24 dev dummy0"), 0);
    {
        TunnelSession session(*driver, *net);
        PHPVPN_REQUIRE_ERROR(session.start(*config, settings), ErrorCode::netconfig_preflight_conflict);
        PHPVPN_CHECK(!linkExists("phpvpn"));
        PHPVPN_CHECK(!std::filesystem::exists(settings.journalPath));
    }

    PHPVPN_CHECK_EQ(run("ip addr flush dev dummy0"), 0);
    PHPVPN_CHECK_EQ(run("ip route add 10.99.99.0/24 dev dummy0 metric 300"), 0);
    {
        TunnelSession session(*driver, *net);
        PHPVPN_REQUIRE_ERROR(session.start(*config, settings), ErrorCode::netconfig_preflight_conflict);
        PHPVPN_CHECK(!linkExists("phpvpn"));
    }
    // A route in another table counts as well.
    PHPVPN_CHECK_EQ(run("ip route flush dev dummy0"), 0);
    PHPVPN_CHECK_EQ(run("ip route add 10.99.99.0/24 dev dummy0 table 1000"), 0);
    {
        TunnelSession session(*driver, *net);
        PHPVPN_REQUIRE_ERROR(session.start(*config, settings), ErrorCode::netconfig_preflight_conflict);
    }
    PHPVPN_CHECK_EQ(run("ip route flush table 1000"), 0);
    PHPVPN_CHECK_EQ(run("ip link del dummy0"), 0);

    // A foreign interface named phpvpn is a collision and stays untouched.
    PHPVPN_CHECK_EQ(run("ip link add phpvpn type dummy"), 0);
    PHPVPN_CHECK_EQ(run("ip link set phpvpn alias foreign-owner"), 0);
    {
        TunnelSession session(*driver, *net);
        PHPVPN_REQUIRE_ERROR(session.start(*config, settings), ErrorCode::netconfig_preflight_conflict);
        PHPVPN_CHECK(!session.active());
    }
    PHPVPN_CHECK(linkExists("phpvpn"));
    PHPVPN_CHECK_EQ(readSys("phpvpn", "ifalias"), std::string("foreign-owner"));
    PHPVPN_CHECK(contains(capture("ip -d link show dev phpvpn"), "dummy"));
    PHPVPN_CHECK(!std::filesystem::exists(settings.journalPath));
    run("ip link del phpvpn");
}

PHPVPN_TEST(NetnsTunnel, PersistentForeignPhpvpnFailsClosed) {
    PHPVPN_REQUIRE_ISOLATED();
    resetNamespace();
    PHPVPN_CHECK_EQ(run("ip tuntap add mode tun name phpvpn"), 0);
    const int index = static_cast<int>(::if_nametoindex("phpvpn"));

    auto driver = createPlatformTunnelDriver();
    PHPVPN_REQUIRE_OK(driver->loadAndVerify(L""));
    PHPVPN_REQUIRE_ERROR(driver->createAdapter(L"phpvpn", L"phpvpn", kGuid),
                         ErrorCode::netconfig_preflight_conflict);
    // Not attached, not marked, still persistent.
    PHPVPN_CHECK(linkExists("phpvpn"));
    PHPVPN_CHECK(readSys("phpvpn", "ifalias").empty());
    PHPVPN_CHECK(contains(capture("ip -d link show dev phpvpn"), "persist on"));

    auto net = createPlatformNetConfig();
    const std::uint64_t claimed = composeInterfaceIdentity(ourTag(), index);
    auto exists = net->interfaceExists(claimed);
    PHPVPN_REQUIRE_OK(exists);
    PHPVPN_CHECK(!exists.value());
    PHPVPN_REQUIRE_ERROR(net->removeOwnedPhantomInterface(L"phpvpn", kGuid, claimed),
                         ErrorCode::journal_ambiguous);
    PHPVPN_CHECK(linkExists("phpvpn"));

    TemporaryDirectory directory;
    TunnelSession session(*driver, *net);
    PHPVPN_REQUIRE_ERROR(session.start(*vpnConfig(), sessionSettings(directory)),
                         ErrorCode::netconfig_preflight_conflict);
    PHPVPN_CHECK(linkExists("phpvpn"));
    run("ip tuntap del mode tun name phpvpn");
}

// ---- 5. default route and DNS ------------------------------------------------

PHPVPN_TEST(NetnsTunnel, SameDefaultRouteAndDnsDetectsRouteAndResolvConfChanges) {
    PHPVPN_REQUIRE_ISOLATED();
    resetNamespace();
    auto net = createPlatformNetConfig();
    PHPVPN_CHECK_EQ(run("ip link add dummy0 type dummy"), 0);
    PHPVPN_CHECK_EQ(run("ip link set dummy0 up"), 0);
    PHPVPN_CHECK_EQ(run("ip addr add 192.0.2.1/24 dev dummy0"), 0);
    PHPVPN_CHECK_EQ(run("ip route add default via 192.0.2.254 dev dummy0 metric 100"), 0);

    auto baseline = net->snapshot();
    PHPVPN_REQUIRE_OK(baseline);
    PHPVPN_CHECK(baseline->hasDefaultRoute);
    PHPVPN_CHECK_EQ(baseline->defaultRouteMetric, 100u);
    PHPVPN_CHECK_EQ(baseline->defaultRouteInterfaceLuid,
                    static_cast<std::uint64_t>(::if_nametoindex("dummy0")));
    auto again = net->snapshot();
    PHPVPN_REQUIRE_OK(again);
    PHPVPN_CHECK(baseline->sameDefaultRouteAndDns(again.value()));

    // A default route in another table is not the default route.
    PHPVPN_CHECK_EQ(run("ip route add default via 192.0.2.253 dev dummy0 metric 1 table 1000"), 0);
    again = net->snapshot();
    PHPVPN_REQUIRE_OK(again);
    PHPVPN_CHECK(baseline->sameDefaultRouteAndDns(again.value()));
    run("ip route flush table 1000");

    // A better default route changes the result, removing it restores it.
    PHPVPN_CHECK_EQ(run("ip route add default via 192.0.2.253 dev dummy0 metric 50"), 0);
    auto changed = net->snapshot();
    PHPVPN_REQUIRE_OK(changed);
    PHPVPN_CHECK(!baseline->sameDefaultRouteAndDns(changed.value()));
    PHPVPN_CHECK_EQ(run("ip route del default via 192.0.2.253 dev dummy0 metric 50"), 0);
    again = net->snapshot();
    PHPVPN_REQUIRE_OK(again);
    PHPVPN_CHECK(baseline->sameDefaultRouteAndDns(again.value()));

    // resolv.conf replaced by a bind mount in the private mount namespace.
    TemporaryDirectory directory;
    const auto replacement = directory.path / "resolv.conf";
    {
        std::ofstream stream(replacement);
        stream << "nameserver 198.51.100.53\n";
    }
    PHPVPN_CHECK_EQ(::mount(replacement.c_str(), "/etc/resolv.conf", nullptr, MS_BIND, nullptr), 0);
    auto dns = net->snapshot();
    PHPVPN_CHECK_EQ(::umount2("/etc/resolv.conf", 0), 0);
    PHPVPN_REQUIRE_OK(dns);
    PHPVPN_CHECK(!baseline->sameDefaultRouteAndDns(dns.value()));
    PHPVPN_CHECK(std::find(dns->dnsServers.begin(), dns->dnsServers.end(), "198.51.100.53") !=
                 dns->dnsServers.end());
    PHPVPN_CHECK(std::is_sorted(dns->dnsServers.begin(), dns->dnsServers.end()));
    again = net->snapshot();
    PHPVPN_REQUIRE_OK(again);
    PHPVPN_CHECK(baseline->sameDefaultRouteAndDns(again.value()));

    // The override of the tests reads another path without resolved.
    DnsSnapshotOptions options;
    options.resolvConfPath = replacement;
    options.queryResolved = false;
    auto overridden = createNetlinkNetConfig(options)->snapshot();
    PHPVPN_REQUIRE_OK(overridden);
    PHPVPN_CHECK(overridden->dnsServers == std::vector<std::string>{"198.51.100.53"});
    run("ip link del dummy0");
}

// ---- 6. recovery and identity ------------------------------------------------

PHPVPN_TEST(NetnsTunnel, RecoveryAfterKillOfProcessHoldingTheTun) {
    PHPVPN_REQUIRE_ISOLATED();
    resetNamespace();
    TemporaryDirectory directory;
    const auto settings = sessionSettings(directory);

    int ready[2];
    PHPVPN_CHECK_EQ(::pipe2(ready, O_CLOEXEC), 0);
    std::cout.flush();
    const pid_t child = ::fork();
    if (child == 0) {
        ::close(ready[0]);
        auto driver = createPlatformTunnelDriver();
        auto net = createPlatformNetConfig();
        TunnelSession session(*driver, *net);
        const char status = session.start(*vpnConfig(), settings).ok() ? 'R' : 'F';
        const ssize_t written = ::write(ready[1], &status, 1);
        (void)written;
        for (;;) ::pause();
    }
    ::close(ready[1]);
    pollfd wait{ready[0], POLLIN, 0};
    char status = 0;
    const bool signalled = ::poll(&wait, 1, 15000) == 1 && ::read(ready[0], &status, 1) == 1;
    ::close(ready[0]);
    PHPVPN_CHECK(signalled && status == 'R');
    const int index = static_cast<int>(::if_nametoindex("phpvpn"));
    PHPVPN_CHECK(index > 0);
    PHPVPN_CHECK(std::filesystem::exists(settings.journalPath));

    ::kill(child, SIGKILL);
    int childStatus = 0;
    ::waitpid(child, &childStatus, 0);
    PHPVPN_CHECK(WIFSIGNALED(childStatus));

    // The kernel removed the interface with its address and route; the
    // journal is retired only after that is proven.
    auto driver = createPlatformTunnelDriver();
    auto net = createPlatformNetConfig();
    TunnelSession session(*driver, *net);
    PHPVPN_REQUIRE_OK(session.recoverPreviousRun(settings));
    PHPVPN_CHECK(!std::filesystem::exists(settings.journalPath));
    PHPVPN_CHECK(!linkExists("phpvpn"));
    auto snapshot = net->snapshot();
    PHPVPN_REQUIRE_OK(snapshot);
    PHPVPN_CHECK(!anyRowOf(snapshot.value(), composeInterfaceIdentity(ourTag(), index)));
    PHPVPN_CHECK(!hasAddress(snapshot.value(), static_cast<std::uint64_t>(index), "10.99.99.2", 24));

    // A clean start works right after the recovery.
    PHPVPN_REQUIRE_OK(session.start(*vpnConfig(), settings));
    PHPVPN_REQUIRE_OK(session.stop());
}

PHPVPN_TEST(NetnsTunnel, RecycledIfindexDoesNotFakeTheIdentity) {
    PHPVPN_REQUIRE_ISOLATED();
    resetNamespace();
    auto driver = createPlatformTunnelDriver();
    auto net = createPlatformNetConfig();
    PHPVPN_REQUIRE_OK(driver->loadAndVerify(L""));
    auto info = driver->createAdapter(L"phpvpn", L"phpvpn", kGuid);
    PHPVPN_REQUIRE_OK(info);
    const std::uint64_t identity = info->interfaceLuid;
    const int index = identityIfindex(identity);
    driver->closeAdapter();
    PHPVPN_CHECK(!linkExists("phpvpn"));

    PHPVPN_CHECK_EQ(run("ip link add foreign0 index " + std::to_string(index) + " type dummy"), 0);
    PHPVPN_CHECK_EQ(run("ip link set foreign0 up"), 0);
    PHPVPN_CHECK_EQ(run("ip addr add 10.99.99.2/24 dev foreign0"), 0);
    PHPVPN_CHECK_EQ(static_cast<int>(::if_nametoindex("foreign0")), index);

    auto exists = net->interfaceExists(identity);
    PHPVPN_REQUIRE_OK(exists);
    PHPVPN_CHECK(!exists.value());
    auto snapshot = net->snapshot();
    PHPVPN_REQUIRE_OK(snapshot);
    PHPVPN_CHECK(!anyRowOf(snapshot.value(), identity));
    PHPVPN_CHECK(hasAddress(snapshot.value(), static_cast<std::uint64_t>(index), "10.99.99.2", 24));
    auto byAlias = net->interfaceLuidForAlias(L"foreign0");
    PHPVPN_REQUIRE_OK(byAlias);
    PHPVPN_CHECK_EQ(byAlias.value(), static_cast<std::uint64_t>(index));

    // Removals for the old identity leave the foreign rows alone.
    PHPVPN_REQUIRE_OK(net->removeUnicastAddress({identity, ip("10.99.99.2"), 24}));
    PHPVPN_CHECK(contains(capture("ip -4 addr show dev foreign0"), "10.99.99.2/24"));
    PHPVPN_REQUIRE_ERROR(net->addRoute({identity, *Ipv4Cidr::parse("10.99.99.0/24"), Ipv4Address(0), 5}),
                         ErrorCode::netconfig_apply_failed);

    // A mark of another installation is not ours either.
    PHPVPN_CHECK_EQ(run(std::string("ip link set foreign0 alias ") + kOtherMarker), 0);
    exists = net->interfaceExists(identity);
    PHPVPN_REQUIRE_OK(exists);
    PHPVPN_CHECK(!exists.value());
    run("ip link del foreign0");
}

PHPVPN_TEST(NetnsTunnel, RemoveOwnedPhantomInterfaceOnlyOnExactMatch) {
    PHPVPN_REQUIRE_ISOLATED();
    resetNamespace();
    auto net = createPlatformNetConfig();
    auto makePhantom = [](const char* extra) {
        run("ip link del phpvpn");
        return run(std::string("ip tuntap add mode ") + extra + " name phpvpn") == 0 &&
               run(std::string("ip link set phpvpn alias ") + kMarker) == 0;
    };
    auto identityNow = [] {
        return composeInterfaceIdentity(ourTag(), static_cast<int>(::if_nametoindex("phpvpn")));
    };

    PHPVPN_CHECK(makePhantom("tun"));
    std::uint64_t identity = identityNow();
    auto exists = net->interfaceExists(identity);
    PHPVPN_REQUIRE_OK(exists);
    PHPVPN_CHECK(exists.value());

    // Every mismatch fails closed and leaves the device.
    PHPVPN_REQUIRE_ERROR(net->removeOwnedPhantomInterface(L"other", kGuid, identity), ErrorCode::journal_ambiguous);
    PHPVPN_REQUIRE_ERROR(net->removeOwnedPhantomInterface(L"phpvpn", L"{00000000-0000-0000-0000-000000000001}", identity),
                         ErrorCode::journal_ambiguous);
    PHPVPN_REQUIRE_ERROR(net->removeOwnedPhantomInterface(L"phpvpn", kGuid,
                                                          static_cast<std::uint64_t>(identityIfindex(identity))),
                         ErrorCode::journal_ambiguous);
    PHPVPN_REQUIRE_ERROR(net->removeOwnedPhantomInterface(L"phpvpn", kGuid,
                                                          composeInterfaceIdentity(ourTag(), identityIfindex(identity) + 100)),
                         ErrorCode::journal_ambiguous);
    PHPVPN_CHECK(linkExists("phpvpn"));

    // Attached to a process: carrier on.
    {
        UniqueFd attached = attachToPersistent("phpvpn");
        PHPVPN_CHECK(attached.valid());
        PHPVPN_REQUIRE_ERROR(net->removeOwnedPhantomInterface(L"phpvpn", kGuid, identity),
                             ErrorCode::journal_ambiguous);
        PHPVPN_CHECK(linkExists("phpvpn"));
    }

    // Exact match: removed.
    PHPVPN_REQUIRE_OK(net->removeOwnedPhantomInterface(L"phpvpn", kGuid, identity));
    PHPVPN_CHECK(waitUntilGone("phpvpn", std::chrono::milliseconds(2000)));
    // Absent everywhere: success without action.
    PHPVPN_REQUIRE_OK(net->removeOwnedPhantomInterface(L"phpvpn", kGuid, identity));

    // A TAP device, a device owned by another user and a dummy are never removed.
    PHPVPN_CHECK(makePhantom("tap"));
    PHPVPN_REQUIRE_ERROR(net->removeOwnedPhantomInterface(L"phpvpn", kGuid, identityNow()), ErrorCode::journal_ambiguous);
    PHPVPN_CHECK(linkExists("phpvpn"));
    PHPVPN_CHECK(makePhantom("tun user 1000"));
    PHPVPN_REQUIRE_ERROR(net->removeOwnedPhantomInterface(L"phpvpn", kGuid, identityNow()), ErrorCode::journal_ambiguous);
    PHPVPN_CHECK(linkExists("phpvpn"));
    run("ip link del phpvpn");
    PHPVPN_CHECK_EQ(run("ip link add phpvpn type dummy"), 0);
    PHPVPN_CHECK_EQ(run(std::string("ip link set phpvpn alias ") + kMarker), 0);
    PHPVPN_REQUIRE_ERROR(net->removeOwnedPhantomInterface(L"phpvpn", kGuid, identityNow()), ErrorCode::journal_ambiguous);
    PHPVPN_CHECK(linkExists("phpvpn"));

    // The recorded index is gone but the name or the mark lives on elsewhere.
    const std::uint64_t stale = composeInterfaceIdentity(ourTag(), identityIfindex(identityNow()) + 50);
    PHPVPN_REQUIRE_ERROR(net->removeOwnedPhantomInterface(L"phpvpn", kGuid, stale), ErrorCode::journal_ambiguous);
    run("ip link set phpvpn name foreign0");
    PHPVPN_REQUIRE_ERROR(net->removeOwnedPhantomInterface(L"phpvpn", kGuid, stale), ErrorCode::journal_ambiguous);
    run("ip link del foreign0");
    run("ip link del phpvpn");
}

// ---- 7. the whole session ----------------------------------------------------

PHPVPN_TEST(NetnsTunnel, TunnelSessionStartAndStopOverTheRealPlatform) {
    PHPVPN_REQUIRE_ISOLATED();
    resetNamespace();
    TemporaryDirectory directory;
    const auto settings = sessionSettings(directory);
    auto driver = createPlatformTunnelDriver();
    auto net = createPlatformNetConfig();
    TunnelSession session(*driver, *net);

    PHPVPN_REQUIRE_OK(session.recoverPreviousRun(settings));  // no journal
    PHPVPN_REQUIRE_OK(session.start(*vpnConfig(), settings));
    PHPVPN_CHECK(session.active());
    PHPVPN_CHECK(session.tunnel() != nullptr);
    const std::uint64_t identity = session.interfaceLuid();
    PHPVPN_CHECK_EQ(identityOwnerTag(identity), ourTag());
    PHPVPN_CHECK(linkExists("phpvpn"));

    std::ifstream stream(settings.journalPath);
    std::stringstream journalText;
    journalText << stream.rdbuf();
    auto journal = OwnershipJournal::parse(journalText.str());
    PHPVPN_REQUIRE_OK(journal);
    PHPVPN_CHECK_EQ(journal->adapterName, std::string("phpvpn"));
    PHPVPN_CHECK_EQ(journal->interfaceLuid, identity);
    PHPVPN_CHECK(journal->adapterCreated && journal->addressApplied && journal->routeApplied);
    PHPVPN_CHECK_EQ(journal->routeMetric, 5u);

    auto snapshot = net->snapshot();
    PHPVPN_REQUIRE_OK(snapshot);
    PHPVPN_CHECK(hasAddress(snapshot.value(), identity, "10.99.99.2", 24));
    PHPVPN_CHECK(hasRoute(snapshot.value(), identity, "10.99.99.0/24", 5));

    // Traffic through the session tunnel.
    UniqueFd socket = icmpSocket();
    PHPVPN_CHECK(sendEcho(socket.get(), "10.99.99.20", 0x7777, 16));
    const auto request = receiveEcho(*session.tunnel(), "10.99.99.20");
    PHPVPN_CHECK(!request.empty());
    if (!request.empty()) {
        PHPVPN_REQUIRE_OK(session.tunnel()->send(makeEchoReply(request)));
        PHPVPN_CHECK(socketReceivesReply(socket.get(), "10.99.99.20", 0x7777));
    }

    PHPVPN_REQUIRE_ERROR(session.start(*vpnConfig(), settings), ErrorCode::state_conflict);
    PHPVPN_REQUIRE_OK(session.stop());
    PHPVPN_CHECK(!session.active());
    PHPVPN_CHECK(!linkExists("phpvpn"));
    PHPVPN_CHECK(!std::filesystem::exists(settings.journalPath));
    snapshot = net->snapshot();
    PHPVPN_REQUIRE_OK(snapshot);
    PHPVPN_CHECK(!anyRowOf(snapshot.value(), identity));
}

}  // namespace phpvpn
