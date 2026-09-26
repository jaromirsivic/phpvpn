// Placeholder of the Linux platform layer, replaced by WP04 (plan-v001 section 5).
#include "platform/iface/netconfig.h"

namespace phpvpn {

std::unique_ptr<NetConfig> createPlatformNetConfig() {
    return nullptr;
}

}  // namespace phpvpn
