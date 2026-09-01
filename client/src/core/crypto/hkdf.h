// HKDF-SHA-256 exactly as RFC 5869 defines it.
//
// plan_client.md chapter 9 keeps this in the core instead of using
// BCRYPT_HKDF_ALGORITHM, because that algorithm is not available in the same
// shape on every supported Windows build.
#pragma once

#include <cstdint>
#include <span>

#include "core/base/result.h"
#include "core/base/secure_buffer.h"
#include "core/crypto/crypto_provider.h"

namespace phpvpn {

class Hkdf {
public:
    // extract: PRK = HMAC-SHA-256(salt, ikm)
    static Result<Sha256Digest> extract(CryptoProvider& crypto,
                                        std::span<const std::uint8_t> salt,
                                        std::span<const std::uint8_t> ikm);

    // expand: T(1..n) with the single byte counter, truncated to `length`.
    static Result<SecureBuffer> expand(CryptoProvider& crypto,
                                       std::span<const std::uint8_t> prk,
                                       std::span<const std::uint8_t> info,
                                       std::size_t length);

    static Result<SecureBuffer> derive(CryptoProvider& crypto,
                                       std::span<const std::uint8_t> ikm,
                                       std::span<const std::uint8_t> salt,
                                       std::span<const std::uint8_t> info,
                                       std::size_t length);
};

}  // namespace phpvpn
