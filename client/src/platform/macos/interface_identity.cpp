// macOS interface identity: the ownership marker in the interface description.
//
// SIOCSIFDESC, SIOCGIFDESC and struct if_descreq are private to xnu
// (bsd/sys/sockio_private.h, bsd/net/if_private.h) and missing from the public
// SDK, although the kernel serves them to every process (reading needs no
// privileges, writing needs root) and ifconfig uses them for `desc`. This file
// therefore declares the request with the identical layout under its own name
// and derives the ioctl numbers from it exactly as the private headers do:
//
//   #define IF_DESCSIZE 128
//   struct if_descreq {
//       char      ifdr_name[IFNAMSIZ];
//       u_int32_t ifdr_len;               // up to IF_DESCSIZE
//       u_int8_t  ifdr_desc[IF_DESCSIZE]; // opaque data
//   };
//   #define SIOCSIFDESC _IOWR('i', 143, struct if_descreq)
//   #define SIOCGIFDESC _IOWR('i', 144, struct if_descreq)
//
// The size is encoded in the ioctl number, so a layout mismatch would be
// rejected by the kernel instead of being misread; the static_asserts pin it.
//
// Name and index lookups use if_nametoindex and if_indextoname, which report a
// missing interface as ENXIO and a system failure (getifaddrs) with its own
// errno. Lookups that combine an index with a description repeat the index
// lookup afterwards, so the description always belongs to that index.

#include "platform/macos/interface_identity.h"

#include <fcntl.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <openssl/evp.h>

#include <cerrno>
#include <climits>
#include <cstddef>
#include <cstring>
#include <optional>
#include <utility>

namespace phpvpn {
namespace {

constexpr std::size_t kGuidLength = 38;
// IF_DESCSIZE of xnu.
constexpr std::size_t kDescriptionSize = 128;
// A lookup whose interface changed underneath is repeated at most this often.
constexpr int kConsistentLookupAttempts = 3;

struct DescriptionRequest {
    char name[IFNAMSIZ];
    std::uint32_t length;
    std::uint8_t description[kDescriptionSize];
};
static_assert(offsetof(DescriptionRequest, length) == IFNAMSIZ);
static_assert(offsetof(DescriptionRequest, description) == IFNAMSIZ + sizeof(std::uint32_t));
static_assert(sizeof(DescriptionRequest) == IFNAMSIZ + sizeof(std::uint32_t) + kDescriptionSize);

constexpr unsigned long kSetDescription = _IOWR('i', 143, DescriptionRequest);
constexpr unsigned long kGetDescription = _IOWR('i', 144, DescriptionRequest);

// Owning descriptor, local so this file depends on no other platform file.
class OwnedFd {
public:
    OwnedFd() noexcept = default;
    explicit OwnedFd(int fd) noexcept : fd_(fd) {}
    OwnedFd(OwnedFd&& other) noexcept : fd_(std::exchange(other.fd_, -1)) {}
    OwnedFd& operator=(OwnedFd&& other) noexcept {
        if (this != &other) {
            reset();
            fd_ = std::exchange(other.fd_, -1);
        }
        return *this;
    }
    OwnedFd(const OwnedFd&) = delete;
    OwnedFd& operator=(const OwnedFd&) = delete;
    ~OwnedFd() { reset(); }

    int get() const noexcept { return fd_; }
    void reset() noexcept {
        if (fd_ >= 0) ::close(fd_);
        fd_ = -1;
    }

private:
    int fd_ = -1;
};

// Only the errno number, never the localized system text.
std::string errnoText(int error) { return "errno " + std::to_string(error); }

Error lookupFailed(const std::string& what, int error) {
    return Error(ErrorCode::netconfig_apply_failed, what + " (" + errnoText(error) + ")");
}

Error absent() { return Error(kInterfaceAbsentError, "the network interface does not exist"); }

bool isAbsent(const Error& error) noexcept { return error.code == kInterfaceAbsentError; }

bool isHex(char character) noexcept {
    return (character >= '0' && character <= '9') || (character >= 'a' && character <= 'f') ||
           (character >= 'A' && character <= 'F');
}

bool isCanonicalLowerGuid(std::string_view text) noexcept {
    if (text.size() != kGuidLength || text.front() != '{' || text.back() != '}') return false;
    for (std::size_t index = 1; index + 1 < text.size(); ++index) {
        const char character = text[index];
        if (index == 9 || index == 14 || index == 19 || index == 24) {
            if (character != '-') return false;
        } else if (!((character >= '0' && character <= '9') ||
                     (character >= 'a' && character <= 'f'))) {
            return false;
        }
    }
    return true;
}

// The kernel copies IFNAMSIZ bytes and terminates them itself; a longer name
// or one with an embedded NUL could silently name another interface.
Status checkInterfaceName(const std::string& name) {
    if (name.empty() || name.size() >= IFNAMSIZ || name.find('\0') != std::string::npos) {
        return Error(ErrorCode::invalid_argument, "the interface name is not a valid macOS name");
    }
    return Status::success();
}

// The socket the interface ioctls are issued on; ifioctl serves them on any
// socket, AF_INET is always present.
Result<OwnedFd> openInetSocket() {
    OwnedFd fd(::socket(AF_INET, SOCK_DGRAM, 0));
    if (fd.get() < 0) {
        return lookupFailed("the interface control socket could not be opened", errno);
    }
    // No SOCK_CLOEXEC on macOS; the descriptor is short lived, but a fork in
    // another thread must still not inherit it.
    if (::fcntl(fd.get(), F_SETFD, FD_CLOEXEC) != 0) {
        return lookupFailed("the interface control socket could not be made close-on-exec", errno);
    }
    return Result<OwnedFd>(std::move(fd));
}

void copyName(char (&target)[IFNAMSIZ], const std::string& name) noexcept {
    // checkInterfaceName guarantees name.size() < IFNAMSIZ; the rest stays 0.
    std::memcpy(target, name.data(), name.size());
}

Result<unsigned int> indexForName(const std::string& name) {
    errno = 0;
    const unsigned int index = ::if_nametoindex(name.c_str());
    if (index != 0) return index;
    const int error = errno;
    if (error == ENXIO || error == ENODEV) return absent();
    return lookupFailed("the interface index could not be resolved", error);
}

Result<std::string> nameForIndex(unsigned int index) {
    char buffer[IF_NAMESIZE + 1] = {};
    errno = 0;
    if (::if_indextoname(index, buffer) == nullptr) {
        const int error = errno;
        if (error == ENXIO || error == ENODEV) return absent();
        return lookupFailed("the interface name could not be resolved", error);
    }
    return std::string(buffer, ::strnlen(buffer, IF_NAMESIZE));
}

// The name of the interface at the ifindex of `identity` when that interface
// carries the identity's tag, nullopt when the identity is positively absent.
Result<std::optional<std::string>> nameCarryingIdentity(std::uint64_t identity) {
    const int index = identityIfindex(identity);
    // Not an ifindex at all, so no interface can carry this identity.
    if (index <= 0) return std::optional<std::string>();
    const auto ifindex = static_cast<unsigned int>(index);

    for (int attempt = 0; attempt < kConsistentLookupAttempts; ++attempt) {
        auto name = nameForIndex(ifindex);
        if (!name.ok()) {
            if (isAbsent(name.error())) return std::optional<std::string>();
            return name.error();
        }
        auto tag = ownerTagOfInterface(name.value());
        if (!tag.ok()) {
            // Renamed or removed between the two calls: look again.
            if (isAbsent(tag.error())) continue;
            return tag.error();
        }
        auto again = nameForIndex(ifindex);
        if (!again.ok()) {
            if (isAbsent(again.error())) return std::optional<std::string>();
            return again.error();
        }
        if (again.value() != name.value()) continue;
        if (tag.value() != identityOwnerTag(identity)) return std::optional<std::string>();
        return std::optional<std::string>(std::move(name).value());
    }
    return Error(ErrorCode::netconfig_apply_failed, "the network interface changed during the lookup");
}

}  // namespace

Result<std::string> canonicalInstallationGuid(std::wstring_view guid) {
    if (guid.size() != kGuidLength) {
        return Error(ErrorCode::invalid_argument, "the installation GUID is not in braced form");
    }
    std::string text;
    text.reserve(kGuidLength);
    for (wchar_t character : guid) {
        if (character < 0x20 || character > 0x7E) {
            return Error(ErrorCode::invalid_argument, "the installation GUID is not ASCII");
        }
        char narrow = static_cast<char>(character);
        if (narrow >= 'A' && narrow <= 'F') narrow = static_cast<char>(narrow - 'A' + 'a');
        if (narrow != '{' && narrow != '}' && narrow != '-' && !isHex(narrow)) {
            return Error(ErrorCode::invalid_argument, "the installation GUID is not hexadecimal");
        }
        text.push_back(narrow);
    }
    if (!isCanonicalLowerGuid(text)) {
        return Error(ErrorCode::invalid_argument, "the installation GUID is not in braced form");
    }
    return text;
}

std::string ownershipMarker(std::string_view canonicalGuid) {
    return std::string(kOwnershipMarkerPrefix) + std::string(canonicalGuid);
}

std::uint32_t ownerTagForGuid(std::string_view canonicalGuid) {
    unsigned char digest[EVP_MAX_MD_SIZE] = {};
    unsigned int size = 0;
    // SHA-256 is always available in OpenSSL 3; a failure leaves the digest
    // zeroed and the tag falls back to the reserved value 1 below.
    if (EVP_Digest(canonicalGuid.data(), canonicalGuid.size(), digest, &size, EVP_sha256(),
                   nullptr) != 1 ||
        size < 4) {
        std::memset(digest, 0, sizeof(digest));
    }
    const std::uint32_t tag = (static_cast<std::uint32_t>(digest[0]) << 24) |
                              (static_cast<std::uint32_t>(digest[1]) << 16) |
                              (static_cast<std::uint32_t>(digest[2]) << 8) |
                              static_cast<std::uint32_t>(digest[3]);
    return tag == 0 ? 1u : tag;
}

std::uint32_t ownerTagForAlias(std::string_view description) {
    if (description.size() != kOwnershipMarkerPrefix.size() + kGuidLength ||
        description.substr(0, kOwnershipMarkerPrefix.size()) != kOwnershipMarkerPrefix) {
        return 0;
    }
    const std::string_view guid = description.substr(kOwnershipMarkerPrefix.size());
    if (!isCanonicalLowerGuid(guid)) return 0;
    return ownerTagForGuid(guid);
}

Result<std::string> readInterfaceDescription(const std::string& ifname) {
    PHPVPN_TRY(checkInterfaceName(ifname));
    OwnedFd socket;
    PHPVPN_TRY_ASSIGN(socket, openInetSocket());

    DescriptionRequest request{};
    copyName(request.name, ifname);
    if (::ioctl(socket.get(), kGetDescription, &request) != 0) {
        const int error = errno;
        if (error == ENXIO || error == ENODEV) return absent();
        return lookupFailed("the interface description could not be read", error);
    }
    const std::size_t length = request.length < kDescriptionSize
                                   ? static_cast<std::size_t>(request.length)
                                   : kDescriptionSize;
    return std::string(reinterpret_cast<const char*>(request.description), length);
}

Status writeInterfaceDescription(const std::string& ifname, std::string_view description) {
    PHPVPN_TRY(checkInterfaceName(ifname));
    if (description.size() >= kDescriptionSize) {
        return Error(ErrorCode::invalid_argument, "the interface description is too long");
    }
    OwnedFd socket;
    PHPVPN_TRY_ASSIGN(socket, openInetSocket());

    DescriptionRequest request{};
    copyName(request.name, ifname);
    request.length = static_cast<std::uint32_t>(description.size());
    if (!description.empty()) {
        std::memcpy(request.description, description.data(), description.size());
    }
    if (::ioctl(socket.get(), kSetDescription, &request) != 0) {
        const int error = errno;
        if (error == ENXIO || error == ENODEV) return absent();
        if (error == EPERM || error == EACCES) {
            return lookupFailed("the interface description can only be set by root", error);
        }
        return lookupFailed("the interface description could not be set", error);
    }
    return Status::success();
}

Result<std::uint32_t> ownerTagOfInterface(const std::string& ifname) {
    std::string description;
    PHPVPN_TRY_ASSIGN(description, readInterfaceDescription(ifname));
    return ownerTagForAlias(description);
}

Result<std::uint64_t> interfaceIdentityForName(const std::string& ifname) {
    PHPVPN_TRY(checkInterfaceName(ifname));
    for (int attempt = 0; attempt < kConsistentLookupAttempts; ++attempt) {
        unsigned int index = 0;
        PHPVPN_TRY_ASSIGN(index, indexForName(ifname));
        std::uint32_t tag = 0;
        PHPVPN_TRY_ASSIGN(tag, ownerTagOfInterface(ifname));
        // The description must belong to the interface the index names.
        unsigned int again = 0;
        PHPVPN_TRY_ASSIGN(again, indexForName(ifname));
        if (again != index) continue;
        if (index > static_cast<unsigned int>(INT_MAX)) {
            return Error(ErrorCode::netconfig_apply_failed, "the interface index is out of range");
        }
        return composeInterfaceIdentity(tag, static_cast<int>(index));
    }
    return Error(ErrorCode::netconfig_apply_failed, "the network interface changed during the lookup");
}

Result<bool> interfaceCarriesIdentity(std::uint64_t identity) {
    std::optional<std::string> name;
    PHPVPN_TRY_ASSIGN(name, nameCarryingIdentity(identity));
    return name.has_value();
}

Result<std::string> interfaceNameForIdentity(std::uint64_t identity) {
    std::optional<std::string> name;
    PHPVPN_TRY_ASSIGN(name, nameCarryingIdentity(identity));
    if (!name.has_value()) {
        return Error(kInterfaceAbsentError, "the network interface no longer exists");
    }
    return std::move(*name);
}

}  // namespace phpvpn
