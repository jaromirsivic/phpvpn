// Placeholder of the Linux platform layer, replaced by WP02 (plan-v001 section 5).
#include "platform/iface/durable_file.h"

namespace phpvpn {

Status durableReplaceFile(const std::filesystem::path&, std::string_view) {
    return Error{ErrorCode::not_supported, "durable file replacement not implemented yet"};
}

}  // namespace phpvpn
