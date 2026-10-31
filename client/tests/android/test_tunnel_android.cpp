// T-N02 (plan-android-v001 WP03): AndroidTunnelDriver with fake hosts and a
// fake packet session: consent, the registered VpnService, the synthetic
// adapter, establish and the descriptor lifecycle. Pure C++, runs on the build
// host (Windows, Linux) and on the emulator.
#include "harness.h"

#include <memory>
#include <string>

#include "fake_vpn_hosts.h"
#include "platform/android/netconfig_android.h"
#include "platform/android/tunnel_android.h"

namespace phpvpn {
namespace {

using test::FakeNetworkHost;
using test::FakeTunnelFactory;
using test::FakeTunnelHost;

Ipv4Address ip(const char* text) { return *Ipv4Address::parse(text); }
Ipv4Cidr cidr(const char* text) { return *Ipv4Cidr::parse(text); }

struct Driver {
    std::shared_ptr<AndroidVpnContext> context = std::make_shared<AndroidVpnContext>(test::testContextOptions());
    std::shared_ptr<FakeNetworkHost> network = std::make_shared<FakeNetworkHost>(
        test::snapshotJson(test::emulatorWifi().handle, {test::emulatorWifi()}));
    std::shared_ptr<FakeTunnelHost> tunnelHost = std::make_shared<FakeTunnelHost>();
    FakeTunnelFactory sessions;
    std::unique_ptr<TunnelDriver> driver;
    std::unique_ptr<NetConfig> netConfig;

    Driver() {
        context->setNetworkHost(network);
        context->setTunnelHost(tunnelHost);
        driver = createAndroidTunnelDriver(context, sessions.factory());
        netConfig = createAndroidNetConfig(context, nullptr);
    }

    // What TunnelSession does before startSession.
    std::uint64_t prepare() {
        if (!driver->loadAndVerify(L"").ok()) return 0;
        if (!netConfig->snapshot().ok()) return 0;
        const auto adapter = driver->createAdapter(L"phpvpn", L"phpvpn", L"{3f6c2a91-5b7e-4d0a-9c1e-8a2b7d4e6f10}");
        if (!adapter.ok()) return 0;
        const std::uint64_t identity = adapter->interfaceLuid;
        if (!netConfig->addUnicastAddress(UnicastAddressEntry{identity, ip("10.99.99.2"), 24}).ok()) return 0;
        if (!netConfig->addRoute(RouteEntry{identity, cidr("10.99.99.0/24"), Ipv4Address(0), 5}).ok()) return 0;
        return identity;
    }
};

}  // namespace

PHPVPN_TEST(AndroidTunnel, LoadAndVerifyChecksTheConsentFirst) {
    Driver fixture;
    fixture.network->consent = false;
    fixture.context->setTunnelHost(nullptr);
    PHPVPN_REQUIRE_ERROR(fixture.driver->loadAndVerify(L""), ErrorCode::tunnel_permission_missing);

    fixture.network->consentFailure = ErrorCode::tunnel_adapter_failed;
    PHPVPN_REQUIRE_ERROR(fixture.driver->loadAndVerify(L""), ErrorCode::tunnel_adapter_failed);
    fixture.network->consentFailure.reset();

    fixture.network->consent = true;
    const auto missing = fixture.driver->loadAndVerify(L"");
    PHPVPN_REQUIRE_ERROR(missing, ErrorCode::tunnel_adapter_failed);
    if (!missing.ok()) PHPVPN_CHECK_EQ(missing.error().detail, std::string("the VPN service is not running"));
    PHPVPN_CHECK(!errorCodeIsRecoverable(ErrorCode::tunnel_permission_missing));

    fixture.context->setTunnelHost(fixture.tunnelHost);
    PHPVPN_REQUIRE_OK(fixture.driver->loadAndVerify(L"ignored"));

    fixture.context->setNetworkHost(nullptr);
    PHPVPN_REQUIRE_ERROR(fixture.driver->loadAndVerify(L""), ErrorCode::tunnel_driver_unsupported);
}

PHPVPN_TEST(AndroidTunnel, CreateAdapterAllocatesTheSyntheticIdentity) {
    Driver fixture;
    // Not verified yet.
    PHPVPN_REQUIRE_ERROR(fixture.driver->createAdapter(L"phpvpn", L"phpvpn", L"{guid}"),
                         ErrorCode::tunnel_adapter_failed);
    PHPVPN_REQUIRE_OK(fixture.driver->loadAndVerify(L""));
    const auto adapter = fixture.driver->createAdapter(L"phpvpn", L"phpvpn", L"{3F6C2A91-5B7E-4D0A-9C1E-8A2B7D4E6F10}");
    PHPVPN_REQUIRE_OK(adapter);
    PHPVPN_CHECK(isAndroidIdentity(adapter->interfaceLuid));
    PHPVPN_CHECK(adapter->created);
    PHPVPN_CHECK_EQ(adapter->adapterGuid, std::string("{3F6C2A91-5B7E-4D0A-9C1E-8A2B7D4E6F10}"));
    PHPVPN_CHECK_EQ(fixture.context->currentIdentity(), adapter->interfaceLuid);
    // Nothing was established: no interface exists yet.
    PHPVPN_CHECK_EQ(fixture.tunnelHost->establishCalls, 0);
    PHPVPN_REQUIRE_ERROR(fixture.driver->openAdapter(L"phpvpn"), ErrorCode::not_supported);
    PHPVPN_CHECK_EQ(fixture.driver->runningDriverVersion(), std::uint32_t{0});
    fixture.driver->closeAdapter();
    PHPVPN_CHECK_EQ(fixture.context->currentIdentity(), std::uint64_t{0});
}

PHPVPN_TEST(AndroidTunnel, StartSessionEstablishesVerifiesAndWraps) {
    Driver fixture;
    PHPVPN_REQUIRE_ERROR(fixture.driver->startSession(4u * 1024 * 1024), ErrorCode::tunnel_adapter_failed);
    const std::uint64_t identity = fixture.prepare();
    PHPVPN_CHECK(identity != 0);
    const auto tunnel = fixture.driver->startSession(4u * 1024 * 1024);
    PHPVPN_REQUIRE_OK(tunnel);
    PHPVPN_CHECK(*tunnel == fixture.sessions.tunnel);
    PHPVPN_CHECK_EQ(*fixture.sessions.lastDescriptor, fixture.tunnelHost->lastDescriptor);
    PHPVPN_CHECK(fixture.context->holdsInterface(identity));
    PHPVPN_REQUIRE_ERROR(fixture.driver->startSession(4u * 1024 * 1024), ErrorCode::state_conflict);

    // endSession detaches the session but keeps the interface (Linux order).
    fixture.driver->endSession();
    PHPVPN_CHECK(fixture.sessions.tunnel->detached);
    PHPVPN_CHECK(fixture.context->holdsInterface(identity));
    PHPVPN_CHECK_EQ(fixture.tunnelHost->waitCalls, 0);

    fixture.driver->closeAdapter();
    PHPVPN_CHECK(!fixture.context->holdsInterface(identity));
    PHPVPN_CHECK_EQ(fixture.tunnelHost->waitCalls, 1);
#if !defined(_WIN32)
    PHPVPN_CHECK(!test::descriptorIsOpen(fixture.tunnelHost->lastDescriptor));
#endif
}

PHPVPN_TEST(AndroidTunnel, FailuresAfterEstablishReleaseTheInterface) {
    Driver fixture;
    std::uint64_t identity = fixture.prepare();
    fixture.tunnelHost->establishFailure = ErrorCode::tunnel_adapter_failed;
    PHPVPN_REQUIRE_ERROR(fixture.driver->startSession(1), ErrorCode::tunnel_adapter_failed);
    PHPVPN_CHECK(!fixture.context->holdsInterface(identity));
    fixture.driver->closeAdapter();

    // The packet session cannot be created: the descriptor is closed again.
    Driver failing;
    failing.sessions.failure = ErrorCode::tunnel_session_failed;
    failing.driver = createAndroidTunnelDriver(failing.context, failing.sessions.factory());
    identity = failing.prepare();
    PHPVPN_REQUIRE_ERROR(failing.driver->startSession(1), ErrorCode::tunnel_session_failed);
    PHPVPN_CHECK(!failing.context->holdsInterface(identity));
    PHPVPN_CHECK_EQ(failing.tunnelHost->waitCalls, 1);
    PHPVPN_CHECK_EQ(failing.context->currentIdentity(), std::uint64_t{0});
#if !defined(_WIN32)
    PHPVPN_CHECK(!test::descriptorIsOpen(failing.tunnelHost->lastDescriptor));
#endif

    // No baseline: the snapshot before createAdapter is missing.
    Driver blind;
    PHPVPN_REQUIRE_OK(blind.driver->loadAndVerify(L""));
    const auto adapter = blind.driver->createAdapter(L"phpvpn", L"phpvpn", L"{guid}");
    PHPVPN_REQUIRE_OK(adapter);
    PHPVPN_REQUIRE_OK(blind.netConfig->addUnicastAddress(UnicastAddressEntry{adapter->interfaceLuid, ip("10.99.99.2"), 24}));
    PHPVPN_REQUIRE_OK(blind.netConfig->addRoute(RouteEntry{adapter->interfaceLuid, cidr("10.99.99.0/24"), Ipv4Address(0), 5}));
    PHPVPN_REQUIRE_ERROR(blind.driver->startSession(1), ErrorCode::netconfig_verify_failed);
    PHPVPN_CHECK_EQ(blind.context->currentIdentity(), std::uint64_t{0});

    // The snapshot after establish cannot be read.
    Driver unreadable;
    identity = unreadable.prepare();
    unreadable.network->snapshotFailure = ErrorCode::netconfig_apply_failed;
    PHPVPN_REQUIRE_ERROR(unreadable.driver->startSession(1), ErrorCode::netconfig_verify_failed);
    PHPVPN_CHECK(!unreadable.context->holdsInterface(identity));
}

PHPVPN_TEST(AndroidTunnel, DriverClosesOnlyItsOwnAdapter) {
    Driver fixture;
    const std::uint64_t identity = fixture.prepare();
    PHPVPN_REQUIRE_OK(fixture.driver->startSession(1));
    {
        // A second driver over the same context (a test double, never the runtime).
        auto other = createAndroidTunnelDriver(fixture.context, fixture.sessions.factory());
        other->closeAdapter();
    }
    PHPVPN_CHECK(fixture.context->holdsInterface(identity));
    fixture.driver.reset();  // the destructor closes like closeAdapter
    PHPVPN_CHECK(!fixture.context->holdsInterface(identity));
}

PHPVPN_TEST(AndroidTunnel, WithoutAContextEveryOperationIsUnsupported) {
    auto driver = createAndroidTunnelDriver(nullptr, nullptr);
    const ErrorCode unsupported = ErrorCode::tunnel_driver_unsupported;
    PHPVPN_REQUIRE_ERROR(driver->loadAndVerify(L""), unsupported);
    PHPVPN_REQUIRE_ERROR(driver->createAdapter(L"phpvpn", L"phpvpn", L"{guid}"), unsupported);
    PHPVPN_REQUIRE_ERROR(driver->openAdapter(L"phpvpn"), unsupported);
    PHPVPN_REQUIRE_ERROR(driver->startSession(1), unsupported);
    driver->endSession();
    driver->closeAdapter();
    PHPVPN_CHECK_EQ(driver->runningDriverVersion(), std::uint32_t{0});
}

}  // namespace phpvpn
