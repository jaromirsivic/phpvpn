#pragma once
#include "status_model.h"

namespace phpvpn::tray {
inline constexpr wchar_t kReconnectSavedText[] =
    L"Your changes have been saved. It is strictly required that you disconnect and connect again to apply them. Do you want to disconnect and reconnect now?";

// UI-thread policy. A token belongs to one confirmed save, never to a future
// manual Disconnect. Transport readers and window lifetime stay with the app.
class ReconnectController {
public:
    enum class Phase { none, readingSavedStatus, confirming, revalidating, disconnecting, connectQueued };
    Phase phase() const { return phase_; }
    std::uint64_t token() const { return token_; }
    bool current(std::uint64_t token) const { return phase_ != Phase::none && token == token_; }
    bool begin() {
        if (phase_ != Phase::none) return false;
        ++token_; phase_ = Phase::readingSavedStatus; return true;
    }
    void cancel() { ++token_; phase_ = Phase::none; identity_ = {}; }
    bool confirm(const TrayStatus& status) {
        if (phase_ != Phase::readingSavedStatus || !status.valid || status.state != "connected" ||
            !status.connectionEpoch || !status.connectAttemptId || !status.connectionStartedAtUtcMs) return false;
        identity_ = status; phase_ = Phase::confirming; return true;
    }
    void consent() { if (phase_ == Phase::confirming) phase_ = Phase::revalidating; }
    bool revalidate(const TrayStatus& status) {
        if (phase_ != Phase::revalidating || !sameConnection(status) || status.state != "connected" ||
            status.connectionStartedAtUtcMs != identity_.connectionStartedAtUtcMs) return false;
        phase_ = Phase::disconnecting; return true;
    }
    bool idle(const TrayStatus& status) const {
        return sameConnection(status) && status.state == "disconnected" && status.phase == "idle" &&
            status.tunnelReady == "no" && !status.hasError && !status.connectionStartedAtUtcMs;
    }
    // Only the Disconnect reply and its own subsequent reader may advance this.
    bool observeDisconnect(const TrayStatus& status, std::uint64_t now = 0, std::uint64_t deadline = UINT64_MAX) {
        if (phase_ != Phase::disconnecting) return false;
        if (now >= deadline || !sameConnection(status) || status.hasError || status.state == "error" ||
            (status.state != "disconnected" && status.connectionStartedAtUtcMs != identity_.connectionStartedAtUtcMs)) {
            cancel(); return false;
        }
        if (idle(status)) { phase_ = Phase::connectQueued; return true; }
        if (status.state == "disconnected" || (status.state != "connected" && status.phase != "stopping")) cancel();
        return false;
    }
    bool consume(std::uint64_t token, const TrayStatus& status) {
        if (!current(token) || phase_ != Phase::connectQueued) return false;
        const bool allowed = idle(status);
        cancel(); return allowed;
    }
private:
    bool sameConnection(const TrayStatus& status) const {
        return status.valid && status.connectionEpoch == identity_.connectionEpoch &&
            status.connectAttemptId == identity_.connectAttemptId;
    }
    Phase phase_ = Phase::none;
    std::uint64_t token_ = 0;
    TrayStatus identity_;
};
} // namespace phpvpn::tray
