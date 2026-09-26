// Platform identity of a Linux network interface, carried in the
// `interfaceLuid` fields of the platform interfaces (plan-v001 section 4.4).
//
// identity = (uint64(ownerTag) << 32) | ifindex
//
// ownerTag is the first four bytes (big endian) of SHA-256 over the canonical
// installation GUID. The tag is non-zero only for an interface whose
// IFLA_IFALIAS is exactly the ownership marker "phpvpn-owner:<GUID>"; foreign
// interfaces have tag 0. When the owned interface disappears and a foreign one
// later receives the same ifindex, the recorded identity therefore no longer
// exists and cannot produce a false match.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "core/base/result.h"

namespace phpvpn {

inline constexpr std::string_view kOwnershipMarkerPrefix = "phpvpn-owner:";

// Accepts only the braced 38 character form {xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx}
// and returns it with lower case hexadecimal digits.
Result<std::string> canonicalInstallationGuid(std::wstring_view guid);

// "phpvpn-owner:" followed by the canonical GUID.
std::string ownershipMarker(std::string_view canonicalGuid);

// First four bytes of SHA-256 over the canonical GUID, never 0: the practically
// impossible zero digest prefix maps to 1 so an owned identity stays distinct
// from every foreign one.
std::uint32_t ownerTagForGuid(std::string_view canonicalGuid);

// Tag of an interface alias: non-zero only when the alias is exactly an
// ownership marker with a canonical GUID.
std::uint32_t ownerTagForAlias(std::string_view alias);

constexpr std::uint64_t composeInterfaceIdentity(std::uint32_t ownerTag, int ifindex) noexcept {
    return (static_cast<std::uint64_t>(ownerTag) << 32) | static_cast<std::uint32_t>(ifindex);
}
constexpr std::uint32_t identityOwnerTag(std::uint64_t identity) noexcept {
    return static_cast<std::uint32_t>(identity >> 32);
}
constexpr int identityIfindex(std::uint64_t identity) noexcept {
    return static_cast<int>(static_cast<std::uint32_t>(identity & 0xFFFFFFFFu));
}

}  // namespace phpvpn
