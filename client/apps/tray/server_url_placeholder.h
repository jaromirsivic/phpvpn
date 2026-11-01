#pragma once

#include "core/config/config.h"

#include <algorithm>
#include <cctype>
#include <string_view>

namespace phpvpn::tray {

// True when the draft still carries the placeholder host of the default
// configuration (kPlaceholderServerHost). A URL that parses is matched by
// host, so scheme, port, path and letter case do not matter; text that does
// not parse falls back to a case-insensitive substring search.
inline bool mentionsPlaceholderServerUrl(std::string_view text) {
    if (const auto parsed = parseServerUrl(text); parsed.ok()) return isPlaceholderServerUrl(parsed.value());
    const auto host = kPlaceholderServerHost;
    const auto sameLetter = [](char a, char b) {
        return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
    };
    return std::search(text.begin(), text.end(), host.begin(), host.end(), sameLetter) != text.end();
}

}  // namespace phpvpn::tray
