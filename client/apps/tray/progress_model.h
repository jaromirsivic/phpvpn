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
    explicit ProgressModel(bool disconnect = false) : disconnect_(disconnect) {}
    void onCommandFailure();
    void onSnapshot(const TrayStatus &status, std::int64_t nowMs);
    void onTick(std::int64_t nowMs);
    // Called once after the visible success heading has actually painted.
    void onSuccessPresented(std::int64_t nowMs);
    std::optional<std::int64_t> successDeadlineMs() const { return successDeadlineMs_; }
    // Closing is a local presentation action, with no command callback.
    void onClose() { view_.closeRequested = true; diagnostic_.reset(); }
    const std::optional<HttpDiagnostic>& httpDiagnostic() const { return diagnostic_; }
    std::uint64_t connectionEpoch() const { return epoch_; }
    const std::optional<std::string>& connectAttemptId() const { return attemptId_; }
    const ProgressView &view() const { return view_; }

  private:
    std::optional<HttpDiagnostic> diagnostic_;
    std::uint64_t epoch_ = 0;
    std::optional<std::string> attemptId_;
    bool diagnosticRetired_ = false;
    ProgressView view_;
    bool disconnect_ = false;
    std::optional<std::int64_t> successDeadlineMs_;
};
} // namespace phpvpn::tray
