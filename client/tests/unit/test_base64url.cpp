// Canonical base64url, protocol.md chapter 6.1.

#include "core/protocol/base64url.h"

#include <string>
#include <vector>

#include "harness.h"

namespace phpvpn {
namespace {

std::vector<std::uint8_t> bytes(std::initializer_list<int> values) {
    std::vector<std::uint8_t> out;
    for (int value : values) {
        out.push_back(static_cast<std::uint8_t>(value));
    }
    return out;
}

}  // namespace

PHPVPN_TEST(base64url, encodes_without_padding) {
    PHPVPN_CHECK_EQ(Base64Url::encode(bytes({})), std::string(""));
    PHPVPN_CHECK_EQ(Base64Url::encode(bytes({0x66})), std::string("Zg"));
    PHPVPN_CHECK_EQ(Base64Url::encode(bytes({0x66, 0x6F})), std::string("Zm8"));
    PHPVPN_CHECK_EQ(Base64Url::encode(bytes({0x66, 0x6F, 0x6F})), std::string("Zm9v"));
}

PHPVPN_TEST(base64url, uses_the_url_alphabet) {
    // 0xFB 0xFF encodes to the two characters that separate the alphabets.
    PHPVPN_CHECK_EQ(Base64Url::encode(bytes({0xFB, 0xFF, 0xBF})), std::string("-_-_"));
}

PHPVPN_TEST(base64url, rejects_a_remainder_of_one) {
    PHPVPN_CHECK(!Base64Url::isCanonical("A"));
    PHPVPN_CHECK(!Base64Url::isCanonical("AAAAA"));
    PHPVPN_CHECK(Base64Url::isCanonical("AA"));
    PHPVPN_CHECK(Base64Url::isCanonical("AAA"));
    PHPVPN_CHECK(Base64Url::isCanonical("AAAA"));
}

PHPVPN_TEST(base64url, rejects_padding_whitespace_and_the_standard_alphabet) {
    PHPVPN_CHECK(!Base64Url::isCanonical("Zg=="));
    PHPVPN_CHECK(!Base64Url::isCanonical("Zm 8"));
    PHPVPN_CHECK(!Base64Url::isCanonical("Zm8\n"));
    PHPVPN_CHECK(!Base64Url::isCanonical("+m8"));
    PHPVPN_CHECK(!Base64Url::isCanonical("Zm/"));
}

PHPVPN_TEST(base64url, rejects_non_zero_unused_bits) {
    // `_w` is canonical because its unused bits are zero, `_x` is not.
    PHPVPN_CHECK(Base64Url::isCanonical("_w"));
    PHPVPN_CHECK(!Base64Url::isCanonical("_x"));
    PHPVPN_CHECK(Base64Url::isCanonical("__8"));
    PHPVPN_CHECK(!Base64Url::isCanonical("__9"));
}

PHPVPN_TEST(base64url, decodes_only_canonical_text) {
    std::vector<std::uint8_t> decoded;
    PHPVPN_CHECK(Base64Url::decode("Zm9v", decoded));
    PHPVPN_CHECK_EQ(decoded, bytes({0x66, 0x6F, 0x6F}));

    PHPVPN_CHECK(!Base64Url::decode("Zm9v=", decoded));
    PHPVPN_CHECK(!Base64Url::decode("_x", decoded));
    PHPVPN_CHECK(!Base64Url::decode("A", decoded));
}

PHPVPN_TEST(base64url, reports_the_decoded_size) {
    PHPVPN_CHECK_EQ(Base64Url::decodedSize("Zg"), std::size_t(1));
    PHPVPN_CHECK_EQ(Base64Url::decodedSize("Zm8"), std::size_t(2));
    PHPVPN_CHECK_EQ(Base64Url::decodedSize("Zm9v"), std::size_t(3));

    // A 12 byte IV is 16 characters and a 16 byte tag is 22 characters.
    PHPVPN_CHECK(Base64Url::isCanonicalWithByteLength(
        Base64Url::encode(std::vector<std::uint8_t>(12, 0)), 12));
    PHPVPN_CHECK_EQ(Base64Url::encode(std::vector<std::uint8_t>(12, 0)).size(), std::size_t(16));
    PHPVPN_CHECK_EQ(Base64Url::encode(std::vector<std::uint8_t>(16, 0)).size(), std::size_t(22));
    PHPVPN_CHECK_EQ(Base64Url::encode(std::vector<std::uint8_t>(32, 0)).size(), std::size_t(43));
}

PHPVPN_TEST(base64url, round_trips_every_single_byte) {
    for (int value = 0; value <= 255; ++value) {
        const auto original = bytes({value});
        const std::string text = Base64Url::encode(original);
        PHPVPN_CHECK(Base64Url::isCanonical(text));

        std::vector<std::uint8_t> decoded;
        PHPVPN_CHECK(Base64Url::decode(text, decoded));
        PHPVPN_CHECK_EQ(decoded, original);
    }
}

}  // namespace phpvpn
