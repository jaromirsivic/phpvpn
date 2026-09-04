#include "core/runtime/status_snapshot.h"

namespace phpvpn {

const char* checkpointResultName(CheckpointResult value) noexcept {
    switch (value) {
        case CheckpointResult::success: return "success";
        case CheckpointResult::failed: return "failed";
        default: return "pending";
    }
}

SafeError SafeError::fromConnection(const Error& error, ConnectionPhase phase,
                                   bool healthSyntaxUsable) {
    // client.md: public connection errors describe the failed operation,
    // never raw transport details or an inference from historical checkpoints.
    if (error.code == ErrorCode::transport_timeout || error.code == ErrorCode::timeout)
        return {"communication_timeout", "The server did not respond in time. Verification could not be completed.", true};
    if (error.code == ErrorCode::config_invalid_url ||
        (phase == ConnectionPhase::health && !healthSyntaxUsable))
        return {"endpoint_unavailable_or_invalid", "The server endpoint is not reachable or not valid.",
                error.code != ErrorCode::config_invalid_url && error.recoverable()};
    if (phase == ConnectionPhase::starting_tunnel)
        return {"tunnel_start_failed", "The local tunnel could not be started.", error.recoverable()};
    const bool transportFailure = error.code >= ErrorCode::transport_unavailable &&
                                  error.code <= ErrorCode::transport_timeout;
    const bool serverCommunicationFailure = error.code >= ErrorCode::server_bad_request &&
                                            error.code <= ErrorCode::server_unexpected_status;
    if ((phase == ConnectionPhase::health && healthSyntaxUsable &&
         error.code >= ErrorCode::crypto_unavailable && error.code <= ErrorCode::crypto_tag_mismatch) ||
        (phase == ConnectionPhase::info && !transportFailure &&
         error.code != ErrorCode::cancelled && !serverCommunicationFailure))
        return {"server_verification_failed", "The server could not be verified with the configured SERVER_SECRET.", false};
    if (phase == ConnectionPhase::access_key_check && error.code == ErrorCode::server_unauthorized)
        return {"access_key_rejected", "The server rejected the configured ACCESS_KEY.", false};
    if (phase == ConnectionPhase::access_key_check || phase == ConnectionPhase::running ||
        phase == ConnectionPhase::info || phase == ConnectionPhase::health ||
        transportFailure || error.code == ErrorCode::cancelled)
        return {"communication_failed", "Communication with the server could not be completed.", error.recoverable()};
    return {"connection_failed", "The connection attempt failed.", error.recoverable()};
}

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
    return fromConnection(error, ConnectionPhase::idle);
}

}  // namespace phpvpn
