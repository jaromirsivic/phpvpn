// GTK tray application (plan 4.12), the Linux counterpart of apps/tray/tray_application.
//
// One GtkApplication per desktop session (org.phpvpn.Tray, handles the command line):
// a second launch forwards its arguments to the primary instance and exits.
//   (no argument)            autostart, indicator only
//   --installation-complete  installation notice through the notification service
//   --show-window            show the fallback control window (application launcher)
// Exit and session end only close this tray; they never disconnect the VPN.
//
// Extension points for the remaining windows (WP10):
//   * TrayWindowHooks: openers called by the menu, the control window and the
//     one-shot configuration check (get-config-document / get-config-state), with
//     the same error and warning texts as Windows. A missing hook shows a
//     placeholder message.
//   * registerParticipant(): Wizard, Edit Config and Communication contracts report
//     their lifecycle snapshots; Exit asks the shared discard question only when a
//     participant is dirty or unknown, and session end calls endSession on each.
//     Register the Wizard first: its approvedDiscard veto is checked before any
//     other window is discarded, as on Windows.
//   * requestReconnectAfterSave(): the post-save "disconnect and reconnect now?" flow
//     of ReconnectController (Windows tray_application.cpp).
//   * requestConfigSave()/abandonConfigSave(): Edit Config publishes through the
//     shared command channel, exactly like the Windows set-config path.
//   * addStatusListener(): Statistics and Contracts receive every status snapshot.
//   * endpoint()/dispatcher(): the IPC endpoint and UI dispatcher for own channels.
#pragma once
#include <gtk/gtk.h>

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "control_window.h"
#include "gtk_util.h"
#include "indicator.h"
#include "ipc_async.h"
#include "lifecycle.h"
#include "notifications.h"
#include "poll_schedule.h"
#include "progress_window.h"
#include "reconnect_controller.h"
#include "tray_options.h"

namespace phpvpn::tray {
inline constexpr const char kApplicationId[] = "org.phpvpn.Tray";

struct TrayWindowHooks {
    // Configuration Wizard. False: it could not be opened.
    std::function<bool()> openWizard;
    // Communication contracts: activate an open window (true) before any IPC.
    std::function<bool()> activateContracts;
    std::function<bool(const std::string& document, const std::string& path, const std::string& revision)> openContracts;
    // Edit Config: activate an open editor (true) before any IPC.
    std::function<bool()> activateEditor;
    std::function<bool(const std::string& document, const std::string& path)> openEditor;
    // Show Server Info (the window performs its own get-server-info).
    std::function<void()> openServerInfo;
    // Statistics after the valid get-config-state, with the last snapshot.
    std::function<void(const TrayStatus& snapshot, bool unavailable)> openStatistics;
    // HTTP error detail of the Connect progress window, and its forced close.
    std::function<void(GtkWindow* parent, const HttpDiagnostic& diagnostic)> openHttpErrorDetail;
    std::function<void()> closeHttpErrorDetail;
};

struct ParticipantRegistration {
    LifecycleParticipant* participant = nullptr;
    // Parent of the reconnect question; may return nullptr.
    std::function<GtkWindow*()> window;
};

class TrayApp {
public:
    explicit TrayApp(TrayOptions options);
    ~TrayApp();
    TrayApp(const TrayApp&) = delete;
    TrayApp& operator=(const TrayApp&) = delete;
    int run(int argc, char** argv);

    // --- WP10 extension points -------------------------------------------------
    TrayWindowHooks& hooks() { return hooks_; }
    void registerParticipant(ParticipantRegistration registration);
    void unregisterParticipant(LifecycleParticipant* participant);
    void addStatusListener(std::function<void(const TrayStatus& snapshot, bool unavailable)> listener);
    // Called by a participant after a confirmed save of the exact snapshot.
    void requestReconnectAfterSave(LifecycleParticipant* participant, const LifecycleSnapshot& saved);
    // Edit Config save over the shared command channel (Windows startCommand
    // set_config). True: done is called exactly once, also with notSent when the
    // request could not be started; the caller reports that result. False: refused
    // before any request. Either the tray has already shown its own message (another
    // command is being processed) or Exit/session end is in progress; the caller
    // silently ends its save state and shows no second message.
    bool requestConfigSave(const SaveOperation& operation, std::string payload,
                           std::function<void(const SaveResult&)> done);
    void abandonConfigSave();
    // Menu and control window actions, also usable by other windows.
    void onMenuAction(MenuAction action);
    void showControlWindow();
    const TrayStatus& snapshot() const { return snapshot_; }
    bool statusUnavailable() const { return statusUnavailable_; }
    bool connectedOrConnectingNow() const { return connectedOrConnecting(snapshot_); }
    const IpcEndpoint& endpoint() const { return endpoint_; }
    const Dispatcher& dispatcher() const { return dispatcher_; }
    GtkApplication* application() const { return application_; }
    void requestExit();
    void endSession();

private:
    class WatcherSink;
    bool initialise();
    int onCommandLine(GApplicationCommandLine* commandLine);
    void onStatusTimer();
    void onSnapshot(const TrayStatus& snapshot);
    void updateIndicator();
    bool startCommand(IpcCommand command, bool reconnectDisconnect = false);
    void finishCommand(IpcCommand command, std::string& response);
    void finishDisconnect(std::string& response);
    void startConfigAction(MenuAction action);
    void finishConfigAction(MenuAction action, WizardResult& response);
    void showConfigWarning(std::string_view availability, std::string_view path, std::string_view reason);
    bool savedSessionLive() const;
    void finishSavedSession();
    void cancelReconnect();
    void finishSavedStatus(std::string& response);
    void observeReconnectDisconnect(const TrayStatus& status, std::uint64_t token);
    void runQueuedConnect(std::uint64_t token);
    void pollReconnect();
    void advanceExit();
    void showErrorText(std::string_view text);
    GtkWindow* dialogParent() const;
    void watchStatusNotifier();

    TrayOptions options_;
    IpcEndpoint endpoint_;
    Dispatcher dispatcher_;
    GtkApplication* application_ = nullptr;
    bool initialised_ = false, held_ = false;
    TrayWindowHooks hooks_;
    std::vector<ParticipantRegistration> participants_;
    std::vector<std::function<void(const TrayStatus&, bool)>> statusListeners_;

    TrayStatus snapshot_;
    bool statusUnavailable_ = true;
    PollSchedule polling_{kStatusIntervalMs};
    guint statusTimer_ = 0, reconnectTimer_ = 0, exitTimer_ = 0;
    StatusChannel statusReader_;
    IpcChannel commands_, disconnect_, savedStatus_, config_;
    std::optional<IpcCommand> pendingCommand_;
    bool connectSuperseded_ = false;
    bool disconnectPending_ = false;
    bool disconnectInProgress_ = false;
    std::int64_t disconnectDeadline_ = 0;

    ReconnectController reconnect_;
    LifecycleParticipant* savedParticipant_ = nullptr;
    LifecycleSnapshot savedSnapshot_;
    std::int64_t savedStatusDeadline_ = 0;

    std::optional<MenuAction> configAction_;
    ExitCoordinator exit_;
    bool endingSession_ = false;

    ProgressWindow progress_, disconnectProgress_;
    Indicator indicator_;
    ControlWindow control_;
    DesktopNotifications notifications_;
    std::unique_ptr<WatcherSink> watcherSink_;
    std::unique_ptr<WatcherPresence> watcher_;
    guint watcherWatch_ = 0;
    std::vector<guint> signalSources_;
    DeferredCalls deferred_;
};
}  // namespace phpvpn::tray
