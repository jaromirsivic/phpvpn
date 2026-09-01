// Normative key derivation of protocol.md chapters 7.1 to 7.3 and 8.3.
//
//   semiKey     = HKDF(SERVER_SECRET, SHA256("phpvpn/auth-semi/salt"),
//                      "phpvpn/auth-semi/key", 32)
//   fullAuthKey = HKDF(ACCESS_KEY, HMAC(SERVER_SECRET, "phpvpn/auth-full/salt" NUL src),
//                      "phpvpn/auth-full/key" NUL src, 32)
//   healthKey   = HKDF(SERVER_SECRET, SHA256("phpvpn/health-response/salt" NUL requestId),
//                      "phpvpn/health-response/key" NUL requestId, 32)
//   infoKey     = HKDF(SERVER_SECRET, salt32, "phpvpn/info-response/" || requestId, 32)
#pragma once

#include <span>
#include <string>
#include <string_view>

#include "core/base/result.h"
#include "core/base/secure_buffer.h"
#include "core/crypto/crypto_provider.h"

namespace phpvpn {

// protocol.md chapter 3.2: only an exactly empty configured secret becomes 32
// ASCII characters `0`. The string "0" and a value made of NUL bytes are left
// untouched, so the test is on the length and never on general falsiness.
std::string normalizeServerSecret(std::string_view configured);

class KeyDerivation {
public:
    explicit KeyDerivation(CryptoProvider& crypto) : crypto_(crypto) {}

    Result<SecureBuffer> semiKey(std::string_view serverSecret);

    Result<SecureBuffer> fullAuthKey(std::string_view serverSecret,
                                     std::span<const std::uint8_t> accessKey,
                                     std::string_view src);

    Result<SecureBuffer> healthKey(std::string_view serverSecret, std::string_view requestId);

    Result<SecureBuffer> infoKey(std::string_view serverSecret,
                                 std::span<const std::uint8_t> salt,
                                 std::string_view requestId);

private:
    CryptoProvider& crypto_;
};

}  // namespace phpvpn
