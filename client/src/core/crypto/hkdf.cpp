#include "core/crypto/hkdf.h"

#include <vector>

namespace phpvpn {

Result<Sha256Digest> Hkdf::extract(CryptoProvider& crypto,
                                   std::span<const std::uint8_t> salt,
                                   std::span<const std::uint8_t> ikm) {
    // RFC 5869 uses the salt as the HMAC key and the input keying material as
    // the message. An absent salt is a string of HashLen zeros.
    if (salt.empty()) {
        const std::uint8_t zeroSalt[kSha256Size] = {};
        return crypto.hmacSha256(std::span<const std::uint8_t>(zeroSalt, kSha256Size), ikm);
    }
    return crypto.hmacSha256(salt, ikm);
}

Result<SecureBuffer> Hkdf::expand(CryptoProvider& crypto,
                                  std::span<const std::uint8_t> prk,
                                  std::span<const std::uint8_t> info,
                                  std::size_t length) {
    if (length == 0 || length > 255 * kSha256Size) {
        return Error(ErrorCode::invalid_argument, "HKDF output length out of range");
    }

    SecureBuffer output(length);
    SecureBuffer previous;
    std::vector<std::uint8_t> block;
    std::size_t produced = 0;
    std::uint8_t counter = 1;

    while (produced < length) {
        block.clear();
        block.reserve(previous.size() + info.size() + 1);
        block.insert(block.end(), previous.data(), previous.data() + previous.size());
        block.insert(block.end(), info.begin(), info.end());
        block.push_back(counter);

        Sha256Digest digest{};
        PHPVPN_TRY_ASSIGN(digest, crypto.hmacSha256(prk, block));

        const std::size_t take = (length - produced) < kSha256Size ? (length - produced) : kSha256Size;
        for (std::size_t i = 0; i < take; ++i) {
            output[produced + i] = digest[i];
        }
        produced += take;

        previous.assign(std::span<const std::uint8_t>(digest.data(), digest.size()));
        secureWipe(digest.data(), digest.size());
        ++counter;
    }

    secureWipe(block.data(), block.size());
    return output;
}

Result<SecureBuffer> Hkdf::derive(CryptoProvider& crypto,
                                  std::span<const std::uint8_t> ikm,
                                  std::span<const std::uint8_t> salt,
                                  std::span<const std::uint8_t> info,
                                  std::size_t length) {
    Sha256Digest prk{};
    PHPVPN_TRY_ASSIGN(prk, extract(crypto, salt, ikm));
    auto result = expand(crypto, std::span<const std::uint8_t>(prk.data(), prk.size()), info, length);
    secureWipe(prk.data(), prk.size());
    return result;
}

}  // namespace phpvpn
