#include "harness.h"

#include <string>

#include "core/base/utf8.h"

namespace phpvpn {
namespace {

// "a", U+00E9, U+20AC, U+1F600 and U+10FFFF: one sequence of every length plus
// the highest scalar value.
const std::string kMixedUtf8 = "a\xc3\xa9\xe2\x82\xac\xf0\x9f\x98\x80\xf4\x8f\xbf\xbf";
const std::u16string kMixedUtf16 = {u'a', 0x00e9, 0x20ac, 0xd83d, 0xde00, 0xdbff, 0xdfff};
const std::u32string kMixedUtf32 = {U'a', 0x00e9, 0x20ac, 0x1f600, 0x10ffff};

}  // namespace

PHPVPN_TEST(utf8, fixed_width_conversions_round_trip) {
    PHPVPN_CHECK_EQ(utf16ToUtf8(kMixedUtf16).value(), kMixedUtf8);
    PHPVPN_CHECK_EQ(utf32ToUtf8(kMixedUtf32).value(), kMixedUtf8);
    PHPVPN_CHECK(utf8ToUtf16(kMixedUtf8).value() == kMixedUtf16);
    PHPVPN_CHECK(utf8ToUtf32(kMixedUtf8).value() == kMixedUtf32);

    PHPVPN_CHECK_EQ(utf16ToUtf8(u"").value(), std::string());
    PHPVPN_CHECK(utf8ToUtf32("").value().empty());
    // NUL is a valid scalar value and survives unchanged.
    const std::string withNul("a\0b", 3);
    PHPVPN_CHECK(utf8ToUtf32(withNul).value() == std::u32string(U"a\0b", 3));
    PHPVPN_CHECK_EQ(utf32ToUtf8(std::u32string(U"a\0b", 3)).value(), withNul);

    // Boundaries of every sequence length.
    const std::u32string boundaries = {0x7f, 0x80, 0x7ff, 0x800, 0xd7ff, 0xe000, 0xffff, 0x10000};
    const auto encoded = utf32ToUtf8(boundaries);
    PHPVPN_REQUIRE_OK(encoded);
    PHPVPN_CHECK_EQ(encoded.value(),
                    std::string("\x7f\xc2\x80\xdf\xbf\xe0\xa0\x80\xed\x9f\xbf\xee\x80\x80\xef\xbf\xbf\xf0\x90\x80\x80"));
    PHPVPN_CHECK(utf8ToUtf32(encoded.value()).value() == boundaries);
}

PHPVPN_TEST(utf8, wide_overloads_follow_the_wchar_width) {
    const auto wide = fromUtf8(kMixedUtf8);
    PHPVPN_REQUIRE_OK(wide);
    if constexpr (sizeof(wchar_t) == 2) {
        PHPVPN_CHECK_EQ(wide.value().size(), kMixedUtf16.size());
        PHPVPN_CHECK(std::u16string(wide.value().begin(), wide.value().end()) == kMixedUtf16);
    } else {
        PHPVPN_CHECK_EQ(wide.value().size(), kMixedUtf32.size());
        PHPVPN_CHECK(std::u32string(wide.value().begin(), wide.value().end()) == kMixedUtf32);
    }
    PHPVPN_CHECK_EQ(toUtf8(wide.value()).value(), kMixedUtf8);
    // The adapter name and GUID of the ownership journal stay plain ASCII.
    PHPVPN_CHECK_EQ(toUtf8(L"PHPVPN").value(), std::string("PHPVPN"));
    PHPVPN_CHECK_EQ(toUtf8(L"{7d1b4d2f-1f34-4d0e-9a3e-2f6c9b5a41d8}").value(),
                    std::string("{7d1b4d2f-1f34-4d0e-9a3e-2f6c9b5a41d8}"));
    PHPVPN_CHECK(fromUtf8("phpvpn").value() == std::wstring(L"phpvpn"));
}

PHPVPN_TEST(utf8, malformed_utf8_is_rejected) {
    for (const std::string& bad : {
             std::string("\x80"),              // lone continuation byte
             std::string("\xbf"),              // lone continuation byte
             std::string("\xc0\xaf"),          // overlong two byte form
             std::string("\xc1\xbf"),          // overlong two byte form
             std::string("\xe0\x80\xaf"),      // overlong three byte form
             std::string("\xf0\x80\x80\xaf"),  // overlong four byte form
             std::string("\xed\xa0\x80"),      // encoded high surrogate
             std::string("\xed\xbf\xbf"),      // encoded low surrogate
             std::string("\xf4\x90\x80\x80"),  // above U+10FFFF
             std::string("\xf5\x80\x80\x80"),  // invalid lead byte
             std::string("\xff"),              // invalid lead byte
             std::string("\xc3"),              // truncated two byte sequence
             std::string("\xe2\x82"),          // truncated three byte sequence
             std::string("\xf0\x9f\x98"),      // truncated four byte sequence
             std::string("\xe2\x28\xa1"),      // missing continuation byte
             std::string("ok\xc3(")}) {        // valid prefix, then a broken sequence
        PHPVPN_REQUIRE_ERROR(utf8ToUtf16(bad), ErrorCode::invalid_argument);
        PHPVPN_REQUIRE_ERROR(utf8ToUtf32(bad), ErrorCode::invalid_argument);
        PHPVPN_REQUIRE_ERROR(fromUtf8(bad), ErrorCode::invalid_argument);
    }
}

PHPVPN_TEST(utf8, malformed_wide_text_is_rejected) {
    for (const std::u16string& bad : {
             std::u16string{0xd800},             // unpaired high surrogate at the end
             std::u16string{0xdc00},             // unpaired low surrogate
             std::u16string{0xd83d, u'a'},       // high surrogate followed by a BMP unit
             std::u16string{0xde00, 0xd83d},     // reversed pair
             std::u16string{u'a', 0xd83d, 0xd83d, 0xde00}}) {
        PHPVPN_REQUIRE_ERROR(utf16ToUtf8(bad), ErrorCode::invalid_argument);
    }
    for (const std::u32string& bad : {
             std::u32string{0xd800},
             std::u32string{0xdfff},
             std::u32string{0x110000},
             std::u32string{U'a', static_cast<char32_t>(0xffffffffu)}}) {
        PHPVPN_REQUIRE_ERROR(utf32ToUtf8(bad), ErrorCode::invalid_argument);
    }
    // The wide overload rejects whatever the platform width cannot represent.
    std::wstring loneSurrogate(1, static_cast<wchar_t>(0xd800));
    PHPVPN_REQUIRE_ERROR(toUtf8(loneSurrogate), ErrorCode::invalid_argument);
#if !defined(_WIN32)
    // Only a UTF-32 wchar_t can hold a value above U+10FFFF.
    std::wstring tooLarge(1, static_cast<wchar_t>(0x110000));
    PHPVPN_REQUIRE_ERROR(toUtf8(tooLarge), ErrorCode::invalid_argument);
#endif
}

}  // namespace phpvpn
