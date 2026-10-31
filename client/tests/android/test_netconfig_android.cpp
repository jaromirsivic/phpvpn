// T-N02 (plan-android-v001 WP03): the snapshot JSON of the NetworkHost, the
// exclusion of every TRANSPORT_VPN network, AndroidNetConfig over the context
// and the core's TunnelSession (preflight, start, stop, startup recovery) over
// AndroidNetConfig and AndroidTunnelDriver with fake hosts. Pure C++, runs on
// the build host (Windows, Linux) and on the emulator.
#include "harness.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "core/base/utf8.h"
#include "core/config/config.h"
#include "core/runtime/tunnel_session.h"
#include "fake_vpn_hosts.h"
#include "platform/android/netconfig_android.h"
#include "platform/android/tunnel_android.h"

namespace phpvpn {
namespace {

using test::FakeNetwork;
using test::FakeNetworkHost;
using test::FakeTunnelFactory;
using test::FakeTunnelHost;

Ipv4Address ip(const char* text) { return *Ipv4Address::parse(text); }
Ipv4Cidr cidr(const char* text) { return *Ipv4Cidr::parse(text); }

std::string wifiOnly() { return test::snapshotJson(test::emulatorWifi().handle, {test::emulatorWifi()}); }

bool hasRowsOf(const NetworkSnapshot& snapshot, std::uint64_t identity) {
    for (const auto& row : snapshot.addresses) {
        if (row.interfaceLuid == identity) return true;
    }
    for (const auto& row : snapshot.routes) {
        if (row.interfaceLuid == identity) return true;
    }
    return false;
}

std::shared_ptr<ClientConfig> vpnConfig() {
    auto config = std::make_shared<ClientConfig>();
    config->vpnCidr = cidr("10.99.99.0/24");
    config->virtualIp = ip("10.99.99.2");
    return config;
}

struct TemporaryDirectory {
    std::filesystem::path path;
    TemporaryDirectory() {
        static int sequence = 0;
        path = std::filesystem::temp_directory_path() /
               ("phpvpn-android-netconfig-" +
                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" +
                std::to_string(++sequence));
        std::filesystem::create_directories(path);
    }
    ~TemporaryDirectory() {
        std::error_code code;
        std::filesystem::remove_all(path, code);
    }
};

// The core's view: TunnelSession over AndroidNetConfig and AndroidTunnelDriver
// sharing one context, as the platform factories build them.
struct Stack {
    std::shared_ptr<AndroidVpnContext> context = std::make_shared<AndroidVpnContext>(test::testContextOptions());
    std::shared_ptr<FakeNetworkHost> network;
    std::shared_ptr<FakeTunnelHost> tunnelHost = std::make_shared<FakeTunnelHost>();
    FakeTunnelFactory sessions;
    std::unique_ptr<NetConfig> netConfig;
    std::unique_ptr<TunnelDriver> driver;
    TemporaryDirectory directory;
    TunnelSessionSettings settings;

    explicit Stack(std::string json) : network(std::make_shared<FakeNetworkHost>(std::move(json))) {
        context->setNetworkHost(network);
        context->setTunnelHost(tunnelHost);
        netConfig = createAndroidNetConfig(context, nullptr);
        driver = createAndroidTunnelDriver(context, sessions.factory());
        settings.journalPath = directory.path / "tunnel-journal.json";
        settings.recoveryWaitMs = 0;
    }
};

}  // namespace

PHPVPN_TEST(AndroidNetConfig, ParsesTheSnapshotOfTheKotlinHost) {
    const auto wifi = test::emulatorWifi();
    const auto cellular = test::emulatorCellular();
    const auto state = parseAndroidNetworkState(test::snapshotJson(wifi.handle, {cellular, wifi, test::foreignVpn()}));
    PHPVPN_REQUIRE_OK(state);
    PHPVPN_CHECK(state->activeNetwork == std::optional<std::int64_t>(wifi.handle));
    PHPVPN_CHECK_EQ(state->networks.size(), std::size_t{3});
    const AndroidNetworkRow* active = state->activeUnderlying();
    PHPVPN_CHECK(active != nullptr);
    if (active == nullptr) return;
    PHPVPN_CHECK_EQ(active->handle, wifi.handle);
    // IPv6 rows are validated and dropped.
    PHPVPN_CHECK_EQ(active->addresses.size(), std::size_t{1});
    PHPVPN_CHECK_EQ(active->addresses[0].address, ip("10.0.2.16"));
    PHPVPN_CHECK_EQ(active->addresses[0].prefixLength, 24);
    PHPVPN_CHECK_EQ(active->addresses[0].identity, std::uint64_t{16});
    PHPVPN_CHECK_EQ(active->routes.size(), std::size_t{2});
    PHPVPN_CHECK_EQ(active->routes[0].gateway, ip("10.0.2.2"));
    PHPVPN_CHECK(active->routes[0].isDefault && active->routes[0].unicast);
    PHPVPN_CHECK_EQ(active->routes[1].gateway, Ipv4Address(0));
    // System order, IPv6 kept as reported.
    PHPVPN_CHECK(active->dnsServers == std::vector<std::string>({"10.0.2.3", "fec0::3"}));
}

// The exact text of NetworkSnapshotJson.encode, pinned by the JVM test
// client/android/bridge/src/test/.../VpnContractTest.kt with the same literal.
PHPVPN_TEST(AndroidNetConfig, ParsesTheTextOfTheKotlinEncoder) {
    const std::string text =
        R"({"version":1,"activeNetwork":434026158090,"networks":[)"
        R"({"handle":434026158090,"interfaceName":"wlan0","vpn":false,"transports":["wifi"],)"
        R"("addresses":[{"interfaceName":"wlan0","ifindex":16,"address":"10.0.2.16","prefixLength":24},)"
        R"({"interfaceName":"wlan0","ifindex":16,"address":"fec0::d980:4e45:9fac:c5a9","prefixLength":64}],)"
        R"("routes":[{"interfaceName":"wlan0","ifindex":16,"destination":"0.0.0.0","prefixLength":0,)"
        R"("gateway":"10.0.2.2","type":"unicast","isDefault":true},)"
        R"({"interfaceName":"wlan0","ifindex":16,"destination":"10.0.2.0","prefixLength":24,)"
        R"("gateway":null,"type":"unicast","isDefault":false},)"
        R"({"interfaceName":"wlan0","ifindex":16,"destination":"::","prefixLength":0,)"
        R"("gateway":"fe80::2","type":"unicast","isDefault":true},)"
        R"({"interfaceName":"wlan0","ifindex":16,"destination":"fec0::","prefixLength":64,)"
        R"("gateway":null,"type":"unicast","isDefault":false}],)"
        R"("dnsServers":["10.0.2.3","fec0::3"]},)"
        R"({"handle":438321125386,"interfaceName":"tun0","vpn":true,"transports":["vpn"],)"
        R"("addresses":[{"interfaceName":"tun0","ifindex":30,"address":"10.99.99.2","prefixLength":24}],)"
        R"("routes":[{"interfaceName":null,"ifindex":0,"destination":"10.99.99.0","prefixLength":24,)"
        R"("gateway":null,"type":"unicast","isDefault":false}],"dnsServers":[]}]})";
    // The fake host of these tests writes the same text for the same Wi-Fi.
    PHPVPN_CHECK_EQ(test::snapshotJson(test::emulatorWifi().handle, {test::emulatorWifi()}),
                    text.substr(0, text.find(",{\"handle\":438321125386")) + "]}");

    const auto state = parseAndroidNetworkState(text);
    PHPVPN_REQUIRE_OK(state);
    const NetworkSnapshot snapshot = toNetworkSnapshot(*state);
    PHPVPN_CHECK_EQ(snapshot.addresses.size(), std::size_t{1});
    PHPVPN_CHECK_EQ(snapshot.addresses[0].address, ip("10.0.2.16"));
    PHPVPN_CHECK(!hasRowsOf(snapshot, 30));
    PHPVPN_CHECK(snapshot.hasDefaultRoute);
    PHPVPN_CHECK_EQ(snapshot.defaultRouteInterfaceLuid, std::uint64_t{16});
    PHPVPN_CHECK(snapshot.dnsServers == std::vector<std::string>({"10.0.2.3", "fec0::3"}));
}

PHPVPN_TEST(AndroidNetConfig, SnapshotLeavesOutEveryVpnNetwork) {
    const auto wifi = test::emulatorWifi();
    const auto state = parseAndroidNetworkState(
        test::snapshotJson(wifi.handle, {test::emulatorCellular(), wifi, test::foreignVpn()}));
    PHPVPN_REQUIRE_OK(state);
    const NetworkSnapshot snapshot = toNetworkSnapshot(*state);
    PHPVPN_CHECK(!hasRowsOf(snapshot, 30));  // tun0 of the foreign VPN
    PHPVPN_CHECK(hasRowsOf(snapshot, 15));
    PHPVPN_CHECK(hasRowsOf(snapshot, 16));
    PHPVPN_CHECK_EQ(snapshot.addresses.size(), std::size_t{2});
    PHPVPN_CHECK_EQ(snapshot.routes.size(), std::size_t{4});
    for (const auto& address : snapshot.addresses) PHPVPN_CHECK(!cidr("10.99.99.0/24").contains(address.address));
    PHPVPN_CHECK(snapshot.hasDefaultRoute);
    PHPVPN_CHECK_EQ(snapshot.defaultRouteInterfaceLuid, std::uint64_t{16});
    PHPVPN_CHECK_EQ(snapshot.defaultRouteMetric, std::uint32_t{0});
    PHPVPN_CHECK(snapshot.dnsServers == std::vector<std::string>({"10.0.2.3", "fec0::3"}));
    for (const auto& route : snapshot.routes) {
        PHPVPN_CHECK_EQ(route.metric, std::uint32_t{0});
        PHPVPN_CHECK(route.interfaceLuid >> 32 == 0);  // ifindex identities, point 4
    }
}

PHPVPN_TEST(AndroidNetConfig, ActiveVpnOrNoActiveNetworkHasNoDefaultRouteOrDns) {
    const auto vpn = test::foreignVpn();
    for (const auto& active : {std::optional<std::int64_t>(vpn.handle), std::optional<std::int64_t>()}) {
        const auto state = parseAndroidNetworkState(test::snapshotJson(active, {test::emulatorWifi(), vpn}));
        PHPVPN_REQUIRE_OK(state);
        PHPVPN_CHECK(state->activeUnderlying() == nullptr);
        const NetworkSnapshot snapshot = toNetworkSnapshot(*state);
        PHPVPN_CHECK(!snapshot.hasDefaultRoute);
        PHPVPN_CHECK(snapshot.dnsServers.empty());
        PHPVPN_CHECK(hasRowsOf(snapshot, 16));
    }
}

PHPVPN_TEST(AndroidNetConfig, UnknownIfindexNeverBecomesIdentityZero) {
    FakeNetwork wifi = test::emulatorWifi();
    wifi.ifindex = 0;
    const auto state = parseAndroidNetworkState(test::snapshotJson(wifi.handle, {wifi}));
    PHPVPN_REQUIRE_OK(state);
    const NetworkSnapshot snapshot = toNetworkSnapshot(*state);
    PHPVPN_CHECK(!snapshot.addresses.empty());
    for (const auto& row : snapshot.addresses) PHPVPN_CHECK_EQ(row.interfaceLuid, kUnknownAndroidInterfaceIdentity);
    PHPVPN_CHECK_EQ(snapshot.defaultRouteInterfaceLuid, kUnknownAndroidInterfaceIdentity);
    PHPVPN_CHECK_EQ(androidUnderlyingIdentity(16), std::uint64_t{16});
    PHPVPN_CHECK_EQ(androidUnderlyingIdentity(-1), kUnknownAndroidInterfaceIdentity);
}

PHPVPN_TEST(AndroidNetConfig, RoutesKeepTheirTypeAndRepresentablePrefix) {
    FakeNetwork wifi = test::emulatorWifi();
    wifi.routes.push_back(test::fakeRoute("10.99.99.0", 24, "10.0.2.2", false));
    wifi.routes.push_back(test::fakeRoute("172.16.0.0", 12, "", false));
    wifi.routes.push_back(test::fakeRoute("10.98.0.0", 24, "", false, "throw"));
    const auto state = parseAndroidNetworkState(test::snapshotJson(wifi.handle, {wifi}));
    PHPVPN_REQUIRE_OK(state);
    const NetworkSnapshot snapshot = toNetworkSnapshot(*state);
    PHPVPN_CHECK_EQ(snapshot.routes.size(), std::size_t{4});  // the throw route is left out
    PHPVPN_CHECK_EQ(snapshot.routes[2].destination.toString(), std::string("10.99.99.0/24"));
    PHPVPN_CHECK_EQ(snapshot.routes[2].nextHop, ip("10.0.2.2"));
    PHPVPN_CHECK_EQ(snapshot.routes[3].destination.prefixLength(), 0);  // /12 has no Ipv4Cidr
}

PHPVPN_TEST(AndroidNetConfig, MalformedSnapshotsFailClosed) {
    const std::string good = wifiOnly();
    PHPVPN_REQUIRE_OK(parseAndroidNetworkState(good));
    auto replaced = [&good](const std::string& from, const std::string& to) {
        std::string text = good;
        const auto position = text.find(from);
        if (position != std::string::npos) text.replace(position, from.size(), to);
        return text;
    };
    const std::vector<std::string> bad = {
        "",
        "[]",
        "{\"version\":1,\"activeNetwork\":null}",
        "{\"version\":2,\"activeNetwork\":null,\"networks\":[]}",
        "{\"version\":1,\"activeNetwork\":\"1\",\"networks\":[]}",
        "{\"version\":1,\"activeNetwork\":null,\"networks\":[],\"extra\":0}",
        replaced("\"10.0.2.16\"", "\"10.0.2.256\""),
        replaced("\"10.0.2.16\"", "\"010.0.2.16\""),
        replaced("\"prefixLength\":24", "\"prefixLength\":33"),
        replaced("\"ifindex\":16", "\"ifindex\":-1"),
        replaced("\"wifi\"", "\"satellite\""),
        replaced("\"unicast\"", "\"local\""),
        replaced("\"isDefault\":true", "\"isDefault\":1"),
        replaced("\"vpn\":false", "\"vpn\":null"),
        replaced("\"10.0.2.3\"", "\"10.0.2.3 \""),
        replaced("\"fec0::3\"", "\"fec0::3;\""),
        replaced("\"gateway\":\"10.0.2.2\"", "\"gateway\":\"gw\""),
        replaced("\"handle\":", "\"handle\":-"),
        replaced("\"dnsServers\":[", "\"dns\":["),
        test::snapshotJson(std::nullopt, {test::emulatorWifi(), test::emulatorWifi()}),  // duplicate handle
        std::string(kMaxAndroidSnapshotBytes + 1, ' '),
    };
    for (const auto& text : bad) {
        PHPVPN_REQUIRE_ERROR(parseAndroidNetworkState(text), ErrorCode::netconfig_apply_failed);
    }
    FakeNetwork crowded = test::emulatorWifi();
    crowded.dnsServers.assign(17, "10.0.2.3");
    PHPVPN_REQUIRE_ERROR(parseAndroidNetworkState(test::snapshotJson(crowded.handle, {crowded})),
                         ErrorCode::netconfig_apply_failed);
}

PHPVPN_TEST(AndroidNetConfig, SnapshotGoesThroughTheNetworkHost) {
    auto context = std::make_shared<AndroidVpnContext>(test::testContextOptions());
    auto netConfig = createAndroidNetConfig(context, nullptr);
    PHPVPN_REQUIRE_ERROR(netConfig->snapshot(), ErrorCode::netconfig_apply_failed);  // no host yet

    auto host = std::make_shared<FakeNetworkHost>(wifiOnly());
    context->setNetworkHost(host);
    const auto snapshot = netConfig->snapshot();
    PHPVPN_REQUIRE_OK(snapshot);
    PHPVPN_CHECK(snapshot->hasDefaultRoute);
    PHPVPN_CHECK_EQ(host->snapshotCalls, 1);

    host->snapshotFailure = ErrorCode::netconfig_apply_failed;
    PHPVPN_REQUIRE_ERROR(netConfig->snapshot(), ErrorCode::netconfig_apply_failed);
    host->snapshotFailure.reset();
    host->setJson("{not json");
    PHPVPN_REQUIRE_ERROR(netConfig->snapshot(), ErrorCode::netconfig_apply_failed);
}

PHPVPN_TEST(AndroidNetConfig, RemovalIsANoOpOnlyForOwnedIdentities) {
    auto context = std::make_shared<AndroidVpnContext>(test::testContextOptions());
    auto netConfig = createAndroidNetConfig(context, nullptr);
    const std::uint64_t own = composeAndroidIdentity(5);
    PHPVPN_REQUIRE_OK(netConfig->removeUnicastAddress(UnicastAddressEntry{own, ip("10.99.99.2"), 24}));
    PHPVPN_REQUIRE_OK(netConfig->removeRoute(RouteEntry{own, cidr("10.99.99.0/24"), Ipv4Address(0), 5}));
    PHPVPN_REQUIRE_ERROR(netConfig->removeUnicastAddress(UnicastAddressEntry{16, ip("10.0.2.16"), 24}),
                         ErrorCode::netconfig_apply_failed);
    PHPVPN_REQUIRE_ERROR(netConfig->removeRoute(RouteEntry{16, cidr("10.99.99.0/24"), Ipv4Address(0), 5}),
                         ErrorCode::netconfig_apply_failed);
    PHPVPN_REQUIRE_ERROR(netConfig->removeUnicastAddress(UnicastAddressEntry{own, ip("10.99.99.2"), 33}),
                         ErrorCode::invalid_argument);
    PHPVPN_REQUIRE_ERROR(netConfig->addUnicastAddress(UnicastAddressEntry{own, ip("10.99.99.2"), -1}),
                         ErrorCode::invalid_argument);
    PHPVPN_REQUIRE_ERROR(netConfig->interfaceExists(0), ErrorCode::netconfig_apply_failed);
    PHPVPN_REQUIRE_OK(netConfig->interfaceExists(own));
    PHPVPN_REQUIRE_OK(netConfig->removeOwnedPhantomInterface(L"phpvpn", L"{guid}", own));
}

PHPVPN_TEST(AndroidNetConfig, AliasResolvesTheSyntheticIdentity) {
    auto context = std::make_shared<AndroidVpnContext>(test::testContextOptions());
    auto netConfig = createAndroidNetConfig(context, nullptr);
    PHPVPN_REQUIRE_ERROR(netConfig->interfaceLuidForAlias(L"phpvpn"), ErrorCode::netconfig_apply_failed);
    const auto identity = context->createAdapter("phpvpn");
    PHPVPN_REQUIRE_OK(identity);
    const auto resolved = netConfig->interfaceLuidForAlias(L"phpvpn");
    PHPVPN_REQUIRE_OK(resolved);
    PHPVPN_CHECK_EQ(*resolved, *identity);
    PHPVPN_REQUIRE_ERROR(netConfig->interfaceLuidForAlias(L"tun0"), ErrorCode::netconfig_apply_failed);
}

PHPVPN_TEST(AndroidNetConfig, HostReachabilityFollowsTheActiveUnderlyingRoutes) {
    auto context = std::make_shared<AndroidVpnContext>(test::testContextOptions());
    context->setNetworkHost(std::make_shared<FakeNetworkHost>(wifiOnly()));
    std::vector<Ipv4Address> answer = {ip("10.0.2.2")};
    auto netConfig = createAndroidNetConfig(context, [&answer](const std::string&) { return answer; });
    const std::uint64_t own = composeAndroidIdentity(9);

    auto reachable = netConfig->isHostReachableOutsideInterface("relay.example.test", own);
    PHPVPN_REQUIRE_OK(reachable);
    PHPVPN_CHECK(*reachable);
    reachable = netConfig->isHostReachableOutsideInterface("relay.example.test", 16);  // only over wlan0
    PHPVPN_REQUIRE_OK(reachable);
    PHPVPN_CHECK(!*reachable);
    answer = {ip("203.0.113.7")};  // over the default route of wlan0
    reachable = netConfig->isHostReachableOutsideInterface("relay.example.test", own);
    PHPVPN_REQUIRE_OK(reachable);
    PHPVPN_CHECK(*reachable);
    answer.clear();  // not resolvable: never reachable
    reachable = netConfig->isHostReachableOutsideInterface("relay.example.test", own);
    PHPVPN_REQUIRE_OK(reachable);
    PHPVPN_CHECK(!*reachable);

    // Without an active underlying network nothing is reachable.
    context->setNetworkHost(std::make_shared<FakeNetworkHost>(test::snapshotJson(std::nullopt, {test::emulatorWifi()})));
    answer = {ip("10.0.2.2")};
    reachable = netConfig->isHostReachableOutsideInterface("relay.example.test", own);
    PHPVPN_REQUIRE_OK(reachable);
    PHPVPN_CHECK(!*reachable);
}

PHPVPN_TEST(AndroidNetConfig, WithoutAContextEveryOperationIsUnsupported) {
    auto netConfig = createAndroidNetConfig(nullptr, nullptr);
    const ErrorCode unsupported = ErrorCode::tunnel_driver_unsupported;
    PHPVPN_REQUIRE_ERROR(netConfig->snapshot(), unsupported);
    PHPVPN_REQUIRE_ERROR(netConfig->interfaceExists(1), unsupported);
    PHPVPN_REQUIRE_ERROR(netConfig->removeOwnedPhantomInterface(L"phpvpn", L"{guid}", 1), unsupported);
    PHPVPN_REQUIRE_ERROR(netConfig->addUnicastAddress(UnicastAddressEntry{1, ip("10.99.99.2"), 24}), unsupported);
    PHPVPN_REQUIRE_ERROR(netConfig->removeUnicastAddress(UnicastAddressEntry{1, ip("10.99.99.2"), 24}), unsupported);
    PHPVPN_REQUIRE_ERROR(netConfig->addRoute(RouteEntry{1, cidr("10.99.99.0/24"), Ipv4Address(0), 5}), unsupported);
    PHPVPN_REQUIRE_ERROR(netConfig->removeRoute(RouteEntry{1, cidr("10.99.99.0/24"), Ipv4Address(0), 5}), unsupported);
    PHPVPN_REQUIRE_ERROR(netConfig->interfaceLuidForAlias(L"phpvpn"), unsupported);
    PHPVPN_REQUIRE_ERROR(netConfig->isHostReachableOutsideInterface("relay.example.test", 1), unsupported);
}

// Preflight conflicts (T-A13 at snapshot level): an underlying address inside
// vpnCidr, for example a Wi-Fi that itself uses 10.99.99.0/24.
PHPVPN_TEST(AndroidNetConfig, PreflightRefusesAnUnderlyingAddressInsideTheVpnPrefix) {
    FakeNetwork wifi = test::emulatorWifi();
    wifi.addresses = {{"10.99.99.17", 24}};
    wifi.routes = {test::fakeRoute("0.0.0.0", 0, "10.99.99.1", true)};
    Stack stack(test::snapshotJson(wifi.handle, {wifi}));
    TunnelSession session(*stack.driver, *stack.netConfig);
    PHPVPN_REQUIRE_ERROR(session.start(*vpnConfig(), stack.settings), ErrorCode::netconfig_preflight_conflict);
    PHPVPN_CHECK_EQ(stack.context->currentIdentity(), std::uint64_t{0});
    PHPVPN_CHECK_EQ(stack.tunnelHost->establishCalls, 0);
    PHPVPN_CHECK(!std::filesystem::exists(stack.settings.journalPath));
}

// ... or an underlying route into the prefix.
PHPVPN_TEST(AndroidNetConfig, PreflightRefusesAnUnderlyingRouteIntoTheVpnPrefix) {
    FakeNetwork wifi = test::emulatorWifi();
    wifi.routes.push_back(test::fakeRoute("10.99.99.0", 24, "10.0.2.2", false));
    Stack stack(test::snapshotJson(wifi.handle, {wifi}));
    TunnelSession session(*stack.driver, *stack.netConfig);
    PHPVPN_REQUIRE_ERROR(session.start(*vpnConfig(), stack.settings), ErrorCode::netconfig_preflight_conflict);
    PHPVPN_CHECK_EQ(stack.tunnelHost->establishCalls, 0);

    // A cellular network that is not even active counts as well.
    FakeNetwork cellular = test::emulatorCellular();
    cellular.addresses.push_back({"10.99.99.40", 24});
    Stack inactive(test::snapshotJson(test::emulatorWifi().handle, {test::emulatorWifi(), cellular}));
    TunnelSession other(*inactive.driver, *inactive.netConfig);
    PHPVPN_REQUIRE_ERROR(other.start(*vpnConfig(), inactive.settings), ErrorCode::netconfig_preflight_conflict);
}

// A foreign VPN inside the prefix is replaced by our establish() and must not
// fail the preflight (point 4).
PHPVPN_TEST(AndroidNetConfig, PreflightIgnoresAForeignVpnInsideTheVpnPrefix) {
    Stack stack(test::snapshotJson(test::emulatorWifi().handle, {test::emulatorWifi(), test::foreignVpn()}));
    TunnelSession session(*stack.driver, *stack.netConfig);
    PHPVPN_REQUIRE_OK(session.start(*vpnConfig(), stack.settings));
    PHPVPN_REQUIRE_OK(session.stop());
}

PHPVPN_TEST(AndroidNetConfig, TunnelSessionStartsAndStopsOverTheContext) {
    Stack stack(wifiOnly());
    TunnelSession session(*stack.driver, *stack.netConfig);
    PHPVPN_REQUIRE_OK(session.start(*vpnConfig(), stack.settings));
    PHPVPN_CHECK(session.active());
    PHPVPN_CHECK(session.tunnel() == stack.sessions.tunnel);

    const std::uint64_t identity = session.interfaceLuid();
    PHPVPN_CHECK(isAndroidIdentity(identity));
    PHPVPN_CHECK_EQ(stack.tunnelHost->establishCalls, 1);
    PHPVPN_CHECK_EQ(*stack.sessions.calls, 1);
    PHPVPN_CHECK_EQ(*stack.sessions.lastDescriptor, stack.tunnelHost->lastDescriptor);
    PHPVPN_CHECK_EQ(stack.tunnelHost->lastSpecJson,
                    std::string("{\"version\":1,\"session\":\"phpvpn\",\"address\":\"10.99.99.2\",\"prefixLength\":24,"
                                "\"route\":\"10.99.99.0\",\"routePrefixLength\":24,\"mtu\":1500,"
                                "\"disallowedApplications\":[\"io.github.jaromirsivic.phpvpn\"],"
                                "\"allowIpv6\":true,\"metered\":false,\"dnsServers\":[]}"));
    auto exists = stack.netConfig->interfaceExists(identity);
    PHPVPN_REQUIRE_OK(exists);
    PHPVPN_CHECK(*exists);

    // The journal names the synthetic identity, so recovery can retire it.
    std::ifstream stream(stack.settings.journalPath, std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    stream.close();
    const auto journal = OwnershipJournal::parse(text);
    PHPVPN_REQUIRE_OK(journal);
    PHPVPN_CHECK_EQ(journal->interfaceLuid, identity);
    PHPVPN_CHECK(journal->addressApplied && journal->routeApplied && journal->adapterCreated);

    PHPVPN_REQUIRE_OK(session.stop());
    PHPVPN_CHECK(stack.sessions.tunnel->detached);
    PHPVPN_CHECK_EQ(stack.tunnelHost->waitCalls, 1);
    exists = stack.netConfig->interfaceExists(identity);
    PHPVPN_REQUIRE_OK(exists);
    PHPVPN_CHECK(!*exists);
    PHPVPN_CHECK(!std::filesystem::exists(stack.settings.journalPath));
#if !defined(_WIN32)
    PHPVPN_CHECK(!test::descriptorIsOpen(stack.tunnelHost->lastDescriptor));
#endif

    // A second connect allocates a new identity.
    PHPVPN_REQUIRE_OK(session.start(*vpnConfig(), stack.settings));
    PHPVPN_CHECK(session.interfaceLuid() != identity);
    PHPVPN_REQUIRE_OK(session.stop());
}

// Point 2: the active network switches while establish() runs.
PHPVPN_TEST(AndroidNetConfig, DefaultRouteChangeDuringEstablishFailsVerification) {
    const auto wifi = test::emulatorWifi();
    const auto cellular = test::emulatorCellular();
    Stack stack(test::snapshotJson(cellular.handle, {cellular, wifi}));
    // Baseline, preflight and the core's own check see cellular; the check
    // after establish sees Wi-Fi as the active network.
    stack.network->switchAfter(3, test::snapshotJson(wifi.handle, {cellular, wifi}));
    TunnelSession session(*stack.driver, *stack.netConfig);
    PHPVPN_REQUIRE_ERROR(session.start(*vpnConfig(), stack.settings), ErrorCode::netconfig_verify_failed);
    PHPVPN_CHECK_EQ(stack.tunnelHost->establishCalls, 1);
    PHPVPN_CHECK_EQ(*stack.sessions.calls, 0);
    PHPVPN_CHECK_EQ(stack.tunnelHost->waitCalls, 1);
    PHPVPN_CHECK_EQ(stack.context->currentIdentity(), std::uint64_t{0});
    PHPVPN_CHECK(!session.active());
#if !defined(_WIN32)
    PHPVPN_CHECK(!test::descriptorIsOpen(stack.tunnelHost->lastDescriptor));
#endif
}

PHPVPN_TEST(AndroidNetConfig, DnsChangeDuringEstablishFailsVerification) {
    FakeNetwork wifi = test::emulatorWifi();
    Stack stack(test::snapshotJson(wifi.handle, {wifi}));
    wifi.dnsServers = {"fec0::3", "10.0.2.3"};
    stack.network->switchAfter(3, test::snapshotJson(wifi.handle, {wifi}));
    TunnelSession session(*stack.driver, *stack.netConfig);
    PHPVPN_REQUIRE_ERROR(session.start(*vpnConfig(), stack.settings), ErrorCode::netconfig_verify_failed);
    PHPVPN_CHECK_EQ(stack.context->currentIdentity(), std::uint64_t{0});
}

PHPVPN_TEST(AndroidNetConfig, MissingConsentStopsBeforeAnythingIsCreated) {
    Stack stack(wifiOnly());
    stack.network->consent = false;
    TunnelSession session(*stack.driver, *stack.netConfig);
    PHPVPN_REQUIRE_ERROR(session.start(*vpnConfig(), stack.settings), ErrorCode::tunnel_permission_missing);
    PHPVPN_CHECK_EQ(stack.context->currentIdentity(), std::uint64_t{0});
    PHPVPN_CHECK(!std::filesystem::exists(stack.settings.journalPath));

    // Consent revoked between loadAndVerify and establish.
    stack.network->consent = true;
    stack.tunnelHost->establishFailure = ErrorCode::tunnel_permission_missing;
    PHPVPN_REQUIRE_ERROR(session.start(*vpnConfig(), stack.settings), ErrorCode::tunnel_permission_missing);
    PHPVPN_CHECK_EQ(stack.context->currentIdentity(), std::uint64_t{0});
}

// After a process death the system removed the interface with the descriptor;
// the journal of the dead process names an identity nobody holds.
PHPVPN_TEST(AndroidNetConfig, StartupRecoveryRetiresTheJournalOfADeadProcess) {
    Stack stack(wifiOnly());
    OwnershipJournal journal;
    auto name = toUtf8(stack.settings.adapterName);
    auto guid = toUtf8(stack.settings.requestedGuid);
    PHPVPN_REQUIRE_OK(name);
    PHPVPN_REQUIRE_OK(guid);
    journal.adapterName = *name;
    journal.adapterGuid = *guid;
    journal.interfaceLuid = composeAndroidIdentity(424242);
    journal.adapterCreated = journal.addressApplied = journal.routeApplied = true;
    journal.address = ip("10.99.99.2");
    journal.prefixLength = 24;
    journal.routeDestination = cidr("10.99.99.0/24");
    journal.routeMetric = 5;
    {
        std::ofstream stream(stack.settings.journalPath, std::ios::binary);
        stream << journal.serialize();
    }
    TunnelSession session(*stack.driver, *stack.netConfig);
    PHPVPN_REQUIRE_OK(session.recoverPreviousRun(stack.settings));
    PHPVPN_CHECK(!std::filesystem::exists(stack.settings.journalPath));
    PHPVPN_CHECK(stack.network->snapshotCalls >= 1);
}

// While this process still holds the interface it is no phantom: the journal
// stays for an administrator, nothing is guessed.
PHPVPN_TEST(AndroidNetConfig, StartupRecoveryKeepsTheJournalOfAHeldInterface) {
    Stack stack(wifiOnly());
    TunnelSession running(*stack.driver, *stack.netConfig);
    PHPVPN_REQUIRE_OK(running.start(*vpnConfig(), stack.settings));
    TunnelSession recovering(*stack.driver, *stack.netConfig);
    PHPVPN_REQUIRE_ERROR(recovering.recoverPreviousRun(stack.settings), ErrorCode::journal_ambiguous);
    PHPVPN_CHECK(std::filesystem::exists(stack.settings.journalPath));
    PHPVPN_REQUIRE_OK(running.stop());
}

}  // namespace phpvpn
