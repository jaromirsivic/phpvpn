// macOS platform tests owned by WP03 (plan-macos-v001 step 7).
//
// Interface identity: the pure GUID, marker and tag rules shared with Linux,
// and the live description and index lookups against lo0. Nothing here needs
// privileges or changes an interface.
#include "harness.h"

#include <net/if.h>
#include <unistd.h>

#include <string>

#include "platform/macos/interface_identity.h"

namespace phpvpn {
namespace {

constexpr const char* kMarker = "phpvpn-owner:{3f6c2a91-5b7e-4d0a-9c1e-8a2b7d4e6f10}";
// Short enough for IFNAMSIZ, never a real interface name.
constexpr const char* kMissingInterface = "phpvpnnone9";

int loopbackIndex() { return static_cast<int>(::if_nametoindex("lo0")); }

}  // namespace

PHPVPN_TEST(MacInterfaceIdentity, CanonicalGuidAcceptsOnlyTheBracedForm) {
    auto guid = canonicalInstallationGuid(L"{3F6C2A91-5B7E-4D0A-9C1E-8A2B7D4E6F10}");
    PHPVPN_REQUIRE_OK(guid);
    PHPVPN_CHECK_EQ(guid.value(), std::string("{3f6c2a91-5b7e-4d0a-9c1e-8a2b7d4e6f10}"));
    auto macGuid = canonicalInstallationGuid(L"{a1c4e7b2-6d3f-4a8e-9b0c-5e2d7f1a3c64}");
    PHPVPN_REQUIRE_OK(macGuid);
    PHPVPN_CHECK_EQ(macGuid.value(), std::string("{a1c4e7b2-6d3f-4a8e-9b0c-5e2d7f1a3c64}"));

    PHPVPN_REQUIRE_ERROR(canonicalInstallationGuid(L"3f6c2a91-5b7e-4d0a-9c1e-8a2b7d4e6f10"),
                         ErrorCode::invalid_argument);
    PHPVPN_REQUIRE_ERROR(canonicalInstallationGuid(L"{3f6c2a91-5b7e-4d0a-9c1e-8a2b7d4e6f1g}"),
                         ErrorCode::invalid_argument);
    PHPVPN_REQUIRE_ERROR(canonicalInstallationGuid(L"{3f6c2a91x5b7e-4d0a-9c1e-8a2b7d4e6f10}"),
                         ErrorCode::invalid_argument);
    PHPVPN_REQUIRE_ERROR(canonicalInstallationGuid(L"{3f6c2a91-5b7e-4d0a-9c1e-8a2b7d4e6f10}x"),
                         ErrorCode::invalid_argument);
    // A non-ASCII character in place of the last digit.
    std::wstring nonAscii = L"{3f6c2a91-5b7e-4d0a-9c1e-8a2b7d4e6f10}";
    nonAscii[36] = static_cast<wchar_t>(0xE9);
    PHPVPN_REQUIRE_ERROR(canonicalInstallationGuid(nonAscii), ErrorCode::invalid_argument);
    PHPVPN_REQUIRE_ERROR(canonicalInstallationGuid(L""), ErrorCode::invalid_argument);
}

PHPVPN_TEST(MacInterfaceIdentity, MarkerAndTagAreStableSha256Prefixes) {
    const std::string guid = "{3f6c2a91-5b7e-4d0a-9c1e-8a2b7d4e6f10}";
    PHPVPN_CHECK_EQ(ownershipMarker(guid), std::string(kMarker));
    // printf '%s' '{3f6c2a91-5b7e-4d0a-9c1e-8a2b7d4e6f10}' | shasum -a 256 -> 2319aedc...
    PHPVPN_CHECK_EQ(ownerTagForGuid(guid), 0x2319aedcu);
    PHPVPN_CHECK_EQ(ownerTagForGuid(guid), ownerTagForGuid(guid));
    // The macOS installation GUID of TunnelSessionSettings -> fa2764e5...
    PHPVPN_CHECK_EQ(ownerTagForGuid("{a1c4e7b2-6d3f-4a8e-9b0c-5e2d7f1a3c64}"), 0xfa2764e5u);
    PHPVPN_CHECK_EQ(ownerTagForAlias(kMarker), 0x2319aedcu);
}

PHPVPN_TEST(MacInterfaceIdentity, ForeignDescriptionsHaveTagZero) {
    const std::string marker = kMarker;
    PHPVPN_CHECK_EQ(ownerTagForAlias(""), 0u);
    PHPVPN_CHECK_EQ(ownerTagForAlias("phpvpn-owner:"), 0u);
    PHPVPN_CHECK_EQ(ownerTagForAlias("Wi-Fi"), 0u);
    PHPVPN_CHECK_EQ(ownerTagForAlias(marker + " "), 0u);
    PHPVPN_CHECK_EQ(ownerTagForAlias(" " + marker), 0u);
    PHPVPN_CHECK_EQ(ownerTagForAlias(marker + std::string(1, '\0')), 0u);
    PHPVPN_CHECK_EQ(ownerTagForAlias("phpvpn-owner:{3F6C2A91-5B7E-4D0A-9C1E-8A2B7D4E6F10}"), 0u);
    PHPVPN_CHECK_EQ(ownerTagForAlias("PHPVPN-OWNER:{3f6c2a91-5b7e-4d0a-9c1e-8a2b7d4e6f10}"), 0u);
    PHPVPN_CHECK_EQ(ownerTagForAlias("phpvpn-owner:3f6c2a91-5b7e-4d0a-9c1e-8a2b7d4e6f10"), 0u);
    // Another canonical GUID yields another, non-zero tag.
    const std::uint32_t other = ownerTagForAlias("phpvpn-owner:{00000000-0000-0000-0000-000000000001}");
    PHPVPN_CHECK_NE(other, 0u);
    PHPVPN_CHECK_NE(other, 0x2319aedcu);
}

PHPVPN_TEST(MacInterfaceIdentity, ComposesTagAndIfindex) {
    const std::uint64_t identity = composeInterfaceIdentity(0x2319aedcu, 42);
    PHPVPN_CHECK_EQ(identity, 0x2319aedc0000002aull);
    PHPVPN_CHECK_EQ(identityOwnerTag(identity), 0x2319aedcu);
    PHPVPN_CHECK_EQ(identityIfindex(identity), 42);
    // Foreign interfaces carry tag 0, so their identity is the bare ifindex.
    PHPVPN_CHECK_EQ(composeInterfaceIdentity(0, 42), 42ull);
    PHPVPN_CHECK_NE(composeInterfaceIdentity(0, 42), identity);
    static_assert(identityIfindex(composeInterfaceIdentity(7u, 65535)) == 65535);
    static_assert(identityOwnerTag(composeInterfaceIdentity(0xffffffffu, 1)) == 0xffffffffu);
}

PHPVPN_TEST(MacInterfaceIdentity, LoopbackDescriptionIsReadableWithoutRoot) {
    // lo0 normally has no description; any content is accepted, only the
    // read itself must work unprivileged.
    auto description = readInterfaceDescription("lo0");
    PHPVPN_REQUIRE_OK(description);
    PHPVPN_CHECK(description->size() <= 128u);
    auto tag = ownerTagOfInterface("lo0");
    PHPVPN_REQUIRE_OK(tag);
    PHPVPN_CHECK_EQ(tag.value(), ownerTagForAlias(description.value()));
}

PHPVPN_TEST(MacInterfaceIdentity, LoopbackIdentityIsForeignWithItsIfindex) {
    const int index = loopbackIndex();
    PHPVPN_CHECK(index > 0);
    auto identity = interfaceIdentityForName("lo0");
    PHPVPN_REQUIRE_OK(identity);
    PHPVPN_CHECK_EQ(identityOwnerTag(identity.value()), 0u);
    PHPVPN_CHECK_EQ(identityIfindex(identity.value()), index);
    PHPVPN_CHECK_EQ(identity.value(), static_cast<std::uint64_t>(index));

    auto carried = interfaceCarriesIdentity(identity.value());
    PHPVPN_REQUIRE_OK(carried);
    PHPVPN_CHECK(carried.value());
    auto name = interfaceNameForIdentity(identity.value());
    PHPVPN_REQUIRE_OK(name);
    PHPVPN_CHECK_EQ(name.value(), std::string("lo0"));

    // The same index with an owner tag is not lo0's identity.
    const std::uint64_t claimed = composeInterfaceIdentity(0x2319aedcu, index);
    carried = interfaceCarriesIdentity(claimed);
    PHPVPN_REQUIRE_OK(carried);
    PHPVPN_CHECK(!carried.value());
    PHPVPN_REQUIRE_ERROR(interfaceNameForIdentity(claimed), kInterfaceAbsentError);
}

PHPVPN_TEST(MacInterfaceIdentity, UnknownInterfaceIsReportedAsAbsent) {
    PHPVPN_CHECK_EQ(::if_nametoindex(kMissingInterface), 0u);
    PHPVPN_REQUIRE_ERROR(interfaceIdentityForName(kMissingInterface), kInterfaceAbsentError);
    PHPVPN_REQUIRE_ERROR(readInterfaceDescription(kMissingInterface), kInterfaceAbsentError);
    PHPVPN_REQUIRE_ERROR(ownerTagOfInterface(kMissingInterface), kInterfaceAbsentError);
    // The logical adapter name is not an interface on macOS.
    PHPVPN_REQUIRE_ERROR(interfaceIdentityForName("phpvpn"), kInterfaceAbsentError);

    // Indices that cannot exist or are unused are positively absent.
    for (const std::uint64_t identity :
         {composeInterfaceIdentity(0x2319aedcu, 0), composeInterfaceIdentity(0x2319aedcu, -1),
          composeInterfaceIdentity(0x2319aedcu, 65000), composeInterfaceIdentity(0, 65000)}) {
        auto carried = interfaceCarriesIdentity(identity);
        PHPVPN_REQUIRE_OK(carried);
        PHPVPN_CHECK(!carried.value());
        PHPVPN_REQUIRE_ERROR(interfaceNameForIdentity(identity), kInterfaceAbsentError);
    }
}

PHPVPN_TEST(MacInterfaceIdentity, InvalidNamesAndDescriptionsAreRejected) {
    PHPVPN_REQUIRE_ERROR(interfaceIdentityForName(""), ErrorCode::invalid_argument);
    PHPVPN_REQUIRE_ERROR(interfaceIdentityForName("a-very-long-name0"), ErrorCode::invalid_argument);
    PHPVPN_REQUIRE_ERROR(readInterfaceDescription(std::string("lo0") + std::string(1, '\0')),
                         ErrorCode::invalid_argument);
    // Checked before any ioctl, so nothing changes even when run as root.
    PHPVPN_REQUIRE_ERROR(writeInterfaceDescription("lo0", std::string(128, 'x')),
                         ErrorCode::invalid_argument);
    PHPVPN_REQUIRE_ERROR(writeInterfaceDescription(kMissingInterface, std::string(200, 'x')),
                         ErrorCode::invalid_argument);
}

PHPVPN_TEST(MacInterfaceIdentity, WritingADescriptionNeedsRoot) {
    if (::geteuid() == 0) {
        std::cout << "    skipped: running as root, lo0 is left untouched\n";
        return;
    }
    PHPVPN_REQUIRE_ERROR(writeInterfaceDescription("lo0", kMarker), ErrorCode::netconfig_apply_failed);
    auto tag = ownerTagOfInterface("lo0");
    PHPVPN_REQUIRE_OK(tag);
    PHPVPN_CHECK_EQ(tag.value(), 0u);
    PHPVPN_REQUIRE_ERROR(writeInterfaceDescription(kMissingInterface, kMarker), kInterfaceAbsentError);
}

}  // namespace phpvpn
