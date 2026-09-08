#pragma once
#include "status_model.h"
#include <optional>

namespace phpvpn::tray {
enum class ProgressStage { connecting, succeeded, failed };
enum class Shield { grey, orange, green };
inline constexpr unsigned kStatusIntervalMs = 5000;
inline constexpr unsigned kProgressIntervalMs = 500;
inline constexpr std::int64_t kSuccessDisplayMs = 3000;

struct ProgressErrorText {
    std::string_view code, description, panel, action;
};
// Only this allowlist supplies display text, never a received description.
const ProgressErrorText &progressErrorText(std::string_view code);

struct ProgressView {
    ProgressStage stage = ProgressStage::connecting;
    Shield shield = Shield::orange;
    CheckpointResult serverUrl = CheckpointResult::pending;
    CheckpointResult serverSecret = CheckpointResult::pending;
    CheckpointResult accessKey = CheckpointResult::pending;
    std::string errorDescription;
    std::string errorCode;
    bool statusUnavailable = false;
    unsigned closingSeconds = 3;
    bool closeRequested = false;
    bool operator==(const ProgressView &) const = default;
};

class ProgressModel {
  public:
    void onSnapshot(const TrayStatus &status, std::int64_t nowMs);
    void onTick(std::int64_t nowMs);
    // Called once after the visible success heading has actually painted.
    void onSuccessPresented(std::int64_t nowMs);
    std::optional<std::int64_t> successDeadlineMs() const { return successDeadlineMs_; }
    // Closing is a local presentation action, with no command callback.
    void onClose() { view_.closeRequested = true; }
    const ProgressView &view() const { return view_; }

  private:
    ProgressView view_;
    std::optional<std::int64_t> successDeadlineMs_;
};
} // namespace phpvpn::tray
