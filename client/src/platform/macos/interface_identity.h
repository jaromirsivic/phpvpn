// Platform identity of a macOS network interface, carried in the
// `interfaceLuid` fields of the platform interfaces (plan-macos-v001,
// limitation 1; the same scheme as platform/linux/interface_identity.h).
//
// identity = (uint64(ownerTag) << 32) | ifindex
//
// ownerTag is the first four bytes (big endian) of SHA-256 over the canonical
// installation GUID. macOS has no ifalias, so the ownership marker
// "phpvpn-owner:<GUID>" lives in the interface description
// (SIOCSIFDESC/SIOCGIFDESC, `ifconfig <if> desc`). The tag is non-zero only for
// an interface whose description is exactly that marker; foreign interfaces
// have tag 0. The kernel clears the description when an interface is
// detached, so when the owned utun disappears and another interface later
// receives the same ifindex, the recorded identity no longer exists and cannot
// produce a false match.
//
// Lookup results of the live functions below:
// - success: the interface was found and read;
// - kInterfaceAbsentError: positively absent, no interface of that name or
//   index exists (ENXIO from the kernel or from if_nametoindex/if_indextoname),
//   or, for the identity based lookups, the index no longer carries the tag;
// - invalid_argument: a name that can never be an interface name;
// - netconfig_apply_failed: every other failure (socket, ioctl, getifaddrs),
//   never to be read as absence.
// interfaceCarriesIdentity reports absence as false instead of an error.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "core/base/result.h"

namespace phpvpn {

inline constexpr std::string_view kOwnershipMarkerPrefix = "phpvpn-owner:";

// Error code of a positively absent interface. No other failure of the
// lookups below uses it, so callers can test `error().code` against it.
inline constexpr ErrorCode kInterfaceAbsentError = ErrorCode::tunnel_adapter_failed;

// Accepts only the braced 38 character form {xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx}
// and returns it with lower case hexadecimal digits.
Result<std::string> canonicalInstallationGuid(std::wstring_view guid);

// "phpvpn-owner:" followed by the canonical GUID.
std::string ownershipMarker(std::string_view canonicalGuid);

// First four bytes of SHA-256 over the canonical GUID, never 0: the practically
// impossible zero digest prefix maps to 1 so an owned identity stays distinct
// from every foreign one.
std::uint32_t ownerTagForGuid(std::string_view canonicalGuid);

// Tag of an interface description: non-zero only when the description is
// exactly an ownership marker with a canonical GUID.
std::uint32_t ownerTagForAlias(std::string_view description);

constexpr std::uint64_t composeInterfaceIdentity(std::uint32_t ownerTag, int ifindex) noexcept {
    return (static_cast<std::uint64_t>(ownerTag) << 32) | static_cast<std::uint32_t>(ifindex);
}
constexpr std::uint32_t identityOwnerTag(std::uint64_t identity) noexcept {
    return static_cast<std::uint32_t>(identity >> 32);
}
constexpr int identityIfindex(std::uint64_t identity) noexcept {
    return static_cast<int>(static_cast<std::uint32_t>(identity & 0xFFFFFFFFu));
}

// Description of the interface (SIOCGIFDESC on an AF_INET datagram socket, no
// privileges needed). Empty when the interface has none. The bytes are
// returned exactly as stored, at most IF_DESCSIZE (128).
Result<std::string> readInterfaceDescription(const std::string& ifname);

// Replaces the description (SIOCSIFDESC, root only). At most 127 bytes, so
// `ifconfig` always shows a terminated string; an empty description clears it.
Status writeInterfaceDescription(const std::string& ifname, std::string_view description);

// Owner tag of the interface description, 0 when the interface has no
// description or a foreign one.
Result<std::uint32_t> ownerTagOfInterface(const std::string& ifname);

// Identity of the interface with that name (if_nametoindex plus the tag of its
// description, tag 0 when unmarked). An unknown name is kInterfaceAbsentError.
Result<std::uint64_t> interfaceIdentityForName(const std::string& ifname);

// True when an interface with the ifindex of `identity` exists and its
// description tag equals identityOwnerTag(identity) (for a foreign identity:
// tag 0, an unmarked description). False when the index is gone, cannot be an
// ifindex, or carries another tag. Errors only for lookup failures.
Result<bool> interfaceCarriesIdentity(std::uint64_t identity);

// Name of the interface behind `identity` (if_indextoname), verified to carry
// the identity's tag like interfaceCarriesIdentity. kInterfaceAbsentError when
// the index is gone or no longer carries the tag.
Result<std::string> interfaceNameForIdentity(std::uint64_t identity);

}  // namespace phpvpn
