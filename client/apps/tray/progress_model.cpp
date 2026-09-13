#include "progress_model.h"
#include <array>
#include <limits>

namespace phpvpn::tray {
const ProgressErrorText &progressErrorText(std::string_view code) {
    static constexpr std::array<ProgressErrorText, 8> errors{
        {{"endpoint_unavailable_or_invalid", "The server endpoint is not reachable or not valid.",
          "Cannot connect to the server", "Check the Server URL and network connection, then try again."},
         {"server_verification_failed", "The server could not be verified with the configured SERVER_SECRET.",
          "Cannot verify the server", "Check the configured SERVER_SECRET and server settings."},
         {"access_key_rejected", "The server rejected the configured ACCESS_KEY.", "Cannot connect to the server",
          "Check the configured ACCESS_KEY and try again."},
         {"tunnel_start_failed", "The local tunnel could not be started.", "Cannot start the VPN tunnel",
          "Ask your administrator to check the PHPVPN service and tunnel setup."},
         {"communication_failed", "Communication with the server could not be completed.",
          "Cannot complete the connection", "Check the network and server availability, then try again."},
         {"communication_timeout", "The server did not respond in time. Verification could not be completed.",
          "Connection timed out", "Check the network and server availability, then try again."},
         {"communication_contracts_verification_failed", "Communication contracts could not be verified. Check server compatibility.", "Cannot verify communication contracts", "Check server compatibility, then reconnect."},
         {"status_unavailable", "The connection status is unavailable.", "Cannot read connection status",
          "Check that the PHPVPN service is running."}}};
    for (const auto &error : errors)
        if (error.code == code)
            return error;
    return errors[4]; // Unknown codes cannot inject text or claim key rejection.
}
void ProgressModel::onSnapshot(const TrayStatus &status, std::int64_t /*nowMs*/) {
    if (status.valid && epoch_ && status.connectionEpoch == epoch_ && attemptId_ != status.connectAttemptId) {
        diagnostic_.reset(); view_.closeRequested = true; return;
    }
    if (status.valid && epoch_ && status.connectionEpoch < epoch_) {
        // A restarted service may have a lower epoch. A foreign UUID can never
        // keep raw data alive, even when the frozen public error is retained.
        if (status.connectAttemptId != attemptId_) { diagnostic_.reset(); diagnosticRetired_ = true; }
        return;
    }
    if (!disconnect_) {
        if (!status.valid || view_.closeRequested) { diagnosticRetired_ = diagnosticRetired_ || diagnostic_.has_value(); diagnostic_.reset(); }
        else {
            if (status.connectionEpoch != epoch_) diagnosticRetired_ = false;
            if (epoch_ && status.connectionEpoch != epoch_ && view_.stage != ProgressStage::connecting)
                view_.closeRequested = true;
            epoch_ = status.connectionEpoch;
            attemptId_ = status.connectAttemptId;
            if (diagnostic_ && !status.httpDiagnostic) diagnosticRetired_ = true;
            if (!diagnosticRetired_ && !view_.closeRequested && status.state == "error" && status.phase == "idle" &&
                status.hasError && !status.errorRecoverable) diagnostic_ = status.httpDiagnostic;
            else diagnostic_.reset();
        }
    }
    if (view_.stage != ProgressStage::connecting || view_.closeRequested)
        return;
    view_.statusUnavailable = !status.valid;
    if (!status.valid)
        return; // Delivery failure is not a bootstrap result.
    if (disconnect_) {
        // The reply can still carry the previous connection's error while the
        // accepted Disconnect waits for teardown. Keep observing the service.
        const bool failed = status.phase != "stopping" &&
            (status.state == "error" || (status.state == "disconnected" && status.hasError));
        view_.errorCode = failed ? "disconnect_unconfirmed" : "";
        if (status.state == "disconnected" && status.phase == "idle" &&
            status.tunnelReady == "no" && !status.hasError) {
            view_.stage = ProgressStage::succeeded;
            view_.shield = Shield::green;
        }
        return;
    }
    view_.serverUrl = status.serverUrl;
    view_.serverSecret = status.serverSecret;
    view_.accessKey = status.accessKey;
    view_.communicationContracts = status.communicationContracts;
    if (status.state == "connected" && status.phase == "running" && status.tunnelReady == "yes" && !status.hasError) {
        view_.stage = ProgressStage::succeeded;
        view_.shield = Shield::green;
        // Checkpoints remain the exact four checkpoints from this same status snapshot.
    } else if (status.hasError || status.state == "error") {
        view_.stage = ProgressStage::failed;
        view_.shield = Shield::grey;
        const auto &error = progressErrorText(status.errorCode);
        view_.errorCode = error.code;
        view_.errorDescription = error.description;
    } else if (status.state == "disconnected" && status.phase == "idle") {
        view_.closeRequested = true;
    }
}
void ProgressModel::onCommandFailure() {
    if (!disconnect_ || view_.stage != ProgressStage::connecting || view_.closeRequested) return;
    view_.stage = ProgressStage::failed;
    view_.shield = Shield::grey;
    view_.errorCode = "disconnect_unconfirmed";
}
void ProgressModel::onSuccessPresented(std::int64_t nowMs) {
    if (view_.stage != ProgressStage::succeeded || view_.closeRequested || successDeadlineMs_ || nowMs < 0)
        return;
    successDeadlineMs_ = nowMs > (std::numeric_limits<std::int64_t>::max)() - kSuccessDisplayMs
                             ? (std::numeric_limits<std::int64_t>::max)()
                             : nowMs + kSuccessDisplayMs;
}
void ProgressModel::onTick(std::int64_t nowMs) {
    if (!successDeadlineMs_ || view_.closeRequested)
        return;
    if (nowMs >= *successDeadlineMs_) {
        view_.closeRequested = true;
        return;
    }
    const auto remaining =
        nowMs < *successDeadlineMs_ - kSuccessDisplayMs ? kSuccessDisplayMs : *successDeadlineMs_ - nowMs;
    view_.closingSeconds = static_cast<unsigned>((remaining + 999) / 1000);
}
} // namespace phpvpn::tray
