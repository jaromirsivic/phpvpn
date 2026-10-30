// macOS tray application (plan-macos-v001 step 9), the AppKit counterpart of
// apps/tray/tray_application and apps/tray-linux/tray_app with the same workflow:
// five second get-status polling, Connect/Disconnect with progress windows, the
// one-shot configuration check of the menu actions, the reconnect question after a
// confirmed save, the common Exit coordinator and the terminal session end.
//
// One instance per login session (main.mm): a second launch forwards
// --show-window/--installation-complete through NSDistributedNotificationCenter.
// Exit, logout, SIGTERM and a crash only close this tray; they never disconnect the VPN.
//
// Extension points for the windows (installed from the constructor):
//   * TrayWindowHooks: openers called by the menu, the control window and the
//     one-shot configuration check (get-config-document / get-config-state).
//   * registerParticipant(): Wizard, Edit Config and Communication contracts report
//     their lifecycle snapshots for Exit; the Wizard registers first (its veto first).
//   * requestReconnectAfterSave(), requestConfigSave()/abandonConfigSave(),
//     addStatusListener(), endpoint()/dispatcher(): as in the Linux tray.
#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "ipc_async.h"
#include "lifecycle.h"
#include "mac_control_window.h"
#include "mac_notifications.h"
#include "mac_progress_window.h"
#include "mac_status_item.h"
#include "mac_ui.h"
#include "macos_options.h"
#include "poll_schedule.h"
#include "reconnect_controller.h"

namespace phpvpn::tray {
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
    std::function<void(NSWindow* parent, const HttpDiagnostic& diagnostic)> openHttpErrorDetail;
    std::function<void()> closeHttpErrorDetail;
};

struct ParticipantRegistration {
    LifecycleParticipant* participant = nullptr;
    // Window of the participant (the reconnect question is asked while it is open).
    std::function<NSWindow*()> window;
};

class TrayApp {
public:
    explicit TrayApp(TrayOptions options);
    ~TrayApp();
    TrayApp(const TrayApp&) = delete;
    TrayApp& operator=(const TrayApp&) = delete;

    // Status item, polling, signals and the first get-status. False: startup failed.
    bool start();
    // Arguments of this launch or of a later launch forwarded to this instance.
    void handleLaunch(const LaunchRequest& request);

    // --- window extension points ------------------------------------------------
    TrayWindowHooks& hooks() { return hooks_; }
    void registerParticipant(ParticipantRegistration registration);
    void unregisterParticipant(LifecycleParticipant* participant);
    void addStatusListener(std::function<void(const TrayStatus& snapshot, bool unavailable)> listener);
    // Called by a participant after a confirmed save of the exact snapshot.
    void requestReconnectAfterSave(LifecycleParticipant* participant, const LifecycleSnapshot& saved);
    // Edit Config save over the shared command channel (Windows startCommand
    // set_config). True: done is called exactly once, also with notSent when the
    // request could not be started. False: refused before any request (the tray has
    // shown its own message, or Exit/session end is in progress).
    bool requestConfigSave(const SaveOperation& operation, std::string payload,
                           std::function<void(const SaveResult&)> done);
    void abandonConfigSave();
    void onMenuAction(MenuAction action);
    void showControlWindow();
    const TrayStatus& snapshot() const { return snapshot_; }
    bool statusUnavailable() const { return statusUnavailable_; }
    bool connectedOrConnectingNow() const { return connectedOrConnecting(snapshot_); }
    const IpcEndpoint& endpoint() const { return endpoint_; }
    const Dispatcher& dispatcher() const { return dispatcher_; }
    void requestExit();
    // Terminal and non-blocking cleanup. quit: also terminate the application (the
    // system termination path passes false because AppKit terminates by itself).
    void endSession(bool quit = true);
    bool ending() const { return endingSession_; }

private:
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
    void installSignalHandlers();
    void scheduleTestHooks();
    void captureForTest(const std::string& directory, const std::string& prefix);

    TrayOptions options_;
    IpcEndpoint endpoint_;
    Dispatcher dispatcher_;
    bool started_ = false;
    TrayWindowHooks hooks_;
    std::vector<ParticipantRegistration> participants_;
    std::vector<std::function<void(const TrayStatus&, bool)>> statusListeners_;

    TrayStatus snapshot_;
    bool statusUnavailable_ = true;
    PollSchedule polling_{kStatusIntervalMs};
    mac::Timer statusTimer_, reconnectTimer_, exitTimer_;
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
    StatusItem statusItem_;
    ControlWindow control_;
    Notifications notifications_;
    std::vector<dispatch_source_t> signalSources_;
    mac::Deferred deferred_;
};

// Windows of the tray, each in its own file pair; called from the TrayApp constructor
// in the Windows lifecycle order (the Wizard first).
void installWizardWindows(TrayApp& app);
void installServerInfoWindow(TrayApp& app);
void installStatisticsWindow(TrayApp& app);
void installEditorWindow(TrayApp& app);
void installContractsWindow(TrayApp& app);
}  // namespace phpvpn::tray
