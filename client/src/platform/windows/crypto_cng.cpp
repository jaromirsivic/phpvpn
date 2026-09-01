// CNG backed CryptoProvider, plan_client.md chapter 9.
//
// Algorithm handles are opened once and shared. Key handles are created per
// operation, which keeps the provider free of cross request state.
#include <windows.h>

#include <bcrypt.h>

#include <mutex>
#include <vector>

#include "core/base/logger.h"
#include "core/crypto/crypto_provider.h"

#ifndef STATUS_SUCCESS
#define STATUS_SUCCESS ((NTSTATUS)0x00000000L)
#endif
#ifndef STATUS_AUTH_TAG_MISMATCH
#define STATUS_AUTH_TAG_MISMATCH ((NTSTATUS)0xC000A002L)
#endif

namespace phpvpn {
namespace {

bool succeeded(NTSTATUS status) noexcept { return status >= 0; }

class AlgorithmHandle {
public:
    AlgorithmHandle() = default;

    AlgorithmHandle(const AlgorithmHandle&) = delete;
    AlgorithmHandle& operator=(const AlgorithmHandle&) = delete;

    ~AlgorithmHandle() {
        if (handle_ != nullptr) {
            ::BCryptCloseAlgorithmProvider(handle_, 0);
        }
    }

    NTSTATUS open(LPCWSTR algorithm, ULONG flags) {
        return ::BCryptOpenAlgorithmProvider(&handle_, algorithm, nullptr, flags);
    }

    BCRYPT_ALG_HANDLE get() const noexcept { return handle_; }

private:
    BCRYPT_ALG_HANDLE handle_ = nullptr;
};

class KeyHandle {
public:
    KeyHandle() = default;

    KeyHandle(const KeyHandle&) = delete;
    KeyHandle& operator=(const KeyHandle&) = delete;

    ~KeyHandle() {
        if (handle_ != nullptr) {
            ::BCryptDestroyKey(handle_);
        }
        if (!object_.empty()) {
            secureWipe(object_.data(), object_.size());
        }
    }

    NTSTATUS create(BCRYPT_ALG_HANDLE algorithm,
                    ULONG objectLength,
                    std::span<const std::uint8_t> key) {
        object_.assign(objectLength, 0);
        return ::BCryptGenerateSymmetricKey(
            algorithm,
            &handle_,
            object_.empty() ? nullptr : object_.data(),
            objectLength,
            const_cast<PUCHAR>(reinterpret_cast<const UCHAR*>(key.data())),
            static_cast<ULONG>(key.size()),
            0);
    }

    BCRYPT_KEY_HANDLE get() const noexcept { return handle_; }

private:
    BCRYPT_KEY_HANDLE handle_ = nullptr;
    std::vector<std::uint8_t> object_;
};

// CNG wants a writable pointer even for input only buffers.
PUCHAR mutablePointer(std::span<const std::uint8_t> data) noexcept {
    return const_cast<PUCHAR>(reinterpret_cast<const UCHAR*>(data.data()));
}

ULONG castLength(std::size_t size) noexcept { return static_cast<ULONG>(size); }

class CngCryptoProvider final : public CryptoProvider {
public:
    Status initialise() {
        if (!succeeded(sha256_.open(BCRYPT_SHA256_ALGORITHM, 0))) {
            return fail(ErrorCode::crypto_unavailable, "SHA-256 provider unavailable");
        }
        if (!succeeded(hmac_.open(BCRYPT_SHA256_ALGORITHM, BCRYPT_ALG_HANDLE_HMAC_FLAG))) {
            return fail(ErrorCode::crypto_unavailable, "HMAC-SHA-256 provider unavailable");
        }
        if (!succeeded(aes_.open(BCRYPT_AES_ALGORITHM, 0))) {
            return fail(ErrorCode::crypto_unavailable, "AES provider unavailable");
        }
        if (!succeeded(rng_.open(BCRYPT_RNG_ALGORITHM, 0))) {
            return fail(ErrorCode::crypto_unavailable, "RNG provider unavailable");
        }

        const NTSTATUS chaining = ::BCryptSetProperty(
            aes_.get(),
            BCRYPT_CHAINING_MODE,
            reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(BCRYPT_CHAIN_MODE_GCM)),
            sizeof(BCRYPT_CHAIN_MODE_GCM),
            0);
        if (!succeeded(chaining)) {
            return fail(ErrorCode::crypto_unavailable, "GCM chaining mode unavailable");
        }

        ULONG written = 0;
        const NTSTATUS objectLength = ::BCryptGetProperty(aes_.get(),
                                                          BCRYPT_OBJECT_LENGTH,
                                                          reinterpret_cast<PUCHAR>(&aesObjectLength_),
                                                          sizeof(aesObjectLength_),
                                                          &written,
                                                          0);
        if (!succeeded(objectLength)) {
            return fail(ErrorCode::crypto_unavailable, "AES object length unavailable");
        }

        const NTSTATUS hmacObject = ::BCryptGetProperty(hmac_.get(),
                                                        BCRYPT_OBJECT_LENGTH,
                                                        reinterpret_cast<PUCHAR>(&hmacObjectLength_),
                                                        sizeof(hmacObjectLength_),
                                                        &written,
                                                        0);
        if (!succeeded(hmacObject)) {
            return fail(ErrorCode::crypto_unavailable, "HMAC object length unavailable");
        }

        const NTSTATUS shaObject = ::BCryptGetProperty(sha256_.get(),
                                                       BCRYPT_OBJECT_LENGTH,
                                                       reinterpret_cast<PUCHAR>(&shaObjectLength_),
                                                       sizeof(shaObjectLength_),
                                                       &written,
                                                       0);
        if (!succeeded(shaObject)) {
            return fail(ErrorCode::crypto_unavailable, "SHA-256 object length unavailable");
        }

        return ok();
    }

    Result<Sha256Digest> sha256(std::span<const std::uint8_t> data) override {
        std::vector<std::uint8_t> object(shaObjectLength_, 0);
        BCRYPT_HASH_HANDLE hash = nullptr;
        NTSTATUS status = ::BCryptCreateHash(sha256_.get(),
                                             &hash,
                                             object.empty() ? nullptr : object.data(),
                                             shaObjectLength_,
                                             nullptr,
                                             0,
                                             0);
        if (!succeeded(status)) {
            return Error(ErrorCode::crypto_failed, "SHA-256 context creation failed");
        }

        Sha256Digest digest{};
        status = ::BCryptHashData(hash, mutablePointer(data), castLength(data.size()), 0);
        if (succeeded(status)) {
            status = ::BCryptFinishHash(hash, digest.data(), castLength(digest.size()), 0);
        }
        ::BCryptDestroyHash(hash);
        secureWipe(object.data(), object.size());

        if (!succeeded(status)) {
            return Error(ErrorCode::crypto_failed, "SHA-256 computation failed");
        }
        return digest;
    }

    Result<Sha256Digest> hmacSha256(std::span<const std::uint8_t> key,
                                    std::span<const std::uint8_t> data) override {
        std::vector<std::uint8_t> object(hmacObjectLength_, 0);
        BCRYPT_HASH_HANDLE hash = nullptr;
        NTSTATUS status = ::BCryptCreateHash(hmac_.get(),
                                             &hash,
                                             object.empty() ? nullptr : object.data(),
                                             hmacObjectLength_,
                                             mutablePointer(key),
                                             castLength(key.size()),
                                             0);
        if (!succeeded(status)) {
            return Error(ErrorCode::crypto_failed, "HMAC context creation failed");
        }

        Sha256Digest digest{};
        status = ::BCryptHashData(hash, mutablePointer(data), castLength(data.size()), 0);
        if (succeeded(status)) {
            status = ::BCryptFinishHash(hash, digest.data(), castLength(digest.size()), 0);
        }
        ::BCryptDestroyHash(hash);
        secureWipe(object.data(), object.size());

        if (!succeeded(status)) {
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

        KeyHandle keyHandle;
        if (!succeeded(keyHandle.create(aes_.get(), aesObjectLength_, key))) {
            return fail(ErrorCode::crypto_failed, "AES key import failed");
        }

        BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO info;
        BCRYPT_INIT_AUTH_MODE_INFO(info);
        info.pbNonce = mutablePointer(iv);
        info.cbNonce = castLength(iv.size());
        info.pbAuthData = aad.empty() ? nullptr : mutablePointer(aad);
        info.cbAuthData = castLength(aad.size());
        info.pbTag = tag.data();
        info.cbTag = castLength(tag.size());

        ciphertext.assign(plaintext.size(), 0);
        ULONG written = 0;
        const NTSTATUS status = ::BCryptEncrypt(keyHandle.get(),
                                                plaintext.empty() ? nullptr : mutablePointer(plaintext),
                                                castLength(plaintext.size()),
                                                &info,
                                                nullptr,
                                                0,
                                                ciphertext.empty() ? nullptr : ciphertext.data(),
                                                castLength(ciphertext.size()),
                                                &written,
                                                0);
        if (!succeeded(status) || written != ciphertext.size()) {
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

        KeyHandle keyHandle;
        if (!succeeded(keyHandle.create(aes_.get(), aesObjectLength_, key))) {
            return Error(ErrorCode::crypto_failed, "AES key import failed");
        }

        BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO info;
        BCRYPT_INIT_AUTH_MODE_INFO(info);
        info.pbNonce = mutablePointer(iv);
        info.cbNonce = castLength(iv.size());
        info.pbAuthData = aad.empty() ? nullptr : mutablePointer(aad);
        info.cbAuthData = castLength(aad.size());
        info.pbTag = mutablePointer(tag);
        info.cbTag = castLength(tag.size());

        // BCryptDecrypt verifies the whole tag before it reports success, so the
        // buffer is handed out only after authentication.
        SecureBuffer plaintext(ciphertext.size());
        ULONG written = 0;
        const NTSTATUS status =
            ::BCryptDecrypt(keyHandle.get(),
                            ciphertext.empty() ? nullptr : mutablePointer(ciphertext),
                            castLength(ciphertext.size()),
                            &info,
                            nullptr,
                            0,
                            plaintext.empty() ? nullptr : plaintext.data(),
                            castLength(plaintext.size()),
                            &written,
                            0);
        if (status == STATUS_AUTH_TAG_MISMATCH) {
            plaintext.clear();
            return Error(ErrorCode::crypto_tag_mismatch, "GCM tag did not verify");
        }
        if (!succeeded(status) || written != plaintext.size()) {
            plaintext.clear();
            return Error(ErrorCode::crypto_failed, "AES-GCM decryption failed");
        }
        return plaintext;
    }

    Status randomBytes(std::span<std::uint8_t> out) override {
        if (out.empty()) {
            return ok();
        }
        const NTSTATUS status = ::BCryptGenRandom(nullptr,
                                                  out.data(),
                                                  castLength(out.size()),
                                                  BCRYPT_USE_SYSTEM_PREFERRED_RNG);
        if (!succeeded(status)) {
            return fail(ErrorCode::crypto_failed, "CSPRNG failed");
        }
        return ok();
    }

private:
    AlgorithmHandle sha256_;
    AlgorithmHandle hmac_;
    AlgorithmHandle aes_;
    AlgorithmHandle rng_;
    ULONG aesObjectLength_ = 0;
    ULONG hmacObjectLength_ = 0;
    ULONG shaObjectLength_ = 0;
};

}  // namespace

Result<std::unique_ptr<CryptoProvider>> createPlatformCryptoProvider() {
    auto provider = std::make_unique<CngCryptoProvider>();
    PHPVPN_TRY(provider->initialise());
    return std::unique_ptr<CryptoProvider>(std::move(provider));
}

}  // namespace phpvpn
