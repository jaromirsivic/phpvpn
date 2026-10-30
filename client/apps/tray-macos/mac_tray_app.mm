#include "mac_tray_app.h"

#include <signal.h>

#include <algorithm>
#include <utility>

#include "core/base/secure_buffer.h"
#include "core/protocol/json.h"
#include "macos_text.h"

namespace phpvpn::tray {
namespace {
constexpr std::int64_t kDisconnectObservationMs = 30000;
constexpr std::int64_t kSavedStatusBudgetMs = 30000;
constexpr std::string_view kSavedStatusUnverified =
    "Your changes were saved, but the current connection status could not be verified. Reconnect manually to apply them.";
}  // namespace

TrayApp::TrayApp(TrayOptions options)
    : options_(std::move(options)), endpoint_{options_.socketPath, options_.expectedServerUid},
      dispatcher_(mainRunLoopDispatcher()),
      statusReader_(endpoint_, dispatcher_, [this](const TrayStatus& status) { onSnapshot(status); }),
      commands_(endpoint_, dispatcher_), disconnect_(endpoint_, dispatcher_), savedStatus_(endpoint_, dispatcher_),
      config_(endpoint_, dispatcher_), progress_(endpoint_, dispatcher_), disconnectProgress_(endpoint_, dispatcher_),
      statusItem_([this](MenuAction action) { onMenuAction(action); }),
      control_([this](MenuAction action) { onMenuAction(action); }) {
    disconnectProgress_.disconnectSnapshot = [this](const TrayStatus& status) {
        if (!endingSession_ && disconnectInProgress_) onSnapshot(status);
    };
    progress_.openErrorDetail = [this](NSWindow* parent, const HttpDiagnostic& diagnostic) {
        if (hooks_.openHttpErrorDetail) hooks_.openHttpErrorDetail(parent, diagnostic);
        else mac::showMessage(kErrorDetailUnavailableText, "PHPVPN", mac::DialogIcon::information);
    };
    progress_.closeErrorDetail = [this] {
        if (hooks_.closeHttpErrorDetail) hooks_.closeHttpErrorDetail();
    };
    // The Wizard registers the first lifecycle participant, so its approvedDiscard
    // veto is checked before any other window (Windows order).
    installWizardWindows(*this);
    // Read-only windows without drafts; their participants only receive endSession.
    installServerInfoWindow(*this);
    installStatisticsWindow(*this);
    // Editing windows, after the Wizard in the Windows lifecycle order.
    installEditorWindow(*this);
    installContractsWindow(*this);
}

TrayApp::~TrayApp() { endSession(false); }

bool TrayApp::start() {
    if (started_) return true;
    if (endingSession_) return false;
    const auto view = indicatorView(snapshot_, statusUnavailable_, false, false);
    if (!statusItem_.create(view)) return false;
    control_.update(view);
    installSignalHandlers();
    polling_.start(monotonicMs());
    statusTimer_.start(kStatusIntervalMs, [this] { onStatusTimer(); });
    // Startup reads the global state first and never connects (client.md).
    statusReader_.request();
    started_ = true;
    scheduleTestHooks();
    return true;
}

// --- CI hooks (macos_options.h, TEST ONLY) ----------------------------------------------
// The window opens through the same path as its menu item, after the first
// get-status had time to arrive; the capture follows a few seconds later so the
// IPC of the window (get-config-state, get-server-info) has completed. The capture
// renders the views the application owns and needs no Screen Recording permission.
void TrayApp::scheduleTestHooks() {
    if (options_.testOpenWindow.empty() && options_.testCaptureDirectory.empty()) return;
    TrayApp* app = this;
    const std::string window = options_.testOpenWindow;
    const std::string directory = options_.testCaptureDirectory;
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, static_cast<int64_t>(1500 * NSEC_PER_MSEC)),
                   dispatch_get_main_queue(), ^{
        if (app->endingSession_) return;
        if (window == "control") app->showControlWindow();
        else if (window == "wizard") app->onMenuAction(MenuAction::wizard);
        else if (window == "contracts") app->onMenuAction(MenuAction::contracts);
        else if (window == "edit") app->onMenuAction(MenuAction::edit);
        else if (window == "info") app->onMenuAction(MenuAction::info);
        else if (window == "statistics") app->onMenuAction(MenuAction::statistics);
    });
    if (directory.empty()) return;
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, static_cast<int64_t>(6000 * NSEC_PER_MSEC)),
                   dispatch_get_main_queue(), ^{
        if (app->endingSession_) return;
        app->captureForTest(directory, window.empty() ? "menubar" : window);
        app->endSession(true);
    });
}

void TrayApp::captureForTest(const std::string& directory, const std::string& prefix) {
    NSString* base = [NSString stringWithUTF8String:directory.c_str()];
    [[NSFileManager defaultManager] createDirectoryAtPath:base withIntermediateDirectories:YES attributes:nil error:nil];
    auto writeView = [&](NSView* view, NSString* name) {
        if (!view || view.bounds.size.width < 1 || view.bounds.size.height < 1) return;
        NSBitmapImageRep* rep = [view bitmapImageRepForCachingDisplayInRect:view.bounds];
        if (!rep) return;
        [view cacheDisplayInRect:view.bounds toBitmapImageRep:rep];
        NSData* png = [rep representationUsingType:NSBitmapImageFileTypePNG properties:@{}];
        [png writeToFile:[base stringByAppendingPathComponent:name] atomically:YES];
    };
    writeView(statusItem_.buttonView(), [NSString stringWithFormat:@"%s-status-item.png", prefix.c_str()]);
    NSUInteger index = 0;
    for (NSWindow* window in [NSApp windows]) {
        if (!window.isVisible || !window.contentView) continue;
        NSString* title = window.title.length ? window.title : @"window";
        NSMutableString* safe = [NSMutableString string];
        for (NSUInteger i = 0; i < title.length && safe.length < 40; ++i) {
            const unichar character = [title characterAtIndex:i];
            const bool plain = (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
                               (character >= '0' && character <= '9');
            [safe appendFormat:@"%C", plain ? character : static_cast<unichar>('-')];
        }
        writeView(window.contentView,
                  [NSString stringWithFormat:@"%s-%lu-%@.png", prefix.c_str(), static_cast<unsigned long>(index++), safe]);
    }
}

void TrayApp::handleLaunch(const LaunchRequest& request) {
    if (endingSession_) return;
    if (request.installationComplete) showInstallationNotice(notifications_);
    if (request.showWindow) showControlWindow();
}

void TrayApp::installSignalHandlers() {
    // Exit and session end are terminal; a signal (launchctl bootout, kill, logout)
    // performs the same cleanup. The handler runs on a global queue and marshals to
    // the main run loop, which also serves a running modal alert.
    TrayApp* app = this;
    for (int number : {SIGTERM, SIGINT, SIGHUP}) {
        ::signal(number, SIG_IGN);
        dispatch_source_t source = dispatch_source_create(DISPATCH_SOURCE_TYPE_SIGNAL, static_cast<uintptr_t>(number), 0,
                                                          dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0));
        if (!source) continue;
        dispatch_source_set_event_handler(source, ^{
            mac::post([app] { app->endSession(true); });
        });
        dispatch_resume(source);
        signalSources_.push_back(source);
    }
}

void TrayApp::showControlWindow() {
    if (endingSession_) return;
    control_.show(indicatorView(snapshot_, statusUnavailable_, disconnectInProgress_,
                                pendingCommand_ == IpcCommand::connect && !connectSuperseded_));
}

void TrayApp::showErrorText(std::string_view text) { mac::showError(text, "PHPVPN"); }

void TrayApp::registerParticipant(ParticipantRegistration registration) {
    if (!registration.participant) return;
    unregisterParticipant(registration.participant);
    participants_.push_back(std::move(registration));
}

void TrayApp::unregisterParticipant(LifecycleParticipant* participant) {
    if (savedParticipant_ == participant) {
        savedParticipant_ = nullptr;
        cancelReconnect();
    }
    std::erase_if(participants_, [&](const auto& item) { return item.participant == participant; });
}

void TrayApp::addStatusListener(std::function<void(const TrayStatus&, bool)> listener) {
    if (listener) statusListeners_.push_back(std::move(listener));
}

// --- status ------------------------------------------------------------------------------
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
        (!snapshot.valid ||
         (snapshot.phase != "stopping" &&
          (snapshot.state == "error" || (snapshot.state == "disconnected" && snapshot.phase == "idle")))))
        disconnectInProgress_ = false;
    statusUnavailable_ = !snapshot.valid;
    applyStatusSnapshot(snapshot_, snapshot);
    // Only the live Connect owner retains raw HTTP. Icon/statistics caches do not.
    snapshot_.httpDiagnostic.reset();
    const auto listeners = statusListeners_;
    for (const auto& listener : listeners) listener(snapshot_, statusUnavailable_);
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
    statusItem_.update(view);
    control_.update(view);
}

// --- menu ----------------------------------------------------------------------------------
void TrayApp::onMenuAction(MenuAction action) {
    if (endingSession_) return;
    switch (action) {
        case MenuAction::toggle:
            startCommand(connectedOrConnecting(snapshot_) ? IpcCommand::disconnect : IpcCommand::connect);
            break;
        case MenuAction::wizard:
            cancelReconnect();
            if (!hooks_.openWizard) mac::showMessage(unavailableWindowText(action), "PHPVPN");
            else if (!hooks_.openWizard()) showErrorText("Configuration Wizard could not be opened.");
            break;
        case MenuAction::contracts:
        case MenuAction::edit:
        case MenuAction::statistics:
            startConfigAction(action);
            break;
        case MenuAction::info:
            if (hooks_.openServerInfo) hooks_.openServerInfo();
            else mac::showMessage(unavailableWindowText(action), "PHPVPN");
            break;
        case MenuAction::about:
            mac::showMessage(aboutBodyText(), aboutHeadingText(), mac::DialogIcon::information);
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
    if (command == IpcCommand::connect) {
        statusReader_.abandon();
        updateIndicator();
    }
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
    if (response.empty()) {
        onSnapshot({});
        showErrorText("The PHPVPN service did not complete the command.\n\n" + std::string(kServiceStartHint));
        return;
    }
    auto parsed = Json::parse(response);
    if (!parsed.ok() || !parsed.value().isObject()) {
        onSnapshot({});
        showErrorText("The service returned an invalid response.");
        return;
    }
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

// --- one-shot configuration check ----------------------------------------------------------
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
        if (!response.reply && response.reply.code() == ErrorCode::ipc_unavailable)
            text += "\n\n" + std::string(kServiceStartHint);
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
            if (!mac::confirm(text, "Create configuration") || endingSession_) return;
            WizardRequest create;
            create.command = WizardCommand::create_default_config;
            if (config_.requestWizard(create, [this, action](WizardResult& result) { finishConfigAction(action, result); }))
                configAction_ = action;
            else
                showErrorText("The default configuration request could not be sent.");
            return;
        }
        const auto* document = result.find("document");
        if (document && document->isString()) {
            if (!hooks_.openEditor) mac::showMessage(unavailableWindowText(action), "PHPVPN");
            else hooks_.openEditor(document->stringValue(), path);
            return;
        }
    } else if (availability == "valid") {
        if (action == MenuAction::contracts) {
            const auto* document = result.find("document");
            const auto* revision = result.find("revisionToken");
            if (!hooks_.openContracts) mac::showMessage(unavailableWindowText(action), "PHPVPN");
            else if (!document || !document->isString() ||
                     !hooks_.openContracts(document->stringValue(), path,
                                           revision && revision->isString() ? revision->stringValue() : std::string{}))
                showErrorText("Communication contracts could not be opened.");
        } else {
            if (!hooks_.openStatistics) mac::showMessage(unavailableWindowText(action), "PHPVPN");
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
        mac::showMessage("The configuration is " + availability + ".\n" + path + "\n" + actionText,
                         "Communication contracts", mac::DialogIcon::warning);
        return;
    }
    showConfigWarning(availability, path, reason && reason->isString() ? reason->stringValue() : std::string_view{});
}

void TrayApp::showConfigWarning(std::string_view availability, std::string_view path, std::string_view reason) {
    const auto text = "The configuration is " + std::string(availability) + ".\n" + std::string(reason) +
        "\nExpected configuration: " + std::string(path) +
        "\nOpen Configuration Wizard to create or reset it, or copy a configuration supplied by your administrator to this path. Edit Config can repair readable UTF-8 text.";
    mac::showMessage(text, "PHPVPN configuration", mac::DialogIcon::warning);
}

// --- Edit Config save ---------------------------------------------------------------------------
bool TrayApp::requestConfigSave(const SaveOperation& operation, std::string payload,
                                std::function<void(const SaveResult&)> done) {
    cancelReconnect();
    // Windows startCommand(set_config): Exit/session end refuse silently, a running
    // command shows only the tray's message; the editor adds none of its own.
    if (endingSession_ || exit_.active()) {
        secureWipe(payload.data(), payload.size());
        return false;
    }
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
        // The request never left the tray: the editor reports notSent.
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

// --- reconnect after save ---------------------------------------------------------------------------
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
    reconnectTimer_.stop();
    finishSavedSession();
}

void TrayApp::requestReconnectAfterSave(LifecycleParticipant* participant, const LifecycleSnapshot& saved) {
    if (!participant) return;
    if (reconnect_.phase() != ReconnectController::Phase::none) {
        participant->approvedDiscard(saved);
        return;
    }
    savedParticipant_ = participant;
    savedSnapshot_ = saved;
    if (endingSession_ || exit_.active() || disconnectPending_ || disconnectInProgress_ || pendingCommand_ ||
        !savedSessionLive() || !reconnect_.begin()) {
        finishSavedSession();
        return;
    }
    savedStatusDeadline_ = monotonicMs() + kSavedStatusBudgetMs;
    reconnectTimer_.start(50, [this] { pollReconnect(); });
    if (!savedStatus_.request(IpcCommand::get_status, {}, [this](std::string& response) { finishSavedStatus(response); })) {
        cancelReconnect();
        showErrorText(kSavedStatusUnverified);
    }
}

void TrayApp::finishSavedStatus(std::string& response) {
    const auto phase = reconnect_.phase();
    if (phase != ReconnectController::Phase::readingSavedStatus && phase != ReconnectController::Phase::revalidating) return;
    if (endingSession_ || exit_.active() || !savedSessionLive()) {
        cancelReconnect();
        return;
    }
    const auto status = parseStatusResponse(response);
    if (!status.valid) {
        cancelReconnect();
        showErrorText(kSavedStatusUnverified);
        return;
    }
    if (phase == ReconnectController::Phase::readingSavedStatus) {
        if (status.state != "connected" || !reconnect_.confirm(status)) {
            cancelReconnect();
            return;
        }
        const auto token = reconnect_.token();
        for (const auto& registration : participants_)
            if (registration.participant == savedParticipant_ && registration.window)
                if (NSWindow* owner = registration.window()) mac::presentWindow(owner);
        const bool yes = mac::confirm(utf8(kReconnectSavedText), "Configuration saved", true);
        if (!reconnect_.current(token)) return;
        if (!yes || endingSession_ || exit_.active() || !savedSessionLive()) {
            cancelReconnect();
            return;
        }
        reconnect_.consent();
        savedStatusDeadline_ = monotonicMs() + kSavedStatusBudgetMs;
        if (!savedStatus_.request(IpcCommand::get_status, {}, [this](std::string& next) { finishSavedStatus(next); }))
            cancelReconnect();
        return;
    }
    if (!reconnect_.revalidate(status)) {
        cancelReconnect();
        return;
    }
    onSnapshot(status);
    if (!startCommand(IpcCommand::disconnect, true)) {
        cancelReconnect();
        return;
    }
    finishSavedSession();
}

void TrayApp::observeReconnectDisconnect(const TrayStatus& status, std::uint64_t token) {
    if (!reconnect_.current(token)) return;
    const auto now = static_cast<std::uint64_t>(monotonicMs());
    if (reconnect_.observeDisconnect(status, now, static_cast<std::uint64_t>(disconnectDeadline_))) {
        // This can run inside the Disconnect window's snapshot handler. Connect
        // opens only after that stack has finished updating its own window.
        deferred_.post([this, token] { runQueuedConnect(token); });
    } else if (!reconnect_.current(token)) {
        cancelReconnect();
        if (static_cast<std::int64_t>(now) >= disconnectDeadline_) updateIndicator();
    }
}

void TrayApp::runQueuedConnect(std::uint64_t token) {
    if (!reconnect_.current(token)) return;
    if (endingSession_ || exit_.active() || statusUnavailable_ || pendingCommand_ || disconnectPending_ ||
        disconnectInProgress_ || !reconnect_.consume(token, snapshot_)) {
        cancelReconnect();
        return;
    }
    reconnectTimer_.stop();
    startCommand(IpcCommand::connect);
}

void TrayApp::pollReconnect() {
    const auto phase = reconnect_.phase();
    if (phase == ReconnectController::Phase::none) return;
    if (phase == ReconnectController::Phase::disconnecting) {
        if (!disconnectProgress_.window()) cancelReconnect();
        else if (monotonicMs() >= disconnectDeadline_) updateIndicator();
    } else if (phase != ReconnectController::Phase::connectQueued) {
        if (!savedSessionLive()) {
            cancelReconnect();
            return;
        }
        if (phase != ReconnectController::Phase::confirming && monotonicMs() >= savedStatusDeadline_) {
            cancelReconnect();
            showErrorText(kSavedStatusUnverified);
        }
    }
}

// --- Exit and session end --------------------------------------------------------------------------
void TrayApp::requestExit() {
    if (endingSession_ || exit_.active()) return;
    cancelReconnect();
    exit_.begin();
    exitTimer_.start(50, [this] { advanceExit(); });
    // The first step may ask the discard question; never inside the menu action itself.
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
        [](std::string_view question) { return mac::confirm(question, kExitTitle); });
    if (endingSession_) return;
    if (step == ExitCoordinator::Step::cancelled || step == ExitCoordinator::Step::idle) exitTimer_.stop();
    else if (step == ExitCoordinator::Step::finished) endSession(true);
}

void TrayApp::endSession(bool quit) {
    if (endingSession_) return;
    endingSession_ = true;
    exit_.cancel();
    // Terminal and non-blocking: no UI, save, retry, Disconnect or worker wait.
    cancelReconnect();
    exitTimer_.stop();
    statusTimer_.stop();
    reconnectTimer_.stop();
    polling_.stop();
    commands_.abandon();
    disconnect_.abandon();
    config_.abandon();
    statusReader_.abandon();
    savedStatus_.abandon();
    pendingCommand_.reset();
    configAction_.reset();
    const auto participants = participants_;
    for (const auto& registration : participants) registration.participant->endSession();
    progress_.endSession();
    disconnectProgress_.endSession();
    if (hooks_.closeHttpErrorDetail) hooks_.closeHttpErrorDetail();
    mac::closeAllDialogs();
    statusItem_.destroy();
    control_.destroy();
    for (auto& source : signalSources_) dispatch_source_cancel(source);
    signalSources_.clear();
    notifications_.detach();
    // Out of any running modal loop first; AppKit then ends the process. The service
    // and the VPN are not touched.
    if (quit) mac::post([] { [NSApp terminate:nil]; });
}
}  // namespace phpvpn::tray
