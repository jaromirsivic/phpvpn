#include "progress_model.h"
#include <array>
#include <limits>

namespace phpvpn::tray {
const ProgressErrorText &progressErrorText(std::string_view code) {
    static constexpr std::array<ProgressErrorText, 7> errors{
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
         {"status_unavailable", "The connection status is unavailable.", "Cannot read connection status",
          "Check that the PHPVPN service is running."}}};
    for (const auto &error : errors)
        if (error.code == code)
            return error;
    return errors[4]; // Unknown codes cannot inject text or claim key rejection.
}
void ProgressModel::onSnapshot(const TrayStatus &status, std::int64_t /*nowMs*/) {
    if (view_.stage != ProgressStage::connecting || view_.closeRequested)
        return;
    view_.statusUnavailable = !status.valid;
    if (!status.valid)
        return; // Delivery failure is not a bootstrap result.
    view_.serverUrl = status.serverUrl;
    view_.serverSecret = status.serverSecret;
    view_.accessKey = status.accessKey;
    if (status.state == "connected" && status.phase == "running" && status.tunnelReady == "yes" && !status.hasError) {
        view_.stage = ProgressStage::succeeded;
        view_.shield = Shield::green;
        // Checkpoints remain the exact triple from this same status snapshot.
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
