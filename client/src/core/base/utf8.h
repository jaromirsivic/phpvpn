// Strict conversions between UTF-8 and the platform wide character strings.
//
// wchar_t is UTF-16 on Windows and UTF-32 on Linux, so the wide overloads pick
// the matching fixed width conversion at compile time. Both directions reject
// malformed input instead of guessing: an overlong or truncated UTF-8 sequence,
// an encoded surrogate, a code point above U+10FFFF and an unpaired UTF-16
// surrogate all fail with invalid_argument. No replacement character is ever
// produced, so a successful result always round trips.
#pragma once

#include <string>
#include <string_view>

#include "core/base/result.h"

namespace phpvpn {

Result<std::string> toUtf8(std::wstring_view text);
Result<std::wstring> fromUtf8(std::string_view text);

// Fixed width variants, also used by the tests to cover the width that the
// current platform does not use for wchar_t.
Result<std::string> utf16ToUtf8(std::u16string_view text);
Result<std::string> utf32ToUtf8(std::u32string_view text);
Result<std::u16string> utf8ToUtf16(std::string_view text);
Result<std::u32string> utf8ToUtf32(std::string_view text);

}  // namespace phpvpn
