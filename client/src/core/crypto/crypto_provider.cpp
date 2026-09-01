#include "core/crypto/crypto_provider.h"

#include "core/protocol/uuid.h"

namespace phpvpn {

Result<std::vector<std::uint8_t>> CryptoProvider::randomVector(std::size_t size) {
    std::vector<std::uint8_t> bytes(size, 0);
    PHPVPN_TRY(randomBytes(bytes));
    return bytes;
}

Result<std::string> CryptoProvider::randomUuidV4() {
    std::uint8_t bytes[16] = {};
    PHPVPN_TRY(randomBytes(std::span<std::uint8_t>(bytes, sizeof(bytes))));
    return Uuid::formatV4(std::span<const std::uint8_t>(bytes, sizeof(bytes)));
}

}  // namespace phpvpn
