// T-N02 (plan-android-v001 WP03): VpnSpec acceptance, the AndroidVpnContext
// state machine with fake hosts and the synthetic identity. Pure C++, runs on
// the build host (Windows, Linux) and on the emulator.
#include "harness.h"

#include <memory>

#include "fake_vpn_hosts.h"
#include "platform/android/vpn_context.h"

namespace phpvpn {
namespace {

using test::FakeNetworkHost;
using test::FakeTunnelHost;

Ipv4Address ip(const char* text) { return *Ipv4Address::parse(text); }
Ipv4Cidr cidr(const char* text) { return *Ipv4Cidr::parse(text); }

struct Fixture {
    std::shared_ptr<AndroidVpnContext> context = std::make_shared<AndroidVpnContext>(test::testContextOptions());
    std::shared_ptr<FakeNetworkHost> network =
        std::make_shared<FakeNetworkHost>(test::snapshotJson(test::emulatorWifi().handle, {test::emulatorWifi()}));
    std::shared_ptr<FakeTunnelHost> tunnel = std::make_shared<FakeTunnelHost>();

    Fixture() {
        context->setNetworkHost(network);
        context->setTunnelHost(tunnel);
    }

    // createAdapter plus the values TunnelSession records for 10.99.99.2/24.
    std::uint64_t prepared() {
        const auto identity = context->createAdapter("phpvpn");
        if (!identity.ok()) return 0;
        if (!context->recordAddress(*identity, ip("10.99.99.2"), 24).ok()) return 0;
        if (!context->recordRoute(*identity, cidr("10.99.99.0/24"), Ipv4Address(0)).ok()) return 0;
        return *identity;
    }
};

}  // namespace

PHPVPN_TEST(AndroidVpnContext, SpecSerializesTheBuilderContract) {
    VpnSpec spec;
    spec.address = ip("10.99.99.2");
    spec.prefixLength = 24;
    spec.route = cidr("10.99.99.0/24");
    spec.disallowedApplication = "io.github.jaromirsivic.phpvpn";
    PHPVPN_CHECK_EQ(serializeVpnSpec(spec),
                    std::string("{\"version\":1,\"session\":\"phpvpn\",\"address\":\"10.99.99.2\",\"prefixLength\":24,"
                                "\"route\":\"10.99.99.0\",\"routePrefixLength\":24,\"mtu\":1500,"
                                "\"disallowedApplications\":[\"io.github.jaromirsivic.phpvpn\"],"
                                "\"allowIpv6\":true,\"metered\":false,\"dnsServers\":[]}"));
}

PHPVPN_TEST(AndroidVpnContext, SyntheticIdentityCarriesTheAnTag) {
    PHPVPN_CHECK_EQ(composeAndroidIdentity(1), std::uint64_t{0x414E000000000001});
    PHPVPN_CHECK(isAndroidIdentity(composeAndroidIdentity(1)));
    PHPVPN_CHECK(!isAndroidIdentity(composeAndroidIdentity(0)));
    PHPVPN_CHECK(!isAndroidIdentity(16));               // an underlying ifindex
    PHPVPN_CHECK(!isAndroidIdentity(0xFFFFFFFFu));      // an unknown ifindex
    PHPVPN_CHECK(!isAndroidIdentity(0x414F000000000001));

    Fixture fixture;
    const auto first = fixture.context->createAdapter("phpvpn");
    PHPVPN_REQUIRE_OK(first);
    PHPVPN_CHECK(isAndroidIdentity(*first));
    PHPVPN_CHECK_EQ(*first >> 48, kAndroidIdentityTag);
    PHPVPN_CHECK_EQ(fixture.context->currentIdentity(), *first);
    fixture.context->closeAdapter();
    PHPVPN_CHECK_EQ(fixture.context->currentIdentity(), std::uint64_t{0});

    // A new context in the same process never repeats an identity.
    AndroidVpnContext other(test::testContextOptions());
    const auto second = other.createAdapter("phpvpn");
    PHPVPN_REQUIRE_OK(second);
    PHPVPN_CHECK(isAndroidIdentity(*second));
    PHPVPN_CHECK(*second > *first);
}

PHPVPN_TEST(AndroidVpnContext, AcceptsOnlyTheExactAddress) {
    Fixture fixture;
    const auto identity = fixture.context->createAdapter("phpvpn");
    PHPVPN_REQUIRE_OK(identity);
    const ErrorCode refused = ErrorCode::netconfig_apply_failed;
    PHPVPN_REQUIRE_ERROR(fixture.context->recordAddress(*identity + 1, ip("10.99.99.2"), 24), refused);
    PHPVPN_REQUIRE_ERROR(fixture.context->recordAddress(16, ip("10.99.99.2"), 24), refused);
    PHPVPN_REQUIRE_ERROR(fixture.context->recordAddress(*identity, ip("10.99.99.0"), 24), refused);    // network
    PHPVPN_REQUIRE_ERROR(fixture.context->recordAddress(*identity, ip("10.99.99.1"), 24), refused);    // relay
    PHPVPN_REQUIRE_ERROR(fixture.context->recordAddress(*identity, ip("10.99.99.255"), 24), refused);  // broadcast
    PHPVPN_REQUIRE_ERROR(fixture.context->recordAddress(*identity, ip("10.99.99.2"), 16), refused);
    PHPVPN_REQUIRE_ERROR(fixture.context->recordAddress(*identity, ip("10.99.99.2"), 30), refused);
    PHPVPN_REQUIRE_ERROR(fixture.context->recordAddress(*identity, ip("10.99.99.2"), 32), refused);
    PHPVPN_REQUIRE_OK(fixture.context->recordAddress(*identity, ip("10.99.99.2"), 24));
    PHPVPN_REQUIRE_OK(fixture.context->recordAddress(*identity, ip("10.99.99.2"), 24));  // same value again
    PHPVPN_REQUIRE_ERROR(fixture.context->recordAddress(*identity, ip("10.99.99.3"), 24), refused);
    PHPVPN_REQUIRE_ERROR(fixture.context->recordAddress(*identity, ip("10.99.99.2"), 25), refused);

    // A /29 prefix is a valid VPN prefix as well.
    fixture.context->closeAdapter();
    const auto small = fixture.context->createAdapter("phpvpn");
    PHPVPN_REQUIRE_OK(small);
    PHPVPN_REQUIRE_OK(fixture.context->recordAddress(*small, ip("192.168.7.10"), 29));
}

PHPVPN_TEST(AndroidVpnContext, AcceptsOnlyTheRouteOfTheAddressPrefix) {
    Fixture fixture;
    const auto identity = fixture.context->createAdapter("phpvpn");
    PHPVPN_REQUIRE_OK(identity);
    const ErrorCode refused = ErrorCode::netconfig_apply_failed;
    // The route needs the address first.
    PHPVPN_REQUIRE_ERROR(fixture.context->recordRoute(*identity, cidr("10.99.99.0/24"), Ipv4Address(0)), refused);
    PHPVPN_REQUIRE_OK(fixture.context->recordAddress(*identity, ip("10.99.99.2"), 24));
    PHPVPN_REQUIRE_ERROR(fixture.context->recordRoute(*identity + 1, cidr("10.99.99.0/24"), Ipv4Address(0)), refused);
    PHPVPN_REQUIRE_ERROR(fixture.context->recordRoute(*identity, cidr("10.99.99.0/24"), ip("10.99.99.1")), refused);
    PHPVPN_REQUIRE_ERROR(fixture.context->recordRoute(*identity, cidr("10.99.98.0/24"), Ipv4Address(0)), refused);
    PHPVPN_REQUIRE_ERROR(fixture.context->recordRoute(*identity, cidr("10.99.99.0/25"), Ipv4Address(0)), refused);
    PHPVPN_REQUIRE_ERROR(fixture.context->recordRoute(*identity, Ipv4Cidr(), Ipv4Address(0)), refused);  // default route
    PHPVPN_REQUIRE_OK(fixture.context->recordRoute(*identity, cidr("10.99.99.0/24"), Ipv4Address(0)));
    PHPVPN_REQUIRE_OK(fixture.context->recordRoute(*identity, cidr("10.99.99.0/24"), Ipv4Address(0)));

    const auto spec = fixture.context->preparedSpec();
    PHPVPN_CHECK(spec.has_value());
    if (!spec) return;
    VpnSpec expected;
    expected.address = ip("10.99.99.2");
    expected.prefixLength = 24;
    expected.route = cidr("10.99.99.0/24");
    expected.disallowedApplication = "io.github.jaromirsivic.phpvpn";
    PHPVPN_CHECK(*spec == expected);
    PHPVPN_CHECK_EQ(spec->mtu, std::uint32_t{1500});
    PHPVPN_CHECK_EQ(spec->sessionName, std::string("phpvpn"));
    PHPVPN_CHECK(spec->dnsServers.empty());
}

PHPVPN_TEST(AndroidVpnContext, EstablishNeedsTheAdapterTheSpecAndTheService) {
    Fixture fixture;
    PHPVPN_REQUIRE_ERROR(fixture.context->establish(), ErrorCode::tunnel_adapter_failed);

    const auto identity = fixture.context->createAdapter("phpvpn");
    PHPVPN_REQUIRE_OK(identity);
    PHPVPN_REQUIRE_ERROR(fixture.context->establish(), ErrorCode::tunnel_session_failed);
    PHPVPN_REQUIRE_OK(fixture.context->recordAddress(*identity, ip("10.99.99.2"), 24));
    PHPVPN_REQUIRE_ERROR(fixture.context->establish(), ErrorCode::tunnel_session_failed);
    PHPVPN_REQUIRE_OK(fixture.context->recordRoute(*identity, cidr("10.99.99.0/24"), Ipv4Address(0)));
    PHPVPN_CHECK_EQ(fixture.tunnel->establishCalls, 0);

    fixture.context->setTunnelHost(nullptr);
    const auto missing = fixture.context->establish();
    PHPVPN_REQUIRE_ERROR(missing, ErrorCode::tunnel_adapter_failed);
    if (!missing.ok()) PHPVPN_CHECK_EQ(missing.error().detail, std::string("the VPN service is not running"));

    fixture.context->setTunnelHost(fixture.tunnel);
    fixture.tunnel->establishFailure = ErrorCode::tunnel_permission_missing;
    PHPVPN_REQUIRE_ERROR(fixture.context->establish(), ErrorCode::tunnel_permission_missing);
    PHPVPN_CHECK(!fixture.context->holdsInterface(*identity));

    fixture.tunnel->establishFailure.reset();
    const auto descriptor = fixture.context->establish();
    PHPVPN_REQUIRE_OK(descriptor);
    PHPVPN_CHECK(*descriptor >= 0);
    PHPVPN_CHECK(fixture.tunnel->lastSpec.has_value());
    PHPVPN_CHECK_EQ(fixture.tunnel->lastSpec->address, ip("10.99.99.2"));
    PHPVPN_CHECK_EQ(fixture.tunnel->lastSpec->disallowedApplication, std::string("io.github.jaromirsivic.phpvpn"));

    // Nothing changes once the interface exists.
    PHPVPN_REQUIRE_ERROR(fixture.context->establish(), ErrorCode::state_conflict);
    PHPVPN_REQUIRE_ERROR(fixture.context->createAdapter("phpvpn"), ErrorCode::state_conflict);
    PHPVPN_REQUIRE_ERROR(fixture.context->recordAddress(*identity, ip("10.99.99.3"), 24),
                         ErrorCode::netconfig_apply_failed);
    PHPVPN_REQUIRE_OK(fixture.context->recordAddress(*identity, ip("10.99.99.2"), 24));
    fixture.context->closeAdapter();
}

PHPVPN_TEST(AndroidVpnContext, InterfaceExistsOnlyWhileTheDescriptorIsHeld) {
    Fixture fixture;
    const std::uint64_t identity = fixture.prepared();
    PHPVPN_CHECK(identity != 0);
    PHPVPN_CHECK(!fixture.context->holdsInterface(identity));
    PHPVPN_CHECK(!fixture.context->holdsInterface(0));

    const auto descriptor = fixture.context->establish();
    PHPVPN_REQUIRE_OK(descriptor);
    PHPVPN_CHECK(fixture.context->holdsInterface(identity));
    PHPVPN_CHECK(!fixture.context->holdsInterface(identity + 1));
    PHPVPN_CHECK(!fixture.context->holdsInterface(16));
#if !defined(_WIN32)
    PHPVPN_CHECK(test::descriptorIsOpen(*descriptor));
#endif

    fixture.context->closeAdapter();
    PHPVPN_CHECK(!fixture.context->holdsInterface(identity));
    PHPVPN_CHECK_EQ(fixture.tunnel->waitCalls, 1);
    PHPVPN_CHECK_EQ(fixture.tunnel->lastWaitTimeoutMs, 2000);
    PHPVPN_CHECK_EQ(fixture.context->currentIdentity(), std::uint64_t{0});
    PHPVPN_CHECK(!fixture.context->preparedSpec().has_value());
#if !defined(_WIN32)
    PHPVPN_CHECK(!test::descriptorIsOpen(*descriptor));
#endif

    // Closing again is harmless and waits for nothing.
    fixture.context->closeAdapter();
    PHPVPN_CHECK_EQ(fixture.tunnel->waitCalls, 1);
}

PHPVPN_TEST(AndroidVpnContext, CloseWithoutTheServiceStillReleasesTheDescriptor) {
    Fixture fixture;
    const std::uint64_t identity = fixture.prepared();
    const auto descriptor = fixture.context->establish();
    PHPVPN_REQUIRE_OK(descriptor);
    // onDestroy of the service unregistered its host before the teardown.
    fixture.context->setTunnelHost(nullptr);
    fixture.context->closeAdapter();
    PHPVPN_CHECK(!fixture.context->holdsInterface(identity));
    PHPVPN_CHECK_EQ(fixture.tunnel->waitCalls, 0);
#if !defined(_WIN32)
    PHPVPN_CHECK(!test::descriptorIsOpen(*descriptor));
#endif

    // A network that does not vanish is logged, the state is reset anyway.
    fixture.context->setTunnelHost(fixture.tunnel);
    fixture.tunnel->networkGone = false;
    const std::uint64_t again = fixture.prepared();
    PHPVPN_REQUIRE_OK(fixture.context->establish());
    fixture.context->closeAdapter();
    PHPVPN_CHECK(!fixture.context->holdsInterface(again));
    PHPVPN_CHECK_EQ(fixture.tunnel->waitCalls, 1);
}

PHPVPN_TEST(AndroidVpnContext, BaselineIsTheLastSnapshotBeforeCreateAdapter) {
    Fixture fixture;
    NetworkSnapshot first;
    first.hasDefaultRoute = true;
    first.defaultRouteInterfaceLuid = 15;
    NetworkSnapshot second = first;
    second.defaultRouteInterfaceLuid = 16;
    NetworkSnapshot later = first;
    later.dnsServers = {"10.0.2.3"};

    PHPVPN_CHECK(!fixture.context->baseline().has_value());
    fixture.context->noteSnapshot(first);
    fixture.context->noteSnapshot(second);
    PHPVPN_CHECK(!fixture.context->baseline().has_value());
    PHPVPN_REQUIRE_OK(fixture.context->createAdapter("phpvpn"));
    fixture.context->noteSnapshot(later);
    const auto baseline = fixture.context->baseline();
    PHPVPN_CHECK(baseline.has_value());
    if (baseline) PHPVPN_CHECK_EQ(baseline->defaultRouteInterfaceLuid, std::uint64_t{16});
    if (baseline) PHPVPN_CHECK(baseline->dnsServers.empty());

    fixture.context->closeAdapter();
    PHPVPN_CHECK(!fixture.context->baseline().has_value());
    // Without a snapshot since the last close, the next adapter has no baseline.
    PHPVPN_REQUIRE_OK(fixture.context->createAdapter("phpvpn"));
    PHPVPN_CHECK(!fixture.context->baseline().has_value());
}

PHPVPN_TEST(AndroidVpnContext, AliasResolvesOnlyTheOpenAdapter) {
    Fixture fixture;
    PHPVPN_CHECK(!fixture.context->identityForAlias("phpvpn").has_value());
    const auto identity = fixture.context->createAdapter("phpvpn");
    PHPVPN_REQUIRE_OK(identity);
    PHPVPN_CHECK(fixture.context->identityForAlias("phpvpn") == std::optional<std::uint64_t>(*identity));
    PHPVPN_CHECK(!fixture.context->identityForAlias("tun0").has_value());
}

PHPVPN_TEST(AndroidVpnContext, InstallMakesTheContextCurrent) {
    AndroidVpnContext::uninstall();
    PHPVPN_CHECK(AndroidVpnContext::current() == nullptr);
    auto context = std::make_shared<AndroidVpnContext>(test::testContextOptions());
    AndroidVpnContext::install(context);
    PHPVPN_CHECK(AndroidVpnContext::current() == context);
    AndroidVpnContext::uninstall();
    PHPVPN_CHECK(AndroidVpnContext::current() == nullptr);
    // The objects created before keep their reference.
    PHPVPN_CHECK(context.use_count() == 1);
}

PHPVPN_TEST(AndroidVpnContext, HostsAreReplaceable) {
    Fixture fixture;
    PHPVPN_CHECK(fixture.context->networkHost() == fixture.network);
    PHPVPN_CHECK(fixture.context->tunnelHost() == fixture.tunnel);
    fixture.context->setNetworkHost(nullptr);
    fixture.context->setTunnelHost(nullptr);
    PHPVPN_CHECK(fixture.context->networkHost() == nullptr);
    PHPVPN_CHECK(fixture.context->tunnelHost() == nullptr);
}

}  // namespace phpvpn
