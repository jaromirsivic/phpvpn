// Placeholder of the Linux platform layer, replaced by WP02 (plan-v001 section 5).
#include "core/crypto/crypto_provider.h"

namespace phpvpn {

Result<std::unique_ptr<CryptoProvider>> createPlatformCryptoProvider() {
    return Error{ErrorCode::not_supported, "crypto provider not implemented yet"};
}

}  // namespace phpvpn
