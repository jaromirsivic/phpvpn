// Connect/Disconnect progress window, the GTK counterpart of apps/tray/progress_window
// (tray_UX.md UX05). The portable ProgressModel owns every decision; this adapter
// polls get-status every 500 ms, reads get-config once for the two detail fields,
// paints the result panel and sends dismiss-connect-diagnostic when an HTTP error
// window closes.
#pragma once
#include <gtk/gtk.h>

#include <array>
#include <functional>
#include <string>

#include "ipc_async.h"
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
    // WP10 hook for the HTTP error detail window. Without it a placeholder is shown.
    std::function<void(GtkWindow* parent, const HttpDiagnostic& diagnostic)> openErrorDetail;
    // Closes an open HTTP error detail window (called when the diagnostic retires).
    std::function<void()> closeErrorDetail;
    bool disconnectPending() const {
        return window_ && disconnect_ && model_.view().stage == ProgressStage::connecting &&
               !model_.view().statusUnavailable && model_.view().errorCode.empty();
    }
    void endSession();
    GtkWindow* window() const { return window_ ? GTK_WINDOW(window_) : nullptr; }
    void present() { if (window_) gtk_window_present(GTK_WINDOW(window_)); }
    const ProgressModel& model() const { return model_; }

private:
    void build();
    void refresh();
    void tick();
    void placeCountdown();
    void onDestroyed();
    void requestClose();
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
    Shield shield_ = Shield::orange;
    guint timer_ = 0;
    guint placement_ = 0;
    bool countdownBeside_ = true;
    bool readPending_ = false, disconnect_ = false, waitingForReply_ = false;
    GtkWidget* window_ = nullptr;
    GtkWidget* shieldArea_ = nullptr;
    GtkWidget* eyebrow_ = nullptr;
    GtkWidget* card_ = nullptr;
    std::array<GtkWidget*, 2> fields_{};
    std::array<GtkWidget*, 4> checks_{}, checkIcons_{};
    GtkWidget* panel_ = nullptr;
    GtkWidget* panelGrid_ = nullptr;
    GtkWidget* panelHeading_ = nullptr;
    GtkWidget* panelAction_ = nullptr;
    GtkWidget* countdown_ = nullptr;
    GtkWidget* errorDetail_ = nullptr;
};
}  // namespace phpvpn::tray
