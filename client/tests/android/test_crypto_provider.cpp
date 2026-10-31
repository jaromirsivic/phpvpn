// Test-only createPlatformCryptoProvider for Android builds without the
// prebuilt OpenSSL of WP01 (PHPVPN_ANDROID_DEPS empty). The POSIX
// configuration files hash the exact bytes and draw random commit and stage
// names, nothing else, so SHA-256, HMAC-SHA-256 and the kernel random source
// are enough for the T-N03 tests; AES-GCM fails with not_supported. Never part
// of libphpvpn-android.so: tests/android/posix_tests.cmake adds this file only
// to test targets and only while the real provider is missing.
#include <sys/random.h>

#include <array>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <memory>
#include <span>
#include <vector>

#include "core/crypto/crypto_provider.h"

namespace phpvpn {
namespace {

// FIPS 180-4 SHA-256, straightforward and only for test inputs.
class Sha256 {
public:
    void update(std::span<const std::uint8_t> data) {
        for (const std::uint8_t byte : data) {
            block_[used_++] = byte;
            if (used_ == block_.size()) {
                compress();
                used_ = 0;
            }
        }
        length_ += static_cast<std::uint64_t>(data.size()) * 8u;
    }

    Sha256Digest finish() {
        const std::uint64_t bits = length_;
        const std::uint8_t one = 0x80;
        update(std::span<const std::uint8_t>(&one, 1));
        const std::uint8_t zero = 0;
        while (used_ != 56) update(std::span<const std::uint8_t>(&zero, 1));
        std::uint8_t encoded[8];
        for (int index = 0; index < 8; ++index) encoded[index] = static_cast<std::uint8_t>(bits >> (56 - 8 * index));
        update(encoded);
        Sha256Digest digest{};
        for (std::size_t index = 0; index < 8; ++index) {
            for (std::size_t shift = 0; shift < 4; ++shift)
                digest[index * 4 + shift] = static_cast<std::uint8_t>(state_[index] >> (24 - 8 * shift));
        }
        return digest;
    }

private:
    static std::uint32_t rotate(std::uint32_t value, int count) { return (value >> count) | (value << (32 - count)); }

    void compress() {
        static constexpr std::uint32_t kRound[64] = {
            0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
            0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
            0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
            0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
            0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
            0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
            0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
            0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
        std::uint32_t words[64];
        for (int index = 0; index < 16; ++index) {
            words[index] = (std::uint32_t(block_[index * 4]) << 24) | (std::uint32_t(block_[index * 4 + 1]) << 16) |
                           (std::uint32_t(block_[index * 4 + 2]) << 8) | std::uint32_t(block_[index * 4 + 3]);
        }
        for (int index = 16; index < 64; ++index) {
            const std::uint32_t s0 = rotate(words[index - 15], 7) ^ rotate(words[index - 15], 18) ^ (words[index - 15] >> 3);
            const std::uint32_t s1 = rotate(words[index - 2], 17) ^ rotate(words[index - 2], 19) ^ (words[index - 2] >> 10);
            words[index] = words[index - 16] + s0 + words[index - 7] + s1;
        }
        std::uint32_t a = state_[0], b = state_[1], c = state_[2], d = state_[3];
        std::uint32_t e = state_[4], f = state_[5], g = state_[6], h = state_[7];
        for (int index = 0; index < 64; ++index) {
            const std::uint32_t s1 = rotate(e, 6) ^ rotate(e, 11) ^ rotate(e, 25);
            const std::uint32_t choice = (e & f) ^ (~e & g);
            const std::uint32_t first = h + s1 + choice + kRound[index] + words[index];
            const std::uint32_t s0 = rotate(a, 2) ^ rotate(a, 13) ^ rotate(a, 22);
            const std::uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
            const std::uint32_t second = s0 + majority;
            h = g;
            g = f;
            f = e;
            e = d + first;
            d = c;
            c = b;
            b = a;
            a = first + second;
        }
        state_[0] += a;
        state_[1] += b;
        state_[2] += c;
        state_[3] += d;
        state_[4] += e;
        state_[5] += f;
        state_[6] += g;
        state_[7] += h;
    }

    std::array<std::uint32_t, 8> state_ = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                           0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    std::array<std::uint8_t, 64> block_{};
    std::size_t used_ = 0;
    std::uint64_t length_ = 0;
};

class TestCryptoProvider final : public CryptoProvider {
public:
    Result<Sha256Digest> sha256(std::span<const std::uint8_t> data) override {
        Sha256 hash;
        hash.update(data);
        return hash.finish();
    }

    Result<Sha256Digest> hmacSha256(std::span<const std::uint8_t> key, std::span<const std::uint8_t> data) override {
        std::array<std::uint8_t, 64> block{};
        if (key.size() > block.size()) {
            const Sha256Digest reduced = sha256(key).value();
            std::memcpy(block.data(), reduced.data(), reduced.size());
        } else if (!key.empty()) {
            std::memcpy(block.data(), key.data(), key.size());
        }
        std::array<std::uint8_t, 64> pad{};
        for (std::size_t index = 0; index < pad.size(); ++index) pad[index] = block[index] ^ 0x36;
        Sha256 inner;
        inner.update(pad);
        inner.update(data);
        const Sha256Digest innerDigest = inner.finish();
        for (std::size_t index = 0; index < pad.size(); ++index) pad[index] = block[index] ^ 0x5c;
        Sha256 outer;
        outer.update(pad);
        outer.update(innerDigest);
        return outer.finish();
    }

    Status aesGcmSeal(std::span<const std::uint8_t>, std::span<const std::uint8_t>, std::span<const std::uint8_t>,
                      std::span<const std::uint8_t>, std::vector<std::uint8_t>&, GcmTag&) override {
        return Error(ErrorCode::not_supported, "AES-GCM needs the OpenSSL provider of WP01");
    }

    Result<SecureBuffer> aesGcmOpen(std::span<const std::uint8_t>, std::span<const std::uint8_t>,
                                    std::span<const std::uint8_t>, std::span<const std::uint8_t>,
                                    std::span<const std::uint8_t>) override {
        return Error(ErrorCode::not_supported, "AES-GCM needs the OpenSSL provider of WP01");
    }

    Status randomBytes(std::span<std::uint8_t> out) override {
        std::size_t filled = 0;
        while (filled < out.size()) {
            const ssize_t count = ::getrandom(out.data() + filled, out.size() - filled, 0);
            if (count < 0) {
                if (errno == EINTR) continue;
                return Error(ErrorCode::internal, "getrandom failed");
            }
            filled += static_cast<std::size_t>(count);
        }
        return Status::success();
    }
};

}  // namespace

Result<std::unique_ptr<CryptoProvider>> createPlatformCryptoProvider() {
    return std::unique_ptr<CryptoProvider>(std::make_unique<TestCryptoProvider>());
}

}  // namespace phpvpn
