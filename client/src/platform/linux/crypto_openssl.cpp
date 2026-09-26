// OpenSSL 3 backed CryptoProvider, plan-v001 section 4.6.
//
// Algorithm implementations are fetched once and shared, they are immutable and
// safe to use from several threads. Every operation owns its own context, which
// keeps the provider free of cross request state like the CNG provider. Freeing
// a context wipes the key schedule inside OpenSSL.
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/params.h>
#include <openssl/rand.h>

#include <algorithm>
#include <climits>
#include <memory>
#include <vector>

#include "core/crypto/crypto_provider.h"

namespace phpvpn {
namespace {

struct DigestDeleter {
    void operator()(EVP_MD* value) const noexcept { EVP_MD_free(value); }
};
struct CipherDeleter {
    void operator()(EVP_CIPHER* value) const noexcept { EVP_CIPHER_free(value); }
};
struct MacDeleter {
    void operator()(EVP_MAC* value) const noexcept { EVP_MAC_free(value); }
};
struct DigestContextDeleter {
    void operator()(EVP_MD_CTX* value) const noexcept { EVP_MD_CTX_free(value); }
};
struct CipherContextDeleter {
    void operator()(EVP_CIPHER_CTX* value) const noexcept { EVP_CIPHER_CTX_free(value); }
};
struct MacContextDeleter {
    void operator()(EVP_MAC_CTX* value) const noexcept { EVP_MAC_CTX_free(value); }
};

using DigestPtr = std::unique_ptr<EVP_MD, DigestDeleter>;
using CipherPtr = std::unique_ptr<EVP_CIPHER, CipherDeleter>;
using MacPtr = std::unique_ptr<EVP_MAC, MacDeleter>;
using DigestContext = std::unique_ptr<EVP_MD_CTX, DigestContextDeleter>;
using CipherContext = std::unique_ptr<EVP_CIPHER_CTX, CipherContextDeleter>;
using MacContext = std::unique_ptr<EVP_MAC_CTX, MacContextDeleter>;

// OpenSSL counts lengths in int. Larger inputs never occur in the protocol and
// are refused instead of being truncated.
bool fitsInt(std::size_t size) noexcept { return size <= static_cast<std::size_t>(INT_MAX); }

// OpenSSL treats a null key as "keep the previous key", so an empty key or an
// empty input still needs a valid pointer.
const unsigned char* pointer(std::span<const std::uint8_t> data) noexcept {
    static const unsigned char empty = 0;
    return data.empty() ? &empty : data.data();
}

class OpenSslCryptoProvider final : public CryptoProvider {
public:
    Status initialise() {
        sha256_.reset(EVP_MD_fetch(nullptr, "SHA256", nullptr));
        if (!sha256_) {
            return fail(ErrorCode::crypto_unavailable, "SHA-256 provider unavailable");
        }
        hmac_.reset(EVP_MAC_fetch(nullptr, "HMAC", nullptr));
        if (!hmac_) {
            return fail(ErrorCode::crypto_unavailable, "HMAC-SHA-256 provider unavailable");
        }
        aes_.reset(EVP_CIPHER_fetch(nullptr, "AES-256-GCM", nullptr));
        if (!aes_) {
            return fail(ErrorCode::crypto_unavailable, "AES provider unavailable");
        }
        if (EVP_CIPHER_get_key_length(aes_.get()) != static_cast<int>(kAesKeySize)) {
            return fail(ErrorCode::crypto_unavailable, "GCM chaining mode unavailable");
        }
        if (RAND_status() != 1) {
            return fail(ErrorCode::crypto_unavailable, "RNG provider unavailable");
        }
        return ok();
    }

    Result<Sha256Digest> sha256(std::span<const std::uint8_t> data) override {
        DigestContext context(EVP_MD_CTX_new());
        if (!context || EVP_DigestInit_ex2(context.get(), sha256_.get(), nullptr) != 1) {
            return Error(ErrorCode::crypto_failed, "SHA-256 context creation failed");
        }

        Sha256Digest digest{};
        unsigned int written = 0;
        if (EVP_DigestUpdate(context.get(), pointer(data), data.size()) != 1 ||
            EVP_DigestFinal_ex(context.get(), digest.data(), &written) != 1 ||
            written != digest.size()) {
            OPENSSL_cleanse(digest.data(), digest.size());
            return Error(ErrorCode::crypto_failed, "SHA-256 computation failed");
        }
        return digest;
    }

    Result<Sha256Digest> hmacSha256(std::span<const std::uint8_t> key,
                                    std::span<const std::uint8_t> data) override {
        MacContext context(EVP_MAC_CTX_new(hmac_.get()));
        char digestName[] = "SHA256";
        const OSSL_PARAM params[] = {
            OSSL_PARAM_construct_utf8_string("digest", digestName, 0),
            OSSL_PARAM_construct_end(),
        };
        if (!context || EVP_MAC_init(context.get(), pointer(key), key.size(), params) != 1) {
            return Error(ErrorCode::crypto_failed, "HMAC context creation failed");
        }

        Sha256Digest digest{};
        std::size_t written = 0;
        if (EVP_MAC_update(context.get(), pointer(data), data.size()) != 1 ||
            EVP_MAC_final(context.get(), digest.data(), &written, digest.size()) != 1 ||
            written != digest.size()) {
            OPENSSL_cleanse(digest.data(), digest.size());
            return Error(ErrorCode::crypto_failed, "HMAC computation failed");
        }
        return digest;
    }

    Status aesGcmSeal(std::span<const std::uint8_t> key,
                      std::span<const std::uint8_t> iv,
                      std::span<const std::uint8_t> aad,
                      std::span<const std::uint8_t> plaintext,
                      std::vector<std::uint8_t>& ciphertext,
                      GcmTag& tag) override {
        if (key.size() != kAesKeySize || iv.size() != kGcmIvSize) {
            return fail(ErrorCode::invalid_argument, "AES-GCM key or nonce length invalid");
        }
        if (!fitsInt(aad.size()) || !fitsInt(plaintext.size())) {
            return fail(ErrorCode::invalid_argument, "AES-GCM input too long");
        }

        CipherContext context(EVP_CIPHER_CTX_new());
        if (!context || EVP_EncryptInit_ex2(context.get(), aes_.get(), key.data(), iv.data(), nullptr) != 1) {
            return fail(ErrorCode::crypto_failed, "AES key import failed");
        }

        ciphertext.assign(plaintext.size(), 0);
        int written = 0;
        int total = 0;
        bool good = true;
        if (!aad.empty()) {
            good = EVP_EncryptUpdate(context.get(), nullptr, &written, aad.data(),
                                     static_cast<int>(aad.size())) == 1;
        }
        if (good && !plaintext.empty()) {
            good = EVP_EncryptUpdate(context.get(), ciphertext.data(), &written, plaintext.data(),
                                     static_cast<int>(plaintext.size())) == 1;
            total = written;
        }
        // GCM is a stream mode, the final call only computes the tag.
        std::uint8_t trailing[16] = {};
        if (good) {
            good = EVP_EncryptFinal_ex(context.get(), trailing, &written) == 1 && written == 0;
        }
        if (good) {
            good = EVP_CIPHER_CTX_ctrl(context.get(), EVP_CTRL_GCM_GET_TAG,
                                       static_cast<int>(tag.size()), tag.data()) == 1;
        }
        if (!good || static_cast<std::size_t>(total) != ciphertext.size()) {
            OPENSSL_cleanse(tag.data(), tag.size());
            ciphertext.clear();
            return fail(ErrorCode::crypto_failed, "AES-GCM encryption failed");
        }
        return ok();
    }

    Result<SecureBuffer> aesGcmOpen(std::span<const std::uint8_t> key,
                                    std::span<const std::uint8_t> iv,
                                    std::span<const std::uint8_t> aad,
                                    std::span<const std::uint8_t> ciphertext,
                                    std::span<const std::uint8_t> tag) override {
        if (key.size() != kAesKeySize || iv.size() != kGcmIvSize || tag.size() != kGcmTagSize) {
            return Error(ErrorCode::invalid_argument, "AES-GCM parameter length invalid");
        }
        if (!fitsInt(aad.size()) || !fitsInt(ciphertext.size())) {
            return Error(ErrorCode::invalid_argument, "AES-GCM input too long");
        }

        CipherContext context(EVP_CIPHER_CTX_new());
        if (!context || EVP_DecryptInit_ex2(context.get(), aes_.get(), key.data(), iv.data(), nullptr) != 1) {
            return Error(ErrorCode::crypto_failed, "AES key import failed");
        }

        // The plaintext is decrypted into a private buffer and handed out only
        // after EVP_DecryptFinal_ex verified the whole tag.
        SecureBuffer plaintext(ciphertext.size());
        int written = 0;
        int total = 0;
        bool good = true;
        if (!aad.empty()) {
            good = EVP_DecryptUpdate(context.get(), nullptr, &written, aad.data(),
                                     static_cast<int>(aad.size())) == 1;
        }
        if (good && !ciphertext.empty()) {
            good = EVP_DecryptUpdate(context.get(), plaintext.data(), &written, ciphertext.data(),
                                     static_cast<int>(ciphertext.size())) == 1;
            total = written;
        }
        if (good) {
            std::uint8_t expected[kGcmTagSize];
            std::copy(tag.begin(), tag.end(), expected);
            good = EVP_CIPHER_CTX_ctrl(context.get(), EVP_CTRL_GCM_SET_TAG,
                                       static_cast<int>(kGcmTagSize), expected) == 1;
        }
        if (!good) {
            OPENSSL_cleanse(plaintext.data(), plaintext.size());
            plaintext.clear();
            return Error(ErrorCode::crypto_failed, "AES-GCM decryption failed");
        }

        std::uint8_t trailing[16] = {};
        if (EVP_DecryptFinal_ex(context.get(), trailing, &written) != 1) {
            OPENSSL_cleanse(plaintext.data(), plaintext.size());
            plaintext.clear();
            return Error(ErrorCode::crypto_tag_mismatch, "GCM tag did not verify");
        }
        if (written != 0 || static_cast<std::size_t>(total) != plaintext.size()) {
            OPENSSL_cleanse(plaintext.data(), plaintext.size());
            plaintext.clear();
            return Error(ErrorCode::crypto_failed, "AES-GCM decryption failed");
        }
        return plaintext;
    }

    Status randomBytes(std::span<std::uint8_t> out) override {
        std::size_t offset = 0;
        while (offset < out.size()) {
            const std::size_t chunk = std::min<std::size_t>(out.size() - offset, INT_MAX);
            if (RAND_bytes(out.data() + offset, static_cast<int>(chunk)) != 1) {
                OPENSSL_cleanse(out.data(), out.size());
                return fail(ErrorCode::crypto_failed, "CSPRNG failed");
            }
            offset += chunk;
        }
        return ok();
    }

private:
    DigestPtr sha256_;
    MacPtr hmac_;
    CipherPtr aes_;
};

}  // namespace

Result<std::unique_ptr<CryptoProvider>> createPlatformCryptoProvider() {
    auto provider = std::make_unique<OpenSslCryptoProvider>();
    PHPVPN_TRY(provider->initialise());
    return std::unique_ptr<CryptoProvider>(std::move(provider));
}

}  // namespace phpvpn
