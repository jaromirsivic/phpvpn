#include "core/runtime/status_snapshot.h"

namespace phpvpn {

const char* connectionStateName(ConnectionState state) noexcept {
    switch (state) {
        case ConnectionState::disconnected:
            return "disconnected";
        case ConnectionState::connecting:
            return "connecting";
        case ConnectionState::connected:
            return "connected";
        case ConnectionState::recovering:
            return "recovering";
        case ConnectionState::error:
            return "error";
    }
    return "disconnected";
}

const char* connectionPhaseName(ConnectionPhase phase) noexcept {
    switch (phase) {
        case ConnectionPhase::idle:
            return "idle";
        case ConnectionPhase::health:
            return "health";
        case ConnectionPhase::info:
            return "info";
        case ConnectionPhase::access_key_check:
            return "access-key-check";
        case ConnectionPhase::starting_tunnel:
            return "starting-tunnel";
        case ConnectionPhase::running:
            return "running";
        case ConnectionPhase::stopping:
            return "stopping";
    }
    return "idle";
}

const char* tristateName(Tristate value) noexcept {
    switch (value) {
        case Tristate::unknown:
            return "unknown";
        case Tristate::yes:
            return "yes";
        case Tristate::no:
            return "no";
    }
    return "unknown";
}

SafeError SafeError::from(const Error& error) {
    SafeError safe;
    safe.code = error.name();
    // Only the curated description is exposed; the internal detail stays in the
    // local log so no system message or path can leak through IPC.
    safe.description = errorCodeDescription(error.code);
    safe.recoverable = error.recoverable();
    return safe;
}

}  // namespace phpvpn
