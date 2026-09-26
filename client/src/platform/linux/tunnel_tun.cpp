// Placeholder of the Linux platform layer, replaced by WP04 (plan-v001 section 5).
#include "platform/iface/tunnel.h"

namespace phpvpn {

std::unique_ptr<TunnelDriver> createPlatformTunnelDriver() {
    return nullptr;
}

}  // namespace phpvpn
