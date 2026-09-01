#include "platform/iface/transport.h"

namespace phpvpn {

const char* httpActionName(HttpAction action) noexcept {
    switch (action) {
        case HttpAction::health:
            return "health";
        case HttpAction::info:
            return "info";
        case HttpAction::data:
            return "data";
    }
    return "health";
}

const char* httpActionContentType(HttpAction action) noexcept {
    // protocol.md chapters 5.2, 6 and 9.1: `data` is the only NDJSON action.
    switch (action) {
        case HttpAction::health:
        case HttpAction::info:
            return "application/json; charset=utf-8";
        case HttpAction::data:
            return "application/x-ndjson; charset=utf-8";
    }
    return "application/json; charset=utf-8";
}

}  // namespace phpvpn
