// Platform independent cryptography boundary.
//
// The core never talks to CNG, OpenSSL or any other library directly. The
// Windows implementation lives in platform/windows/crypto_cng.cpp. The pure PHP
// AES-GCM requirement of protocol.md chapter 7.4 applies to PHP implementations
// only, so the native client uses the platform provider.
#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "core/base/result.h"
#include "core/base/secure_buffer.h"

namespace phpvpn {

inline constexpr std::size_t kSha256Size = 32;
inline constexpr std::size_t kAesKeySize = 32;
inline constexpr std::size_t kGcmIvSize = 12;
inline constexpr std::size_t kGcmTagSize = 16;

using Sha256Digest = std::array<std::uint8_t, kSha256Size>;
using GcmTag = std::array<std::uint8_t, kGcmTagSize>;

class CryptoProvider {
public:
    virtual ~CryptoProvider() = default;

    virtual Result<Sha256Digest> sha256(std::span<const std::uint8_t> data) = 0;

    virtual Result<Sha256Digest> hmacSha256(std::span<const std::uint8_t> key,
                                            std::span<const std::uint8_t> data) = 0;

    // AES-256-GCM with a 12 byte nonce and a 16 byte tag. An empty plaintext is
    // valid and produces the GMAC construction used by the `health` response.
    virtual Status aesGcmSeal(std::span<const std::uint8_t> key,
                              std::span<const std::uint8_t> iv,
                              std::span<const std::uint8_t> aad,
                              std::span<const std::uint8_t> plaintext,
                              std::vector<std::uint8_t>& ciphertext,
                              GcmTag& tag) = 0;

    // Returns the plaintext only after the whole tag verified, so no unverified
    // byte can ever reach the caller.
    virtual Result<SecureBuffer> aesGcmOpen(std::span<const std::uint8_t> key,
                                            std::span<const std::uint8_t> iv,
                                            std::span<const std::uint8_t> aad,
                                            std::span<const std::uint8_t> ciphertext,
                                            std::span<const std::uint8_t> tag) = 0;

    virtual Status randomBytes(std::span<std::uint8_t> out) = 0;

    // Convenience helpers shared by every implementation.
    Result<std::vector<std::uint8_t>> randomVector(std::size_t size);
    Result<std::string> randomUuidV4();
};

// Created by the platform layer. Fails when the platform provider cannot be
// initialised, which is a fatal, non recoverable condition.
Result<std::unique_ptr<CryptoProvider>> createPlatformCryptoProvider();

}  // namespace phpvpn
