// Connect/Disconnect progress window of the macOS tray (tray_UX.md UX05), the AppKit
// counterpart of apps/tray/progress_window and apps/tray-linux/progress_window. The
// portable ProgressModel owns every decision; this adapter polls get-status every
// 500 ms, reads get-config once for the two detail fields, paints the result panel,
// starts the success countdown at the first real paint of the result heading and
// sends dismiss-connect-diagnostic when a window with an HTTP diagnostic closes.
#pragma once
#include <array>
#include <functional>
#include <string>

#include "ipc_async.h"
#include "mac_ui.h"
#include "poll_schedule.h"
#include "progress_details.h"
#include "progress_model.h"

namespace phpvpn::tray {
class ProgressWindow {
public:
    ProgressWindow(IpcEndpoint endpoint, Dispatcher dispatcher);
    ~ProgressWindow();
    ProgressWindow(const ProgressWindow&) = delete;
    ProgressWindow& operator=(const ProgressWindow&) = delete;
    void open(bool disconnect = false);
    void onSnapshot(const TrayStatus& snapshot);
    void onDisconnectReply(const TrayStatus& snapshot);
    void onDisconnectTimeout();
    // Disconnect window only: every snapshot it receives also goes to the tray.
    std::function<void(const TrayStatus&)> disconnectSnapshot;
    // HTTP error detail of the Connect window. Without it a placeholder is shown.
    std::function<void(NSWindow* parent, const HttpDiagnostic& diagnostic)> openErrorDetail;
    // Closes an open HTTP error detail window (called when the diagnostic retires).
    std::function<void()> closeErrorDetail;
    bool disconnectPending() const {
        return window_ && disconnect_ && model_.view().stage == ProgressStage::connecting &&
               !model_.view().statusUnavailable && model_.view().errorCode.empty();
    }
    void endSession();
    NSWindow* window() const { return window_; }
    void present() { if (window_) mac::presentWindow(window_); }
    const ProgressModel& model() const { return model_; }

private:
    void build();
    void refresh();
    void tick();
    void placeCountdown();
    void headingPainted();
    void requestClose();
    void destroy();
    IpcEndpoint endpoint_;
    Dispatcher dispatcher_;
    ProgressModel model_;
    PollSchedule polling_{kProgressIntervalMs};
    StatusChannel reader_;
    IpcChannel detailsReader_;
    IpcChannel dismissReader_;
    ProgressDetails details_;
    std::uint64_t diagnosticEpoch_ = 0;
    std::string diagnosticAttemptId_;
    mac::Timer timer_;
    bool readPending_ = false, disconnect_ = false, waitingForReply_ = false;
    bool countdownBeside_ = true;
    PVWindow* window_ = nil;
    PVStackView* root_ = nil;
    PVScrollView* scroller_ = nil;
    NSTextField* eyebrow_ = nil;
    PVStackView* card_ = nil;
    std::array<NSTextField*, 2> fields_{};
    std::array<NSTextField*, 4> checks_{}, checkIcons_{};
    PVStackView* panel_ = nil;
    PVStackView* panelTexts_ = nil;
    PVPaintLabel* panelHeading_ = nil;
    NSTextField* panelAction_ = nil;
    NSTextField* countdownSide_ = nil;
    NSTextField* countdownUnder_ = nil;
    NSButton* errorDetail_ = nil;
    mac::Deferred deferred_;
};
}  // namespace phpvpn::tray
