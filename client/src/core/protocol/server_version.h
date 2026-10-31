// Compatibility of the server application version, protocol.md chapter 5.2.
//
// The client communicates only with a server of its own MAJOR version: client
// 4.8.3 accepts server 4.1.5, but rejects 3.4.1 and 5.1.1. MINOR and PATCH are
// never compared. A `serverVersion` without a readable MAJOR is incompatible.
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "core/base/result.h"

namespace phpvpn {

inline constexpr std::size_t kMaxVersionMajorDigits = 9;
inline constexpr std::size_t kMaxDisplayedServerVersionLength = 32;

// MAJOR of a SemVer string: one or more ASCII digits without a leading zero
// (except "0" itself), followed by the end, '.', '-' or '+'. Nullopt otherwise.
constexpr std::optional<std::uint32_t> semverMajor(std::string_view version) noexcept {
    std::size_t digits = 0;
    std::uint32_t major = 0;
    while (digits < version.size() && version[digits] >= '0' && version[digits] <= '9') {
        if (digits == kMaxVersionMajorDigits) return std::nullopt;
        major = major * 10 + static_cast<std::uint32_t>(version[digits] - '0');
        ++digits;
    }
    if (digits == 0 || (digits > 1 && version[0] == '0')) return std::nullopt;
    if (digits < version.size() && version[digits] != '.' && version[digits] != '-' && version[digits] != '+')
        return std::nullopt;
    return major;
}

// The server supplied version as it may appear in a user visible text: at most
// 32 characters of [0-9A-Za-z.+-], "unrecognised" for anything else.
std::string displayableServerVersion(std::string_view version);

// Ok when both MAJOR versions are equal. Otherwise server_version_incompatible
// whose detail is displayableServerVersion(serverVersion).
Status requireCompatibleServerVersion(std::string_view serverVersion,
                                      std::string_view clientVersion = PHPVPN_CLIENT_VERSION);

// "Client version X, server version Y." for a server_version_incompatible
// error, built only from the client constant and the sanitised detail.
std::string serverVersionMismatchText(const Error& error);

}  // namespace phpvpn
