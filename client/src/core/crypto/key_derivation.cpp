#include "core/crypto/key_derivation.h"

#include <vector>

#include "core/crypto/hkdf.h"

namespace phpvpn {
namespace {

constexpr std::string_view kAuthSemiSaltDomain = "phpvpn/auth-semi/salt";
constexpr std::string_view kAuthSemiKeyDomain = "phpvpn/auth-semi/key";
constexpr std::string_view kAuthFullSaltDomain = "phpvpn/auth-full/salt";
constexpr std::string_view kAuthFullKeyDomain = "phpvpn/auth-full/key";
constexpr std::string_view kHealthSaltDomain = "phpvpn/health-response/salt";
constexpr std::string_view kHealthKeyDomain = "phpvpn/health-response/key";
constexpr std::string_view kInfoInfoDomain = "phpvpn/info-response/";

// Joins a domain string, the single NUL separator and a value.
std::string joinWithNul(std::string_view domain, std::string_view value) {
    std::string out;
    out.reserve(domain.size() + 1 + value.size());
    out.append(domain);
    out.push_back('\0');
    out.append(value);
    return out;
}

}  // namespace

std::string normalizeServerSecret(std::string_view configured) {
    if (configured.size() == 0) {
        return std::string(32, '0');
    }
    return std::string(configured);
}

Result<SecureBuffer> KeyDerivation::semiKey(std::string_view serverSecret) {
    const std::string secret = normalizeServerSecret(serverSecret);

    Sha256Digest salt{};
    PHPVPN_TRY_ASSIGN(salt, crypto_.sha256(asBytes(kAuthSemiSaltDomain)));

    return Hkdf::derive(crypto_,
                        asBytes(secret),
                        std::span<const std::uint8_t>(salt.data(), salt.size()),
                        asBytes(kAuthSemiKeyDomain),
                        kAesKeySize);
}

Result<SecureBuffer> KeyDerivation::fullAuthKey(std::string_view serverSecret,
                                                std::span<const std::uint8_t> accessKey,
                                                std::string_view src) {
    const std::string secret = normalizeServerSecret(serverSecret);
    const std::string saltMaterial = joinWithNul(kAuthFullSaltDomain, src);
    const std::string info = joinWithNul(kAuthFullKeyDomain, src);

    Sha256Digest salt{};
    PHPVPN_TRY_ASSIGN(salt, crypto_.hmacSha256(asBytes(secret), asBytes(saltMaterial)));

    return Hkdf::derive(crypto_,
                        accessKey,
                        std::span<const std::uint8_t>(salt.data(), salt.size()),
                        asBytes(info),
                        kAesKeySize);
}

Result<SecureBuffer> KeyDerivation::healthKey(std::string_view serverSecret,
                                              std::string_view requestId) {
    const std::string secret = normalizeServerSecret(serverSecret);
    const std::string saltMaterial = joinWithNul(kHealthSaltDomain, requestId);
    const std::string info = joinWithNul(kHealthKeyDomain, requestId);

    Sha256Digest salt{};
    PHPVPN_TRY_ASSIGN(salt, crypto_.sha256(asBytes(saltMaterial)));

    return Hkdf::derive(crypto_,
                        asBytes(secret),
                        std::span<const std::uint8_t>(salt.data(), salt.size()),
                        asBytes(info),
                        kAesKeySize);
}

Result<SecureBuffer> KeyDerivation::infoKey(std::string_view serverSecret,
                                            std::span<const std::uint8_t> salt,
                                            std::string_view requestId) {
    const std::string secret = normalizeServerSecret(serverSecret);
    std::string info;
    info.reserve(kInfoInfoDomain.size() + requestId.size());
    info.append(kInfoInfoDomain);
    info.append(requestId);

    return Hkdf::derive(crypto_, asBytes(secret), salt, asBytes(info), kAesKeySize);
}

}  // namespace phpvpn
