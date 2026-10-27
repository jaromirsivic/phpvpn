#include "tray_app.h"
#include "gtk_util.h"
// Local presentation only. client.md fixes the tray timer at five seconds; the
// progress window owns a separate 500 ms timer and read-only channel.
#include <glib-unix.h>
#include <limits.h>
#include <signal.h>
#include <unistd.h>

#include <algorithm>
#include <filesystem>

#include "core/base/secure_buffer.h"
#include "core/protocol/json.h"
#include "contracts_window.h"
#include "dialogs.h"
#include "editor_window.h"
#include "server_info_window.h"
#include "statistics_window.h"
#include "style.h"
#include "tray_arguments.h"
#include "tray_text.h"
#include "wizard_window.h"

namespace phpvpn::tray {
namespace {
constexpr std::int64_t kDisconnectObservationMs = 30000;
constexpr std::int64_t kSavedStatusBudgetMs = 30000;
constexpr const char kStatusNotifierWatcher[] = "org.kde.StatusNotifierWatcher";
constexpr std::string_view kSavedStatusUnverified =
    "Your changes were saved, but the current connection status could not be verified. Reconnect manually to apply them.";

// Development runs find the generated icons next to the executable (build tree).
std::string developmentIconPath() {
    char buffer[PATH_MAX];
    const auto length = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
    if (length <= 0) return {};
    buffer[length] = '\0';
    std::error_code error;
    const auto icons = std::filesystem::path(buffer).parent_path() / "icons";
    if (std::filesystem::exists(icons / "hicolor" / "22x22" / "apps" / "phpvpn-connected.png", error)) return icons.string();
    return {};
}
template <class F>
guint timeout(guint intervalMs, F* self, void (F::*method)()) {
    struct Call { F* self; void (F::*method)(); };
    return g_timeout_add_full(G_PRIORITY_DEFAULT, intervalMs, [](gpointer raw) -> gboolean {
        auto* call = static_cast<Call*>(raw);
        (call->self->*call->method)();
        return G_SOURCE_CONTINUE;
    }, new Call{self, method}, [](gpointer raw) { delete static_cast<Call*>(raw); });
}
void removeSource(guint& source) {
    if (source) g_source_remove(source);
    source = 0;
}
}  // namespace

class TrayApp::WatcherSink final : public WatcherPresenceSink {
public:
    explicit WatcherSink(TrayApp& app) : app_(app) {}
    void openFallbackWindow() override { app_.showControlWindow(); }
    void showWatcherHint() override { app_.notifications_.showInformation(kWatcherHintTitle, kWatcherHintText); }
private:
    TrayApp& app_;
};

TrayApp::TrayApp(TrayOptions options)
    : options_(std::move(options)), endpoint_{options_.socketPath, options_.expectedServerUid},
      dispatcher_(glibMainDispatcher()),
      statusReader_(endpoint_, dispatcher_, [this](const TrayStatus& status) { onSnapshot(status); }),
      commands_(endpoint_, dispatcher_), disconnect_(endpoint_, dispatcher_), savedStatus_(endpoint_, dispatcher_),
      config_(endpoint_, dispatcher_), progress_(endpoint_, dispatcher_), disconnectProgress_(endpoint_, dispatcher_),
      indicator_([this](MenuAction action) { onMenuAction(action); }),
      control_([this](MenuAction action) { onMenuAction(action); }) {
    disconnectProgress_.disconnectSnapshot = [this](const TrayStatus& status) {
        if (!endingSession_ && disconnectInProgress_) onSnapshot(status);
    };
    progress_.openErrorDetail = [this](GtkWindow* parent, const HttpDiagnostic& diagnostic) {
        if (hooks_.openHttpErrorDetail) hooks_.openHttpErrorDetail(parent, diagnostic);
        else showMessage(parent, kErrorDetailUnavailableText, "PHPVPN", DialogIcon::information);
    };
    progress_.closeErrorDetail = [this] { if (hooks_.closeHttpErrorDetail) hooks_.closeHttpErrorDetail(); };
    // WP10 windows. The Wizard registers the first lifecycle participant, so its
    // approvedDiscard veto is checked before any other window (Windows order).
    installWizardWindows(*this);
    // Read-only windows without drafts; their participants only receive endSession.
    installServerInfoWindow(*this);
    installStatisticsWindow(*this);
    // Editing windows (WP10b), after the Wizard in the Windows lifecycle order.
    installEditorWindow(*this);
    installContractsWindow(*this);
}

TrayApp::~TrayApp() {
    endSession();
    if (application_) g_object_unref(application_);
}

int TrayApp::run(int argc, char** argv) {
    application_ = gtk_application_new(kApplicationId, G_APPLICATION_HANDLES_COMMAND_LINE);
    // Registered with the session manager only to be told about the session end:
    // query-end is never inhibited (Windows WM_QUERYENDSESSION returns TRUE).
    g_object_set(application_, "register-session", TRUE, nullptr);
    g_signal_connect(application_, "command-line", gtkCallback([](GApplication*, GApplicationCommandLine* line, gpointer data) -> int {
        return static_cast<TrayApp*>(data)->onCommandLine(line);
    }), this);
    // Session end and a normal quit perform the same terminal cleanup.
    g_signal_connect(application_, "shutdown", gtkCallback([](GApplication*, gpointer data) {
        static_cast<TrayApp*>(data)->endSession();
    }), this);
    return g_application_run(G_APPLICATION(application_), argc, argv);
}

int TrayApp::onCommandLine(GApplicationCommandLine* commandLine) {
    int argc = 0;
    gchar** argv = g_application_command_line_get_arguments(commandLine, &argc);
    int status = 0;
    if (endingSession_) status = 1;
    else if (!initialised_ && !initialise()) {
        g_application_command_line_printerr(commandLine, "%s\n", utf8(ui::startupError).c_str());
        status = 1;
    } else {
        if (requestsInstallationNotice(argc, argv)) showInstallationNotice(notifications_);
        if (requestsShowWindow(argc, argv)) showControlWindow();
    }
    g_strfreev(argv);
    return status;
}

bool TrayApp::initialise() {
    if (initialised_) return true;
    style::install();
    gtk_window_set_default_icon_name(std::string(kApplicationIconName).c_str());
    auto iconPath = options_.iconThemePath.empty() ? developmentIconPath() : options_.iconThemePath;
    if (!iconPath.empty()) gtk_icon_theme_append_search_path(gtk_icon_theme_get_default(), iconPath.c_str());
    GDBusConnection* bus = g_application_get_dbus_connection(G_APPLICATION(application_));
    notifications_.attach(bus);
    const auto view = indicatorView(snapshot_, statusUnavailable_, false, false);
    if (!indicator_.create(iconPath, view)) return false;
    control_.update(view);
    watchStatusNotifier();
    // Exit and session end are terminal; a signal from the session performs the same cleanup.
    for (int number : {SIGTERM, SIGINT, SIGHUP}) {
        signalSources_.push_back(g_unix_signal_add(number, [](gpointer data) -> gboolean {
            auto* self = static_cast<TrayApp*>(data);
            self->endSession();
            return G_SOURCE_CONTINUE;
        }, this));
    }
    polling_.start(monotonicMs());
    statusTimer_ = timeout(kStatusIntervalMs, this, &TrayApp::onStatusTimer);
    // Startup reads the global state first and never connects (client.md).
    statusReader_.request();
    g_application_hold(G_APPLICATION(application_));
    held_ = true;
    initialised_ = true;
    return true;
}

void TrayApp::watchStatusNotifier() {
    watcherSink_ = std::make_unique<WatcherSink>(*this);
    watcher_ = std::make_unique<WatcherPresence>(*watcherSink_);
    GDBusConnection* bus = g_application_get_dbus_connection(G_APPLICATION(application_));
    if (!bus) { watcher_->vanished(); return; }
    watcherWatch_ = g_bus_watch_name_on_connection(bus, kStatusNotifierWatcher, G_BUS_NAME_WATCHER_FLAGS_NONE,
        [](GDBusConnection*, const gchar*, const gchar*, gpointer data) {
            auto* self = static_cast<TrayApp*>(data);
            if (!self->endingSession_) self->watcher_->appeared();
        },
        [](GDBusConnection*, const gchar*, gpointer data) {
            auto* self = static_cast<TrayApp*>(data);
            if (!self->endingSession_) self->watcher_->vanished();
        },
        this, nullptr);
}

void TrayApp::showControlWindow() {
    if (endingSession_) return;
    control_.show(indicatorView(snapshot_, statusUnavailable_, disconnectInProgress_,
                                pendingCommand_ == IpcCommand::connect && !connectSuperseded_));
}

GtkWindow* TrayApp::dialogParent() const { return control_.visible() ? control_.window() : nullptr; }
void TrayApp::showErrorText(std::string_view text) { showError(dialogParent(), text, "PHPVPN"); }

void TrayApp::registerParticipant(ParticipantRegistration registration) {
    if (!registration.participant) return;
    unregisterParticipant(registration.participant);
    participants_.push_back(std::move(registration));
}
void TrayApp::unregisterParticipant(LifecycleParticipant* participant) {
    if (savedParticipant_ == participant) { savedParticipant_ = nullptr; cancelReconnect(); }
    std::erase_if(participants_, [&](const auto& item) { return item.participant == participant; });
}
void TrayApp::addStatusListener(std::function<void(const TrayStatus&, bool)> listener) {
    if (listener) statusListeners_.push_back(std::move(listener));
}

// --- status ------------------------------------------------------------------
void TrayApp::onStatusTimer() {
    if (endingSession_ || !polling_.onTimer(monotonicMs())) return;
    updateIndicator();
    // The live Disconnect window owns fast status reads. A second reader could
    // deliver an older connected reply after teardown.
    if (!disconnectPending_ && (!disconnectInProgress_ || !disconnectProgress_.window())) statusReader_.request();
}

void TrayApp::onSnapshot(const TrayStatus& snapshot) {
    if (endingSession_) return;
    if (reconnect_.phase() == ReconnectController::Phase::connectQueued && !reconnect_.idle(snapshot)) cancelReconnect();
    if (disconnectInProgress_ && !disconnectPending_ &&
        (!snapshot.valid || (snapshot.phase != "stopping" &&
                             (snapshot.state == "error" || (snapshot.state == "disconnected" && snapshot.phase == "idle")))))
        disconnectInProgress_ = false;
    statusUnavailable_ = !snapshot.valid;
    applyStatusSnapshot(snapshot_, snapshot);
    // Only the live Connect owner retains raw HTTP. Icon/statistics caches do not.
    snapshot_.httpDiagnostic.reset();
    for (const auto& listener : statusListeners_) listener(snapshot_, statusUnavailable_);
    updateIndicator();
}

void TrayApp::updateIndicator() {
    if (endingSession_) return;
    if (disconnectInProgress_ && monotonicMs() >= disconnectDeadline_) {
        cancelReconnect();
        disconnectInProgress_ = false;
        statusUnavailable_ = true;
        disconnectProgress_.onDisconnectTimeout();
        statusReader_.abandon();
        statusReader_.request();
    }
    const auto view = indicatorView(snapshot_, statusUnavailable_, disconnectInProgress_,
                                    pendingCommand_ == IpcCommand::connect && !connectSuperseded_);
    indicator_.update(view);
    control_.update(view);
}

// --- menu --------------------------------------------------------------------
void TrayApp::onMenuAction(MenuAction action) {
    if (endingSession_) return;
    switch (action) {
        case MenuAction::toggle:
            startCommand(connectedOrConnecting(snapshot_) ? IpcCommand::disconnect : IpcCommand::connect);
            break;
        case MenuAction::wizard:
            cancelReconnect();
            if (!hooks_.openWizard) showMessage(dialogParent(), unavailableWindowText(action), "PHPVPN", DialogIcon::information);
            else if (!hooks_.openWizard()) showErrorText("Configuration Wizard could not be opened.");
            break;
        case MenuAction::contracts:
        case MenuAction::edit:
        case MenuAction::statistics:
            startConfigAction(action);
            break;
        case MenuAction::info:
            if (hooks_.openServerInfo) hooks_.openServerInfo();
            else showMessage(dialogParent(), unavailableWindowText(action), "PHPVPN", DialogIcon::information);
            break;
        case MenuAction::about:
            showMessage(dialogParent(), aboutDialogText(), utf8(aboutTitle), DialogIcon::information);
            break;
        case MenuAction::exit: requestExit(); break;
        case MenuAction::separator: break;
    }
}

bool TrayApp::startCommand(IpcCommand command, bool reconnectDisconnect) {
    if (!reconnectDisconnect) cancelReconnect();
    if (endingSession_ || exit_.active()) return false;
    if (disconnectProgress_.disconnectPending()) {
        disconnectProgress_.present();
        return false;
    }
    if (command == IpcCommand::disconnect) {
        if (!disconnect_.request(IpcCommand::disconnect, {}, [this](std::string& response) { finishDisconnect(response); })) {
            showErrorText("Disconnect is already being processed. Waiting for its result.");
            return false;
        }
        const auto token = reconnectDisconnect ? reconnect_.token() : 0;
        disconnectProgress_.disconnectSnapshot = [this, token](const TrayStatus& status) {
            if (endingSession_ || !disconnectInProgress_) return;
            observeReconnectDisconnect(status, token);
            onSnapshot(status);
        };
        disconnectPending_ = true;
        disconnectInProgress_ = true;
        disconnectDeadline_ = monotonicMs() + kDisconnectObservationMs;
        statusReader_.abandon();
        connectSuperseded_ = pendingCommand_ == IpcCommand::connect;
        progress_.endSession();
        disconnectProgress_.open(true);
        updateIndicator();
        return true;
    }
    if (disconnectPending_) {
        showErrorText("Disconnect is already being processed. Waiting for its result.");
        return false;
    }
    if (pendingCommand_) {
        showErrorText("A configuration command is already being processed. Wait for its result and choose this action again.");
        return false;
    }
    if (!commands_.request(command, {}, [this, command](std::string& response) { finishCommand(command, response); })) {
        showErrorText("The command could not be started.");
        return false;
    }
    if (command == IpcCommand::connect) progress_.endSession();
    pendingCommand_ = command;
    if (command == IpcCommand::connect) { statusReader_.abandon(); updateIndicator(); }
    return true;
}

void TrayApp::finishCommand(IpcCommand command, std::string& response) {
    if (pendingCommand_ != command) return;
    pendingCommand_.reset();
    updateIndicator();
    if (command == IpcCommand::connect && connectSuperseded_) {
        connectSuperseded_ = false;
        statusReader_.abandon();
        if (!disconnectPending_ && (!disconnectInProgress_ || !disconnectProgress_.window())) statusReader_.request();
        return;
    }
    // Discard reads started before the command; they could roll the icon back.
    if (command == IpcCommand::connect || command == IpcCommand::disconnect) {
        statusReader_.abandon();
        statusReader_.request();
    }
    if (response.empty()) { onSnapshot({}); showErrorText("The PHPVPN service did not complete the command."); return; }
    auto parsed = Json::parse(response);
    if (!parsed.ok() || !parsed.value().isObject()) { onSnapshot({}); showErrorText("The service returned an invalid response."); return; }
    if (const auto* error = parsed.value().find("error"); error && error->isObject()) {
        // Never display raw transport/system/configuration text as an error.
        const auto* description = error->find("description");
        std::string text = description && description->isString() ? description->stringValue() : "The command failed.";
        if (const auto* code = error->find("code"); code && code->isString() && code->stringValue().starts_with("config_"))
            text += "\nExpected configuration: " + expectedConfigurationPath() +
                    "\nOpen Configuration Wizard, or copy a configuration supplied by your administrator to this path.";
        showErrorText(text);
        return;
    }
    if (command == IpcCommand::connect) {
        const auto status = parseStatusResponse(response);
        progress_.open();
        onSnapshot(status);
        progress_.onSnapshot(status);
    }
}

void TrayApp::finishDisconnect(std::string& response) {
    disconnectPending_ = false;
    const auto status = parseStatusResponse(response);
    disconnectProgress_.onDisconnectReply(status);
    if (!status.valid) {
        cancelReconnect();
        disconnectInProgress_ = false;
        onSnapshot(status);
        statusReader_.abandon();
        statusReader_.request();
        if (!disconnectProgress_.window())
            showErrorText("The service did not confirm Disconnect. Try Disconnect again or check the service status.");
        return;
    }
    onSnapshot(status);
    progress_.onSnapshot(status);
    statusReader_.abandon();
    if (!disconnectInProgress_) statusReader_.request();
}

// --- one-shot configuration check -----------------------------------------------
void TrayApp::startConfigAction(MenuAction action) {
    if (action == MenuAction::contracts && hooks_.activateContracts && hooks_.activateContracts()) return;
    if (action == MenuAction::edit && hooks_.activateEditor && hooks_.activateEditor()) return;
    if (config_.busy()) {
        showErrorText("Another configuration action is being checked. Wait for its result and choose this action again.");
        return;
    }
    WizardRequest request;
    request.command = requiresConfigState(action) ? WizardCommand::get_config_state : WizardCommand::get_config_document;
    if (!config_.requestWizard(request, [this, action](WizardResult& result) { finishConfigAction(action, result); })) {
        showErrorText("The configuration action could not be sent to the service.");
        return;
    }
    configAction_ = action;
}

void TrayApp::finishConfigAction(MenuAction action, WizardResult& response) {
    if (configAction_ != action) return;
    configAction_.reset();
    // The received document is wiped on every path once the action has used it.
    struct WipeDocument {
        WizardResult& response;
        ~WipeDocument() {
            if (!response.reply) return;
            const auto* document = response.reply.value().result.find("document");
            if (document && document->isString()) {
                auto& text = const_cast<std::string&>(document->stringValue());
                secureWipe(text.data(), text.size());
            }
        }
    } wipe{response};
    if (!response.reply || response.reply.value().error) {
        const auto failure = response.reply ? *response.reply.value().error : WizardIpc::safeError(response.reply.error());
        std::string text = failure.description + "\n" + failure.suggestedAction;
        if (failure.configPath) {
            text += "\nExpected configuration: " + *failure.configPath;
            if (action != MenuAction::contracts && failure.code != "config_directory_security_invalid")
                text += "\nOpen Configuration Wizard or copy a configuration supplied by your administrator to this path.";
        }
        showErrorText(text);
        return;
    }
    const auto& result = response.reply.value().result;
    const auto& availability = result.find("availability")->stringValue();
    const auto& path = result.find("configPath")->stringValue();
    if (action == MenuAction::edit) {
        if (availability == "missing") {
            const auto text = "No configuration exists at:\n" + path +
                "\nCreate the default configuration now and open Edit Config? The file will be written immediately.\n"
                "You can also choose No and use Configuration Wizard, or copy a configuration supplied by your administrator to this path.";
            if (!confirm(dialogParent(), text, "Create configuration") || endingSession_) return;
            WizardRequest create;
            create.command = WizardCommand::create_default_config;
            if (config_.requestWizard(create, [this, action](WizardResult& result) { finishConfigAction(action, result); }))
                configAction_ = action;
            else showErrorText("The default configuration request could not be sent.");
            return;
        }
        const auto* document = result.find("document");
        if (document && document->isString()) {
            if (!hooks_.openEditor) showMessage(dialogParent(), unavailableWindowText(action), "PHPVPN", DialogIcon::information);
            else hooks_.openEditor(document->stringValue(), path);
            return;
        }
    } else if (availability == "valid") {
        if (action == MenuAction::contracts) {
            const auto* document = result.find("document");
            const auto* revision = result.find("revisionToken");
            if (!hooks_.openContracts) showMessage(dialogParent(), unavailableWindowText(action), "PHPVPN", DialogIcon::information);
            else if (!document || !document->isString() ||
                     !hooks_.openContracts(document->stringValue(), path,
                                           revision && revision->isString() ? revision->stringValue() : std::string{}))
                showErrorText("Communication contracts could not be opened.");
        } else {
            if (!hooks_.openStatistics) showMessage(dialogParent(), unavailableWindowText(action), "PHPVPN", DialogIcon::information);
            else {
                hooks_.openStatistics(snapshot_, statusUnavailable_);
                statusReader_.request();
            }
        }
        return;
    }
    const auto* reason = result.find("reasonCode");
    if (action == MenuAction::contracts) {
        const std::string actionText = availability == "repairable" ? "Open Configuration Wizard or Edit Config to repair the configuration."
            : availability == "invalid" ? "Open Edit Config to repair the configuration."
            : availability == "missing" ? "Use Configuration Wizard or Edit Config to create a configuration."
            : availability == "unreadable" ? "The configuration file could not be accessed. Check the file permissions."
            : availability == "encoding_error" ? "The configuration is not valid UTF-8. Save it as UTF-8 in an external editor and try again."
            : availability == "too_large" ? "The configuration exceeds the 512 KiB limit."
            : "Resolve the configuration error before opening Communication contracts.";
        showMessage(dialogParent(), "The configuration is " + availability + ".\n" + path + "\n" + actionText,
                    "Communication contracts", DialogIcon::warning);
        return;
    }
    showConfigWarning(availability, path, reason && reason->isString() ? reason->stringValue() : std::string_view{});
}

void TrayApp::showConfigWarning(std::string_view availability, std::string_view path, std::string_view reason) {
    const auto text = "The configuration is " + std::string(availability) + ".\n" + std::string(reason) +
        "\nExpected configuration: " + std::string(path) +
        "\nOpen Configuration Wizard to create or reset it, or copy a configuration supplied by your administrator to this path. Edit Config can repair readable UTF-8 text.";
    showMessage(dialogParent(), text, "PHPVPN configuration", DialogIcon::warning);
}

// --- Edit Config save ----------------------------------------------------------
bool TrayApp::requestConfigSave(const SaveOperation& operation, std::string payload,
                                std::function<void(const SaveResult&)> done) {
    cancelReconnect();
    // Windows startCommand(set_config): Exit/session end refuse silently, a running
    // command shows only the tray's message; the editor adds none of its own.
    if (endingSession_ || exit_.active()) { secureWipe(payload.data(), payload.size()); return false; }
    if (pendingCommand_) {
        secureWipe(payload.data(), payload.size());
        showErrorText("A configuration command is already being processed. Wait for its result and choose this action again.");
        return false;
    }
    pendingCommand_ = IpcCommand::set_config;
    auto callback = std::make_shared<std::function<void(const SaveResult&)>>(std::move(done));
    if (!commands_.requestSave(operation, std::move(payload), [this, callback](const SaveResult& result) {
            if (pendingCommand_ == IpcCommand::set_config) pendingCommand_.reset();
            if (*callback) (*callback)(result);
        })) {
        // The request never left the tray: the editor reports notSent (Windows
        // editor_.finishSave(notSent) after a failed requestSave).
        pendingCommand_.reset();
        if (*callback) (*callback)({operation, SaveOutcome::notSent, ErrorCode::ipc_unavailable, {}});
    }
    return true;
}

void TrayApp::abandonConfigSave() {
    if (pendingCommand_ != IpcCommand::set_config) return;
    commands_.abandon();
    pendingCommand_.reset();
}

// --- reconnect after save (tray_application.cpp:54-67, 355-427) ----------------
bool TrayApp::savedSessionLive() const {
    if (!savedParticipant_) return false;
    return savedSnapshot_.open && savedParticipant_->lifecycleSnapshot() == savedSnapshot_;
}

void TrayApp::finishSavedSession() {
    // Detach before discarding: the normal post-save close no longer owns the intent.
    auto* participant = std::exchange(savedParticipant_, nullptr);
    const auto snapshot = savedSnapshot_;
    savedSnapshot_ = {};
    if (participant) participant->approvedDiscard(snapshot);
}

void TrayApp::cancelReconnect() {
    reconnect_.cancel();
    savedStatus_.abandon();
    removeSource(reconnectTimer_);
    finishSavedSession();
}

void TrayApp::requestReconnectAfterSave(LifecycleParticipant* participant, const LifecycleSnapshot& saved) {
    if (!participant) return;
    if (reconnect_.phase() != ReconnectController::Phase::none) { participant->approvedDiscard(saved); return; }
    savedParticipant_ = participant;
    savedSnapshot_ = saved;
    if (endingSession_ || exit_.active() || disconnectPending_ || disconnectInProgress_ || pendingCommand_ ||
        !savedSessionLive() || !reconnect_.begin()) {
        finishSavedSession();
        return;
    }
    savedStatusDeadline_ = monotonicMs() + kSavedStatusBudgetMs;
    reconnectTimer_ = timeout(50, this, &TrayApp::pollReconnect);
    if (!savedStatus_.request(IpcCommand::get_status, {}, [this](std::string& response) { finishSavedStatus(response); })) {
        cancelReconnect();
        showErrorText(kSavedStatusUnverified);
    }
}

void TrayApp::finishSavedStatus(std::string& response) {
    const auto phase = reconnect_.phase();
    if (phase != ReconnectController::Phase::readingSavedStatus && phase != ReconnectController::Phase::revalidating) return;
    if (endingSession_ || exit_.active() || !savedSessionLive()) { cancelReconnect(); return; }
    const auto status = parseStatusResponse(response);
    if (!status.valid) {
        cancelReconnect();
        showErrorText(kSavedStatusUnverified);
        return;
    }
    if (phase == ReconnectController::Phase::readingSavedStatus) {
        if (status.state != "connected" || !reconnect_.confirm(status)) { cancelReconnect(); return; }
        const auto token = reconnect_.token();
        GtkWindow* owner = nullptr;
        for (const auto& registration : participants_)
            if (registration.participant == savedParticipant_ && registration.window) owner = registration.window();
        const bool yes = confirm(owner, utf8(kReconnectSavedText), "Configuration saved", true);
        if (!reconnect_.current(token)) return;
        if (!yes || endingSession_ || exit_.active() || !savedSessionLive()) { cancelReconnect(); return; }
        reconnect_.consent();
        savedStatusDeadline_ = monotonicMs() + kSavedStatusBudgetMs;
        if (!savedStatus_.request(IpcCommand::get_status, {}, [this](std::string& next) { finishSavedStatus(next); }))
            cancelReconnect();
        return;
    }
    if (!reconnect_.revalidate(status)) { cancelReconnect(); return; }
    onSnapshot(status);
    if (!startCommand(IpcCommand::disconnect, true)) { cancelReconnect(); return; }
    finishSavedSession();
}

void TrayApp::observeReconnectDisconnect(const TrayStatus& status, std::uint64_t token) {
    if (!reconnect_.current(token)) return;
    const auto now = static_cast<std::uint64_t>(monotonicMs());
    if (reconnect_.observeDisconnect(status, now, static_cast<std::uint64_t>(disconnectDeadline_))) {
        // This can run inside the Disconnect window's snapshot handler. Connect
        // opens only after that stack has finished updating its own window.
        struct Queued { TrayApp* self; std::uint64_t token; };
        g_idle_add_full(G_PRIORITY_DEFAULT, [](gpointer raw) -> gboolean {
            auto* queued = static_cast<Queued*>(raw);
            queued->self->runQueuedConnect(queued->token);
            return G_SOURCE_REMOVE;
        }, new Queued{this, token}, [](gpointer raw) { delete static_cast<Queued*>(raw); });
    } else if (!reconnect_.current(token)) {
        cancelReconnect();
        if (static_cast<std::int64_t>(now) >= disconnectDeadline_) updateIndicator();
    }
}

void TrayApp::runQueuedConnect(std::uint64_t token) {
    if (!reconnect_.current(token)) return;
    if (endingSession_ || exit_.active() || statusUnavailable_ || pendingCommand_ || disconnectPending_ ||
        disconnectInProgress_ || !reconnect_.consume(token, snapshot_)) { cancelReconnect(); return; }
    removeSource(reconnectTimer_);
    startCommand(IpcCommand::connect);
}

void TrayApp::pollReconnect() {
    const auto phase = reconnect_.phase();
    if (phase == ReconnectController::Phase::none) return;
    if (phase == ReconnectController::Phase::disconnecting) {
        if (!disconnectProgress_.window()) cancelReconnect();
        else if (monotonicMs() >= disconnectDeadline_) updateIndicator();
    } else if (phase != ReconnectController::Phase::connectQueued) {
        if (!savedSessionLive()) { cancelReconnect(); return; }
        if (phase != ReconnectController::Phase::confirming && monotonicMs() >= savedStatusDeadline_) {
            cancelReconnect();
            showErrorText(kSavedStatusUnverified);
        }
    }
}

// --- Exit and session end --------------------------------------------------------
void TrayApp::requestExit() {
    if (endingSession_ || exit_.active()) return;
    cancelReconnect();
    exit_.begin();
    exitTimer_ = timeout(50, this, &TrayApp::advanceExit);
    // The first step may ask the discard question; it never runs inside the caller's
    // menu/button handler (AT-SPI click action), only from an idle callback.
    deferred_.post([this] { advanceExit(); });
}

void TrayApp::advanceExit() {
    if (endingSession_) return;
    const auto step = exit_.advance(
        [this] {
            std::vector<LifecycleParticipant*> list;
            for (const auto& registration : participants_) list.push_back(registration.participant);
            return list;
        },
        [this](std::string_view question) { return confirm(dialogParent(), question, kExitTitle); });
    if (endingSession_) return;
    if (step == ExitCoordinator::Step::cancelled || step == ExitCoordinator::Step::idle) removeSource(exitTimer_);
    else if (step == ExitCoordinator::Step::finished) endSession();
}

void TrayApp::endSession() {
    if (endingSession_) return;
    endingSession_ = true;
    exit_.cancel();
    // Terminal and non-blocking: no UI, save, retry, Disconnect or worker wait.
    cancelReconnect();
    removeSource(exitTimer_);
    removeSource(statusTimer_);
    removeSource(reconnectTimer_);
    polling_.stop();
    commands_.abandon(); disconnect_.abandon(); config_.abandon(); statusReader_.abandon(); savedStatus_.abandon();
    pendingCommand_.reset();
    configAction_.reset();
    const auto participants = participants_;
    for (const auto& registration : participants) registration.participant->endSession();
    progress_.endSession();
    disconnectProgress_.endSession();
    if (hooks_.closeHttpErrorDetail) hooks_.closeHttpErrorDetail();
    closeAllDialogs();
    indicator_.destroy();
    control_.destroy();
    if (watcherWatch_) { g_bus_unwatch_name(watcherWatch_); watcherWatch_ = 0; }
    for (auto source : signalSources_) g_source_remove(source);
    signalSources_.clear();
    notifications_.detach();
    if (application_) {
        if (held_) { g_application_release(G_APPLICATION(application_)); held_ = false; }
        g_application_quit(G_APPLICATION(application_));
    }
}
}  // namespace phpvpn::tray
