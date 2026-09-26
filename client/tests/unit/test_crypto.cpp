// Cryptographic known answer tests and the normative key derivation.

#include <string>
#include <vector>

#include "core/base/hex.h"
#include "core/base/secure_buffer.h"
#include "core/crypto/canonical.h"
#include "core/crypto/crypto_provider.h"
#include "core/crypto/hkdf.h"
#include "core/crypto/key_derivation.h"
#include "harness.h"

namespace phpvpn {
namespace {

std::vector<std::uint8_t> fromHex(std::string_view text) {
    std::vector<std::uint8_t> out;
    const bool ok = decodeLowercaseHex(text, out);
    PHPVPN_CHECK(ok);
    return out;
}

std::string toHex(std::span<const std::uint8_t> bytes) {
    return toLowercaseHex(bytes);
}

CryptoProvider& provider() {
    static std::unique_ptr<CryptoProvider> instance = [] {
        auto created = createPlatformCryptoProvider();
        return created.ok() ? std::move(created).value() : nullptr;
    }();
    return *instance;
}

}  // namespace

PHPVPN_TEST(crypto, sha256_known_answers) {
    auto empty = provider().sha256(asBytes(""));
    PHPVPN_REQUIRE_OK(empty);
    PHPVPN_CHECK_EQ(toHex(empty.value()),
                    std::string("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));

    auto abc = provider().sha256(asBytes("abc"));
    PHPVPN_REQUIRE_OK(abc);
    PHPVPN_CHECK_EQ(toHex(abc.value()),
                    std::string("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
}

PHPVPN_TEST(crypto, hmac_sha256_rfc4231_cases) {
    // RFC 4231 test case 1.
    const auto key1 = fromHex("0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b");
    auto mac1 = provider().hmacSha256(key1, asBytes("Hi There"));
    PHPVPN_REQUIRE_OK(mac1);
    PHPVPN_CHECK_EQ(toHex(mac1.value()),
                    std::string("b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7"));

    // RFC 4231 test case 2.
    auto mac2 = provider().hmacSha256(asBytes("Jefe"), asBytes("what do ya want for nothing?"));
    PHPVPN_REQUIRE_OK(mac2);
    PHPVPN_CHECK_EQ(toHex(mac2.value()),
                    std::string("5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843"));
}

PHPVPN_TEST(crypto, hkdf_rfc5869_case_one) {
    const auto ikm = fromHex("0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b");
    const auto salt = fromHex("000102030405060708090a0b0c");
    const auto info = fromHex("f0f1f2f3f4f5f6f7f8f9");

    auto derived = Hkdf::derive(provider(), ikm, salt, info, 42);
    PHPVPN_REQUIRE_OK(derived);
    PHPVPN_CHECK_EQ(toHex(derived.value().span()),
                    std::string("3cb25f25faacd57a90434f64d0362f2a2d2d0a90cf1a5a4c5db02d56ecc4c5bf"
                                "34007208d5b887185865"));
}

PHPVPN_TEST(crypto, hkdf_rfc5869_case_three_with_empty_salt_and_info) {
    const auto ikm = fromHex("0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b");

    auto derived = Hkdf::derive(provider(), ikm, std::span<const std::uint8_t>(),
                                  std::span<const std::uint8_t>(), 42);
    PHPVPN_REQUIRE_OK(derived);
    PHPVPN_CHECK_EQ(toHex(derived.value().span()),
                    std::string("8da4e775a563c18f715f802a063c5a31b8a11f5c5ee1879ec3454e5f3c738d2d"
                                "9d201395faa4b61a96c8"));
}

PHPVPN_TEST(crypto, aes_gcm_nist_vector) {
    // NIST GCM test case 16: a 256 bit key, a 96 bit IV and a non empty AAD,
    // which is exactly the shape every PHPVPN ciphertext uses.
    const auto key = fromHex("feffe9928665731c6d6a8f9467308308feffe9928665731c6d6a8f9467308308");
    const auto iv = fromHex("cafebabefacedbaddecaf888");
    const auto aad = fromHex("feedfacedeadbeeffeedfacedeadbeefabaddad2");
    const auto plaintext = fromHex(
        "d9313225f88406e5a55909c5aff5269a86a7a9531534f7da2e4c303d8a318a721c3c0c9595"
        "6809532fcf0e2449a6b525b16aedf5aa0de657ba637b39");

    std::vector<std::uint8_t> ciphertext;
    GcmTag tag{};
    auto sealed = provider().aesGcmSeal(key, iv, aad, plaintext, ciphertext, tag);
    PHPVPN_REQUIRE_OK(sealed);
    PHPVPN_CHECK_EQ(toHex(ciphertext),
                    std::string("522dc1f099567d07f47f37a32a84427d643a8cdcbfe5c0c97598a2bd2555d1aa"
                                "8cb08e48590dbb3da7b08b1056828838c5f61e6393ba7a0abcc9f662"));
    PHPVPN_CHECK_EQ(toHex(tag), std::string("76fc6ece0f4e1768cddf8853bb2d551b"));

    auto opened = provider().aesGcmOpen(key, iv, aad, ciphertext, tag);
    PHPVPN_REQUIRE_OK(opened);
    PHPVPN_CHECK_EQ(toHex(opened.value().span()), toHex(plaintext));
}

PHPVPN_TEST(crypto, aes_gcm_refuses_a_modified_tag) {
    const auto key = fromHex("feffe9928665731c6d6a8f9467308308feffe9928665731c6d6a8f9467308308");
    const auto iv = fromHex("cafebabefacedbaddecaf888");

    std::vector<std::uint8_t> ciphertext;
    GcmTag tag{};
    auto sealed = provider().aesGcmSeal(key, iv, asBytes("aad"), asBytes("payload"), ciphertext,
                                        tag);
    PHPVPN_REQUIRE_OK(sealed);

    GcmTag broken = tag;
    broken[0] = static_cast<std::uint8_t>(broken[0] ^ 0x01);
    auto opened = provider().aesGcmOpen(key, iv, asBytes("aad"), ciphertext, broken);
    PHPVPN_REQUIRE_ERROR(opened, ErrorCode::crypto_tag_mismatch);

    // A different AAD must fail for exactly the same reason.
    auto wrongAad = provider().aesGcmOpen(key, iv, asBytes("other"), ciphertext, tag);
    PHPVPN_REQUIRE_ERROR(wrongAad, ErrorCode::crypto_tag_mismatch);
}

PHPVPN_TEST(crypto, aes_gcm_supports_the_empty_plaintext_gmac) {
    const auto key = fromHex("0000000000000000000000000000000000000000000000000000000000000000");
    const auto iv = fromHex("000000000000000000000000");

    std::vector<std::uint8_t> ciphertext;
    GcmTag tag{};
    auto sealed = provider().aesGcmSeal(key, iv, std::span<const std::uint8_t>(),
                                        std::span<const std::uint8_t>(), ciphertext, tag);
    PHPVPN_REQUIRE_OK(sealed);
    PHPVPN_CHECK(ciphertext.empty());
    PHPVPN_CHECK_EQ(toHex(tag), std::string("530f8afbc74536b9a963b4f1c4cb738b"));

    // GCM test case 13 in the opening direction: only the tag authenticates.
    auto opened = provider().aesGcmOpen(key, iv, std::span<const std::uint8_t>(),
                                        std::span<const std::uint8_t>(), tag);
    PHPVPN_REQUIRE_OK(opened);
    PHPVPN_CHECK(opened.value().empty());
}

// Protocol vectors of tests-integration/test_crypto_vectors.py, taken over as data.
PHPVPN_TEST(crypto, aes_gcm_single_block_vector) {
    // GCM test case 14, one block of zeros under a zero key.
    const std::vector<std::uint8_t> key(32, 0);
    const std::vector<std::uint8_t> iv(12, 0);
    const std::vector<std::uint8_t> plaintext(16, 0);
    std::vector<std::uint8_t> ciphertext;
    GcmTag tag{};
    PHPVPN_REQUIRE_OK(provider().aesGcmSeal(key, iv, std::span<const std::uint8_t>(), plaintext,
                                            ciphertext, tag));
    PHPVPN_CHECK_EQ(toHex(ciphertext), std::string("cea7403d4d606b6e074ec5d3baf39d18"));
    PHPVPN_CHECK_EQ(toHex(tag), std::string("d0d1c8a799996bf0265b98b5d48ab919"));
}

PHPVPN_TEST(crypto, aes_gcm_rejects_a_single_bit_change_anywhere) {
    // Chapter 16.3.17, one changed bit in any input invalidates the decryption.
    const auto key = fromHex(std::string(64, '1'));
    const auto iv = fromHex("222222222222222222222222");
    const auto aad = std::vector<std::uint8_t>{'P', 'H', 'P', 'V', 'P', 'N', '-', 'D', 'A', 'T', 'A'};
    std::vector<std::uint8_t> ciphertext;
    GcmTag tag{};
    PHPVPN_REQUIRE_OK(provider().aesGcmSeal(key, iv, aad, asBytes("payload"), ciphertext, tag));
    for (int position = 0; position < 5; ++position) {
        auto k = key;
        auto n = iv;
        auto a = aad;
        auto c = ciphertext;
        std::vector<std::uint8_t> t(tag.begin(), tag.end());
        std::vector<std::uint8_t>* target[] = {&k, &n, &a, &c, &t};
        auto& bytes = *target[position];
        const std::size_t index = position == 4 ? bytes.size() - 1 : 0;
        bytes[index] = static_cast<std::uint8_t>(bytes[index] ^ 0x01);
        auto opened = provider().aesGcmOpen(k, n, a, c, t);
        PHPVPN_REQUIRE_ERROR(opened, ErrorCode::crypto_tag_mismatch);
    }
    auto intact = provider().aesGcmOpen(key, iv, aad, ciphertext, tag);
    PHPVPN_REQUIRE_OK(intact);
    PHPVPN_CHECK_EQ(toString(intact.value().span()), std::string("payload"));
}

PHPVPN_TEST(crypto, protocol_key_derivation_vectors) {
    // Chapters 7.1 to 7.3 and 8.3 with the inputs of test_crypto_vectors.py.
    KeyDerivation keys(provider());
    auto semi = keys.semiKey("integration secret");
    PHPVPN_REQUIRE_OK(semi);
    PHPVPN_CHECK_EQ(toHex(semi.value().span()),
                    std::string("3cfb86564bf21cb6b3ae31ba4722cb7e9727dad5bab45823da615d6f83c2aa28"));

    const auto accessKey = fromHex(
        "a1a1a1a1a1a1a1a1a1a1a1a1a1a1a1a1a1a1a1a1a1a1a1a1a1a1a1a1a1a1a1a1");
    auto fullAuth = keys.fullAuthKey("integration secret", accessKey, "10.99.99.2");
    PHPVPN_REQUIRE_OK(fullAuth);
    PHPVPN_CHECK_EQ(toHex(fullAuth.value().span()),
                    std::string("c7a1a8dffdd99030f99cb9e99ec874edbc97bd3499df79bb2259d505eb35bcc8"));

    auto health = keys.healthKey("integration secret", "c22cd6e8-14a9-4d83-97b7-54c26a72d1f0");
    PHPVPN_REQUIRE_OK(health);
    PHPVPN_CHECK_EQ(toHex(health.value().span()),
                    std::string("ffd2df7435063418edae4d09411f9516d022fe0b753f8e6bdb763906d19ab6b8"));

    const auto infoSalt = fromHex(
        "5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a");
    auto infoKey = keys.infoKey("integration secret", infoSalt, "4e94de3a-b9ad-4bc3-9297-82b98a19a143");
    PHPVPN_REQUIRE_OK(infoKey);
    PHPVPN_CHECK_EQ(toHex(infoKey.value().span()),
                    std::string("ddb0d9b5feed2bd769ba322df28db9f2098904f62542f0b0c283f222df85169d"));

    // Chapter 16.3.15, only an exactly empty secret is normalized.
    auto semiEmpty = keys.semiKey("");
    auto semiDefault = keys.semiKey("CHANGE_THIS_SERVER_SECRET");
    auto semiZero = keys.semiKey("0");
    PHPVPN_REQUIRE_OK(semiEmpty);
    PHPVPN_REQUIRE_OK(semiDefault);
    PHPVPN_REQUIRE_OK(semiZero);
    PHPVPN_CHECK_EQ(toHex(semiEmpty.value().span()),
                    std::string("b4eacc4bdd452fbe309ecf9ca9597c0343ac71d12db83fd304383869c03b4128"));
    PHPVPN_CHECK_EQ(toHex(semiDefault.value().span()), toHex(semiEmpty.value().span()));
    PHPVPN_CHECK_NE(toHex(semiZero.value().span()), toHex(semiEmpty.value().span()));
    auto fullEmpty = keys.fullAuthKey("", std::vector<std::uint8_t>(32, 0), "10.99.99.2");
    PHPVPN_REQUIRE_OK(fullEmpty);
    PHPVPN_CHECK_EQ(toHex(fullEmpty.value().span()),
                    std::string("e048dd66f434176cd86aa82360723a1c609908e6930303c9b961592fe581353c"));
}

PHPVPN_TEST(crypto, server_secret_normalisation_is_length_based) {
    // Only an exactly empty secret becomes 32 ASCII zeroes.
    PHPVPN_CHECK_EQ(normalizeServerSecret(""), std::string("CHANGE_THIS_SERVER_SECRET"));
    PHPVPN_CHECK_EQ(normalizeServerSecret(std::string(32, '0')), std::string(32, '0'));
    PHPVPN_CHECK_EQ(normalizeServerSecret(std::string(64, '0')), std::string(64, '0'));
    PHPVPN_CHECK_EQ(normalizeServerSecret("0"), std::string("0"));
    PHPVPN_CHECK_EQ(normalizeServerSecret(std::string(2, '\0')), std::string(2, '\0'));
    PHPVPN_CHECK_EQ(normalizeServerSecret("secret"), std::string("secret"));
}

PHPVPN_TEST(crypto, derived_keys_are_stable_and_bound_to_their_inputs) {
    KeyDerivation keys(provider());

    auto semiA = keys.semiKey("secret");
    auto semiB = keys.semiKey("secret");
    auto semiC = keys.semiKey("other");
    PHPVPN_REQUIRE_OK(semiA);
    PHPVPN_REQUIRE_OK(semiB);
    PHPVPN_REQUIRE_OK(semiC);
    PHPVPN_CHECK_EQ(semiA.value().size(), std::size_t(32));
    PHPVPN_CHECK_EQ(toHex(semiA.value().span()), toHex(semiB.value().span()));
    PHPVPN_CHECK_NE(toHex(semiA.value().span()), toHex(semiC.value().span()));

    const auto accessKey =
        fromHex("0102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f20");
    auto fullA = keys.fullAuthKey("secret", accessKey, "10.99.99.2");
    auto fullB = keys.fullAuthKey("secret", accessKey, "10.99.99.3");
    PHPVPN_REQUIRE_OK(fullA);
    PHPVPN_REQUIRE_OK(fullB);
    PHPVPN_CHECK_EQ(fullA.value().size(), std::size_t(32));
    // The source address enters both the salt and the info, so a different
    // client address must yield a different key.
    PHPVPN_CHECK_NE(toHex(fullA.value().span()), toHex(fullB.value().span()));

    auto healthA = keys.healthKey("secret", "8a1f9a26-6d0c-4c58-9f1d-2f0a7f6b1c30");
    auto healthB = keys.healthKey("secret", "8a1f9a26-6d0c-4c58-9f1d-2f0a7f6b1c31");
    PHPVPN_REQUIRE_OK(healthA);
    PHPVPN_REQUIRE_OK(healthB);
    PHPVPN_CHECK_NE(toHex(healthA.value().span()), toHex(healthB.value().span()));

    const auto salt = fromHex("101112131415161718191a1b1c1d1e1f202122232425262728292a2b2c2d2e2f");
    auto infoA = keys.infoKey("secret", salt, "8a1f9a26-6d0c-4c58-9f1d-2f0a7f6b1c30");
    auto infoB = keys.infoKey("secret", salt, "8a1f9a26-6d0c-4c58-9f1d-2f0a7f6b1c31");
    PHPVPN_REQUIRE_OK(infoA);
    PHPVPN_REQUIRE_OK(infoB);
    PHPVPN_CHECK_NE(toHex(infoA.value().span()), toHex(infoB.value().span()));
}

PHPVPN_TEST(crypto, canonical_strings_use_a_single_line_feed) {
    // protocol.md chapter 8.1 pins the whole block, not only the separators.
    const std::string semi = authSemiCanonical("2024-01-01T00:00:00.000Z",
                                               "8a1f9a26-6d0c-4c58-9f1d-2f0a7f6b1c30");
    PHPVPN_CHECK_EQ(semi, std::string("PHPVPN-AUTH-SEMI\n"
                                      "POST\n"
                                      "info\n"
                                      "2024-01-01T00:00:00.000Z\n"
                                      "8a1f9a26-6d0c-4c58-9f1d-2f0a7f6b1c30"));
    PHPVPN_CHECK(semi.back() != '\n');

    const std::string full = authFullCanonical("10.99.99.2", "2024-01-01T00:00:00.000Z",
                                               "8a1f9a26-6d0c-4c58-9f1d-2f0a7f6b1c30",
                                               std::string(64, 'a'));
    PHPVPN_CHECK_EQ(full, std::string("PHPVPN-AUTH-FULL\n"
                                      "POST\n"
                                      "data\n"
                                      "10.99.99.2\n"
                                      "2024-01-01T00:00:00.000Z\n"
                                      "8a1f9a26-6d0c-4c58-9f1d-2f0a7f6b1c30\n") +
                              std::string(64, 'a'));
    PHPVPN_CHECK(full.back() != '\n');
}

PHPVPN_TEST(crypto, the_random_source_produces_distinct_values) {
    auto first = provider().randomVector(32);
    auto second = provider().randomVector(32);
    PHPVPN_REQUIRE_OK(first);
    PHPVPN_REQUIRE_OK(second);
    PHPVPN_CHECK_NE(toHex(first.value()), toHex(second.value()));

    auto uuid = provider().randomUuidV4();
    PHPVPN_REQUIRE_OK(uuid);
    PHPVPN_CHECK_EQ(uuid.value().size(), std::size_t(36));
}

PHPVPN_TEST(crypto, constant_time_comparison_is_length_aware) {
    PHPVPN_CHECK(constantTimeEquals(asBytes("abc"), asBytes("abc")));
    PHPVPN_CHECK(!constantTimeEquals(asBytes("abc"), asBytes("abd")));
    PHPVPN_CHECK(!constantTimeEquals(asBytes("abc"), asBytes("abcd")));
    PHPVPN_CHECK(constantTimeEquals(std::string_view(""), std::string_view("")));
}

PHPVPN_TEST(crypto, secure_buffer_clears_on_demand) {
    SecureBuffer buffer(asBytes("sensitive"));
    PHPVPN_CHECK_EQ(buffer.size(), std::size_t(9));
    buffer.clear();
    PHPVPN_CHECK(buffer.empty());
}

}  // namespace phpvpn
