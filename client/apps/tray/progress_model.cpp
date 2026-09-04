#include "progress_model.h"

namespace phpvpn::tray {
void ProgressModel::onSnapshot(const TrayStatus& status, std::int64_t nowMs) {
    if (!status.valid || view_.stage != ProgressStage::connecting || view_.closeRequested) return;
    view_.serverUrl = status.serverUrl;
    view_.serverSecret = status.serverSecret;
    view_.accessKey = status.accessKey;
    // client.md: checkpoints are historical. Only connected confirms success.
    if (status.state == "connected") {
        view_.stage = ProgressStage::succeeded;
        view_.shield = Shield::green;
        view_.serverUrl = view_.serverSecret = view_.accessKey = CheckpointResult::success;
        succeededAtMs_ = nowMs;
    } else if (status.hasError || status.state == "error") {
        view_.stage = ProgressStage::failed;
        view_.shield = Shield::grey;
        view_.errorDescription = status.errorDescription;
    } else if (status.state == "disconnected") {
        view_.closeRequested = true;
    }
}
void ProgressModel::onTick(std::int64_t nowMs) {
    if (view_.stage == ProgressStage::succeeded && nowMs >= succeededAtMs_ &&
        nowMs - succeededAtMs_ >= kSuccessDisplayMs) view_.closeRequested = true;
}
}  // namespace phpvpn::tray
