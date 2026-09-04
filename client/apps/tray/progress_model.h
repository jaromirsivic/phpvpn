#pragma once
#include "status_model.h"

namespace phpvpn::tray {
enum class ProgressStage { connecting, succeeded, failed };
enum class Shield { grey, orange, green };
inline constexpr unsigned kStatusIntervalMs = 5000;
inline constexpr unsigned kProgressIntervalMs = 500;
inline constexpr std::int64_t kSuccessDisplayMs = 3000;

struct ProgressView {
    ProgressStage stage = ProgressStage::connecting;
    Shield shield = Shield::orange;
    CheckpointResult serverUrl = CheckpointResult::pending;
    CheckpointResult serverSecret = CheckpointResult::pending;
    CheckpointResult accessKey = CheckpointResult::pending;
    std::string errorDescription;
    bool closeRequested = false;
};

class ProgressModel {
public:
    void onSnapshot(const TrayStatus& status, std::int64_t nowMs);
    void onTick(std::int64_t nowMs);
    // Closing is a local presentation action, with no command callback.
    void onClose() { view_.closeRequested = true; }
    const ProgressView& view() const { return view_; }
private:
    ProgressView view_;
    std::int64_t succeededAtMs_ = 0;
};
}  // namespace phpvpn::tray
