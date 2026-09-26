// Placeholder of the Linux platform layer, replaced by WP03 (plan-v001 section 5).
#include "platform/iface/transport.h"

namespace phpvpn {

Result<std::unique_ptr<Transport>> createPlatformTransport() {
    return Error{ErrorCode::not_supported, "transport not implemented yet"};
}

}  // namespace phpvpn
