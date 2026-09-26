// OpenSSL provider specifics owned by WP02 (plan-v001 section 9.4). The known
// answers shared with Windows live in tests/unit/test_crypto.cpp.
#include "harness.h"

#include <algorithm>
#include <atomic>
#include <string>
#include <thread>
#include <vector>

#include "core/base/hex.h"
#include "core/crypto/crypto_provider.h"

namespace phpvpn {
namespace {

std::vector<std::uint8_t> bytesFromHex(std::string_view text) {
    std::vector<std::uint8_t> out;
    PHPVPN_CHECK(decodeLowercaseHex(text, out));
    return out;
}

std::unique_ptr<CryptoProvider> openSsl() {
    auto created = createPlatformCryptoProvider();
    PHPVPN_CHECK(created.ok());
    return created.ok() ? std::move(created).value() : nullptr;
}

}  // namespace

PHPVPN_TEST(crypto_openssl, digest_and_mac_edge_vectors) {
    auto crypto = openSsl();
    if (!crypto) return;
    // FIPS 180-2 two block message.
    auto twoBlocks = crypto->sha256(asBytes("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"));
    PHPVPN_REQUIRE_OK(twoBlocks);
    PHPVPN_CHECK_EQ(toLowercaseHex(twoBlocks.value()),
                    std::string("248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"));
    // An empty key must not be mistaken for "keep the previous key".
    auto emptyKey = crypto->hmacSha256(std::span<const std::uint8_t>(), std::span<const std::uint8_t>());
    PHPVPN_REQUIRE_OK(emptyKey);
    PHPVPN_CHECK_EQ(toLowercaseHex(emptyKey.value()),
                    std::string("b613679a0814d9ec772f95d778c35fc5ff1697c493715653c6c712144292c5ad"));
    // RFC 4231 test case 6, a key longer than the block size is hashed first.
    const std::vector<std::uint8_t> longKey(131, 0xaa);
    auto hashedKey = crypto->hmacSha256(longKey, asBytes("Test Using Larger Than Block-Size Key - Hash Key First"));
    PHPVPN_REQUIRE_OK(hashedKey);
    PHPVPN_CHECK_EQ(toLowercaseHex(hashedKey.value()),
                    std::string("60e431591ee0b67f0d8a26aacbf5b77f8e0bc6213728c5140546040f0ee37f54"));
}

PHPVPN_TEST(crypto_openssl, invalid_lengths_are_arguments_not_crypto_failures) {
    auto crypto = openSsl();
    if (!crypto) return;
    const std::vector<std::uint8_t> key(32, 1), shortKey(31, 1), iv(12, 2), longIv(16, 2);
    std::vector<std::uint8_t> ciphertext;
    GcmTag tag{};
    PHPVPN_REQUIRE_ERROR(crypto->aesGcmSeal(shortKey, iv, {}, asBytes("x"), ciphertext, tag),
                         ErrorCode::invalid_argument);
    PHPVPN_REQUIRE_ERROR(crypto->aesGcmSeal(key, longIv, {}, asBytes("x"), ciphertext, tag),
                         ErrorCode::invalid_argument);
    PHPVPN_REQUIRE_OK(crypto->aesGcmSeal(key, iv, {}, asBytes("x"), ciphertext, tag));
    const std::vector<std::uint8_t> shortTag(tag.begin(), tag.begin() + 15);
    PHPVPN_REQUIRE_ERROR(crypto->aesGcmOpen(key, iv, {}, ciphertext, shortTag), ErrorCode::invalid_argument);
    PHPVPN_REQUIRE_ERROR(crypto->aesGcmOpen(shortKey, iv, {}, ciphertext, tag), ErrorCode::invalid_argument);
    PHPVPN_REQUIRE_ERROR(crypto->aesGcmOpen(key, longIv, {}, ciphertext, tag), ErrorCode::invalid_argument);
}

PHPVPN_TEST(crypto_openssl, truncated_or_extended_ciphertext_is_a_tag_mismatch) {
    auto crypto = openSsl();
    if (!crypto) return;
    const auto key = bytesFromHex("feffe9928665731c6d6a8f9467308308feffe9928665731c6d6a8f9467308308");
    const auto iv = bytesFromHex("cafebabefacedbaddecaf888");
    std::vector<std::uint8_t> ciphertext;
    GcmTag tag{};
    PHPVPN_REQUIRE_OK(crypto->aesGcmSeal(key, iv, asBytes("aad"), asBytes("a longer payload"), ciphertext, tag));
    auto shorter = ciphertext;
    shorter.pop_back();
    PHPVPN_REQUIRE_ERROR(crypto->aesGcmOpen(key, iv, asBytes("aad"), shorter, tag), ErrorCode::crypto_tag_mismatch);
    auto longer = ciphertext;
    longer.push_back(0);
    PHPVPN_REQUIRE_ERROR(crypto->aesGcmOpen(key, iv, asBytes("aad"), longer, tag), ErrorCode::crypto_tag_mismatch);
    PHPVPN_REQUIRE_ERROR(crypto->aesGcmOpen(key, iv, {}, ciphertext, tag), ErrorCode::crypto_tag_mismatch);
}

PHPVPN_TEST(crypto_openssl, large_payload_round_trip_and_random_fill) {
    auto crypto = openSsl();
    if (!crypto) return;
    auto key = crypto->randomVector(32);
    auto iv = crypto->randomVector(12);
    auto payload = crypto->randomVector(1 << 20);
    PHPVPN_REQUIRE_OK(key);
    PHPVPN_REQUIRE_OK(iv);
    PHPVPN_REQUIRE_OK(payload);
    std::vector<std::uint8_t> ciphertext;
    GcmTag tag{};
    PHPVPN_REQUIRE_OK(crypto->aesGcmSeal(key.value(), iv.value(), {}, payload.value(), ciphertext, tag));
    PHPVPN_CHECK_EQ(ciphertext.size(), payload.value().size());
    PHPVPN_CHECK(ciphertext != payload.value());
    auto opened = crypto->aesGcmOpen(key.value(), iv.value(), {}, ciphertext, tag);
    PHPVPN_REQUIRE_OK(opened);
    PHPVPN_CHECK(std::vector<std::uint8_t>(opened.value().span().begin(), opened.value().span().end()) ==
                 payload.value());
    PHPVPN_REQUIRE_OK(crypto->randomBytes(std::span<std::uint8_t>()));
    // A megabyte of CSPRNG output with every byte value is overwhelmingly likely.
    std::vector<bool> seen(256, false);
    for (const auto byte : payload.value()) seen[byte] = true;
    PHPVPN_CHECK(std::find(seen.begin(), seen.end(), false) == seen.end());
}

PHPVPN_TEST(crypto_openssl, one_provider_is_safe_to_share_between_threads) {
    auto crypto = openSsl();
    if (!crypto) return;
    std::atomic<int> failures{0};
    std::vector<std::thread> threads;
    for (int t = 0; t < 8; ++t) {
        threads.emplace_back([&, t] {
            const std::vector<std::uint8_t> key(32, static_cast<std::uint8_t>(t));
            const std::vector<std::uint8_t> iv(12, static_cast<std::uint8_t>(t + 1));
            for (int i = 0; i < 200; ++i) {
                const std::string text = "packet " + std::to_string(t) + "/" + std::to_string(i);
                std::vector<std::uint8_t> ciphertext;
                GcmTag tag{};
                auto mac = crypto->hmacSha256(key, asBytes(text));
                auto digest = crypto->sha256(asBytes(text));
                if (!mac || !digest || !crypto->aesGcmSeal(key, iv, asBytes("aad"), asBytes(text), ciphertext, tag)) {
                    ++failures;
                    continue;
                }
                auto opened = crypto->aesGcmOpen(key, iv, asBytes("aad"), ciphertext, tag);
                if (!opened || toString(opened.value().span()) != text) ++failures;
            }
        });
    }
    for (auto& thread : threads) thread.join();
    PHPVPN_CHECK_EQ(failures.load(), 0);
}

}  // namespace phpvpn
