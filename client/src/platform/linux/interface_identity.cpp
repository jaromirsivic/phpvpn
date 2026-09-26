#include "platform/linux/interface_identity.h"

#include <openssl/evp.h>

namespace phpvpn {
namespace {

constexpr std::size_t kGuidLength = 38;

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
        size = 0;
    }
    const std::uint32_t tag = (static_cast<std::uint32_t>(digest[0]) << 24) |
                              (static_cast<std::uint32_t>(digest[1]) << 16) |
                              (static_cast<std::uint32_t>(digest[2]) << 8) |
                              static_cast<std::uint32_t>(digest[3]);
    return tag == 0 ? 1u : tag;
}

std::uint32_t ownerTagForAlias(std::string_view alias) {
    if (alias.size() != kOwnershipMarkerPrefix.size() + kGuidLength ||
        alias.substr(0, kOwnershipMarkerPrefix.size()) != kOwnershipMarkerPrefix) {
        return 0;
    }
    const std::string_view guid = alias.substr(kOwnershipMarkerPrefix.size());
    if (!isCanonicalLowerGuid(guid)) return 0;
    return ownerTagForGuid(guid);
}

}  // namespace phpvpn
