#include "tray_application.h"
// Local presentation only. client.md fixes the tray timer at five seconds.
// Progress owns a separate 500 ms timer and a separate read-only IPC worker.
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <optional>
#include <string>

#include "core/protocol/json.h"
#include "editor_window.h"
#include "poll_schedule.h"
#include "progress_window.h"
#include "resource.h"
#include "statistics_window.h"
#include "status_reader.h"
#include "window_utils.h"
#include "wizard_window.h"
#include "contracts_window.h"
#include "menu_model.h"

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")

namespace phpvpn::tray {
namespace {
constexpr UINT kTrayMessage = WM_APP + 1;
constexpr UINT kStatusMessage = WM_APP + 2;
constexpr UINT kCommandMessage = WM_APP + 3;
constexpr UINT kConfigMessage = WM_APP + 4, kDisconnectMessage = WM_APP + 5, kInfoMessage = WM_APP + 6;
constexpr UINT kWizardMenu = 206, kContractsMenu = 207;
constexpr UINT_PTR kStatusTimer = 1;
constexpr UINT_PTR kExitTimer = 90;
constexpr wchar_t kClass[] = L"PhpVpnTrayWindow";


} // namespace

TrayApplication::TrayApplication(TrayRuntime runtime)
    : runtime_(std::move(runtime)), shellIcon_(runtime_.shell.get()),
      statusReader_(runtime_.pipeName), commands_(runtime_.pipeName),
      disconnect_(runtime_.pipeName), info_(runtime_.pipeName), config_(runtime_.pipeName),
      editor_(runtime_.pipeName), infoWindow_(runtime_.pipeName), wizard_(runtime_.pipeName),
      contracts_(runtime_.pipeName), progress_(runtime_.pipeName), disconnectProgress_(runtime_.pipeName) {}
TrayApplication::~TrayApplication() {
    endSession();
    commands_.stop(); disconnect_.stop(); info_.stop(); config_.stop(); statusReader_.stop();
    if (window_) {
        KillTimer(window_, kStatusTimer);
        shellIcon_.remove();
        DestroyWindow(window_);
    }
    // Member windows/readers are destroyed before resources, mutex and runtime.
}
bool TrayApplication::initialise(HINSTANCE instance) {
    if (initialisationAttempted_) return initialised_;
    initialisationAttempted_ = true;
    if (runtime_.pipeName.empty() || runtime_.mutexName.empty() || !runtime_.shell || runtime_.tooltipOverride.size() >= 128) return false;
    const auto mutex = CreateMutexW(nullptr, FALSE, runtime_.mutexName.c_str());
    const auto mutexError = GetLastError();
    mutex_.reset(mutex);
    if (!mutex_.get()) return false;
    if (mutexError == ERROR_ALREADY_EXISTS) { alreadyRunning_ = true; initialised_ = true; return true; }
    instance_ = instance;
    WNDCLASSEXW type{};
    type.cbSize = sizeof(type);
    type.lpfnWndProc = procedure;
    type.hInstance = instance;
    type.lpszClassName = kClass;
    if (!RegisterClassExW(&type) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;
    // A hidden top-level window receives real session broadcasts. Message-only
    // windows are excluded by Windows. It never appears in the app switcher.
    window_ = CreateWindowExW(WS_EX_TOOLWINDOW, kClass, L"PHPVPN", WS_POPUP, 0, 0, 0, 0, nullptr,
                              nullptr, instance, this);
    if (!window_) return false;
    if (!resources_.update(instance, static_cast<int>(GetDpiForSystem()))) return false;
    if (!shellIcon_.publish(window_, kTrayMessage, resources_.icon(0),
        runtime_.tooltipOverride.empty() ? ui::product : runtime_.tooltipOverride)) return false;
    polling_.start(nowMs());
    SetTimer(window_, kStatusTimer, kStatusIntervalMs, nullptr);
    statusReader_.request(window_, kStatusMessage);
    initialised_ = true;
    return true;
}
int TrayApplication::run() {
    if (!initialised_) return 1;
    if (alreadyRunning_) return 0;
    if (!window_) return 1;
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        if (editor_.translate(message)) continue;
        if (infoWindow_.translate(message) || wizard_.translate(message)) continue;
        if (progress_.translate(message) || statistics_.translate(message)) continue;
        if (disconnectProgress_.translate(message)) continue;
        if (contracts_.translate(message)) continue;
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return 0;
}
LRESULT CALLBACK TrayApplication::procedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* self = reinterpret_cast<TrayApplication*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        self = static_cast<TrayApplication*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
        self->window_ = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    if (!self) return DefWindowProcW(window, message, wParam, lParam);
    if (self->endingSession_ && message != WM_DESTROY && message != WM_NCDESTROY && message != WM_QUERYENDSESSION && message != WM_ENDSESSION)
        return DefWindowProcW(window, message, wParam, lParam);
    ScopedWindowPlacement placement(self->menuPlacement_);
    switch (message) {
        case kTrayMessage:
            if (LOWORD(lParam) == WM_RBUTTONUP || LOWORD(lParam) == WM_LBUTTONUP || LOWORD(lParam) == WM_CONTEXTMENU || LOWORD(lParam) == NIN_KEYSELECT)
                self->showMenu(LOWORD(lParam) == WM_CONTEXTMENU || LOWORD(lParam) == NIN_KEYSELECT);
            return 0;
        case WM_TIMER:
            if (wParam == kExitTimer) { self->advanceExit(); return 0; }
            if (wParam == kStatusTimer && self->polling_.onTimer(nowMs())) self->statusReader_.request(window, kStatusMessage);
            return 0;
        case kStatusMessage:
            if (auto snapshot = self->statusReader_.take()) self->onSnapshot(*snapshot);
            return 0;
        case kCommandMessage: self->finishCommand(); return 0;
        case kConfigMessage: self->finishConfigAction(); return 0;
        case kDisconnectMessage: self->finishDisconnect(); return 0;
        case kInfoMessage: self->finishInfo(); return 0;
        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case IDM_EDIT_CONFIG: self->startConfigAction(MenuAction::edit); return 0;
                case IDM_SHOW_INFO:
                    if (self->info_.request(IpcCommand::get_info, {}, window, kInfoMessage)) self->infoPlacement_ = self->menuPlacement_;
                    else self->showError(L"Server information is already being requested. Wait for that result.");
                    return 0;
                case kWizardMenu:
                    if (!self->wizard_.open(self->instance_)) self->showError(L"Configuration Wizard could not be opened.");
                    return 0;
                case kContractsMenu: self->startConfigAction(MenuAction::contracts); return 0;
                case IDM_TOGGLE:
                    self->startCommand(self->connectedOrConnecting() ? IpcCommand::disconnect : IpcCommand::connect);
                    return 0;
                case IDM_STATISTICS:
                    self->startConfigAction(MenuAction::statistics);
                    return 0;
                case IDM_EXIT: self->requestExit(); return 0;
                default: break;
            }
            break;
        case WM_QUERYENDSESSION: return TRUE;
        case WM_ENDSESSION: if (wParam) self->endSession(); return 0;
        case WM_CLOSE: self->requestExit(); return 0;
        case WM_DESTROY: self->window_ = nullptr; PostQuitMessage(0); return 0;
        case WM_NCDESTROY: SetWindowLongPtrW(window, GWLP_USERDATA, 0); break;
        default: break;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}
void TrayApplication::showMenu(bool keyboard) {
    HMENU menu = CreatePopupMenu();
    if (!menu) return;
    for (const auto& item : configurationMenu) {
        UINT id = 0;
        switch (item.action) {
            case MenuAction::toggle: id = IDM_TOGGLE; break;
            case MenuAction::wizard: id = kWizardMenu; break;
            case MenuAction::contracts: id = kContractsMenu; break;
            case MenuAction::edit: id = IDM_EDIT_CONFIG; break;
            case MenuAction::info: id = IDM_SHOW_INFO; break;
            case MenuAction::statistics: id = IDM_STATISTICS; break;
            case MenuAction::exit: id = IDM_EXIT; break;
            case MenuAction::separator: break;
        }
        const auto* title = item.action == MenuAction::toggle && connectedOrConnecting() ? L"Disconnect" :
            item.action == MenuAction::contracts ? L"Communication contracts" : item.title.data();
        AppendMenuW(menu, item.action == MenuAction::separator ? MF_SEPARATOR : MF_STRING, id, title);
    }
    POINT cursor{};
    if (!GetCursorPos(&cursor)) {
        const auto work = targetMonitor({}).work;
        cursor = {work.right - 1, work.bottom - 1};
    }
    if (keyboard) {
        if (auto anchor = runtime_.shell->menuAnchor(window_)) cursor = {anchor->x, anchor->y};
    }
    menuPlacement_.menuPoint = Point{cursor.x, cursor.y};
    SetForegroundWindow(window_);
    TrackPopupMenu(menu, TPM_RIGHTBUTTON, cursor.x, cursor.y, 0, window_, nullptr);
    PostMessageW(window_, WM_NULL, 0, 0);
    DestroyMenu(menu);
}
void TrayApplication::onSnapshot(const TrayStatus& snapshot) {
    statusUnavailable_ = !snapshot.valid;
    applyStatusSnapshot(snapshot_, snapshot);
    statistics_.onSnapshot(snapshot_, statusUnavailable_);
    contracts_.setConnectionActive(connectedOrConnecting());
    // Invalid reads retain the last authoritative snapshot, but indicate its
    // availability separately. They never invent a new zero-valued snapshot.
    updateIcon();
}
void TrayApplication::updateIcon() {
    std::size_t state = 0;
    const wchar_t* tip = ui::disconnectedTip;
    if (statusUnavailable_) tip = ui::unavailableTip;
    else if (snapshot_.state == "connected") { state = 2; tip = ui::connectedTip; }
    else if (snapshot_.state == "connecting") { state = 1; tip = ui::connectingTip; }
    else if (snapshot_.state == "recovering") { state = 1; tip = ui::recoveringTip; }
    else if (snapshot_.state == "error") tip = ui::errorTip;
    shellIcon_.publish(window_, kTrayMessage, resources_.icon(state), runtime_.tooltipOverride.empty() ? tip : runtime_.tooltipOverride);
}
void TrayApplication::startCommand(IpcCommand command, const std::string& payload) {
    if (endingSession_ || exitRequested_) return;
    if ((command == IpcCommand::connect || command == IpcCommand::disconnect) &&
        disconnectProgress_.disconnectPending()) {
        reactivateWindow(disconnectProgress_.window());
        return;
    }
    if (command == IpcCommand::disconnect) {
        if (!disconnect_.request(command, {}, window_, kDisconnectMessage)) showError(L"Disconnect is already being processed. Waiting for its result.");
        else {
            disconnectPending_ = true;
            connectSuperseded_ = pendingCommand_ == IpcCommand::connect;
            progress_.endSession();
            disconnectProgress_.open(instance_, true);
        }
        return;
    }
    if (command == IpcCommand::connect && disconnectPending_) {
        showError(L"Disconnect is already being processed. Waiting for its result.");
        return;
    }
    if (pendingCommand_) { showError(L"A configuration command is already being processed. Wait for its result and choose this action again."); return; }
    if (command == IpcCommand::set_config) {
        const auto operation = editor_.beginSave(GetTickCount64() + 30000);
        if (!operation) return;
        saveOperation_ = operation;
        commandPlacement_ = menuPlacement_;
        pendingCommand_ = command;
        if (!commands_.requestSave(*operation, payload, window_, kCommandMessage)) {
            pendingCommand_.reset(); saveOperation_.reset();
            editor_.finishSave({*operation, SaveOutcome::notSent, ErrorCode::ipc_unavailable});
        }
        return;
    }
    if (commands_.request(command, payload, window_, kCommandMessage)) {
        commandPlacement_ = menuPlacement_;
        pendingCommand_ = command;
    }
    else showError(L"The command could not be started.");
}
void TrayApplication::finishCommand() {
    ScopedWindowPlacement placement(commandPlacement_);
    if (pendingCommand_ == IpcCommand::set_config) {
        auto result = commands_.takeSave();
        if (!result || !saveOperation_ || result->operation != *saveOperation_) return;
        pendingCommand_.reset(); saveOperation_.reset();
        // No application-level Save dialog. Only the matching live Editor may
        // transition saving and announce its authoritative classified result.
        editor_.finishSave(*result);
        return;
    }
    const auto response = commands_.take();
    if (!response || !pendingCommand_) return;
    const auto command = *pendingCommand_;
    pendingCommand_.reset();
    if (command == IpcCommand::connect && connectSuperseded_) {
        connectSuperseded_ = false;
        statusReader_.stop(); statusReader_.request(window_, kStatusMessage);
        return;
    }
    // These extra reads do not restart or replace the periodic tray timer.
    if (command == IpcCommand::connect || command == IpcCommand::disconnect) {
        // Discard any read initiated before the command. It may otherwise
        // arrive after the command's newer snapshot and roll the icon back.
        statusReader_.stop();
        statusReader_.request(window_, kStatusMessage);
    }
    if (response->empty()) { showError(L"The PHPVPN service did not complete the command."); return; }
    auto parsed = Json::parse(*response);
    if (!parsed.ok() || !parsed.value().isObject()) { showError(L"The service returned an invalid response."); return; }
    if (const auto* error = parsed.value().find("error"); error && error->isObject()) {
        // Never display raw transport/system/configuration text as an error.
        const auto* description = error->find("description");
        auto text = description && description->isString() ? widen(description->stringValue()) : L"The command failed.";
        if (const auto* code = error->find("code"); code && code->isString() && code->stringValue().starts_with("config_"))
            text += L"\nExpected configuration: " + expectedConfigurationPath() + L"\nOpen Configuration Wizard, or copy a configuration supplied by your administrator to this path.";
        showError(text.c_str());
        return;
    }
    switch (command) {
        case IpcCommand::get_config:
            openEditor(*response, {});
            break;
        case IpcCommand::get_info:
            editor_.open(instance_, L"PHPVPN server information", Json::serializePretty(parsed.value()));
            break;
        case IpcCommand::connect: {
            const auto status = parseStatusResponse(*response);
            progress_.open(instance_);
            onSnapshot(status);
            progress_.onSnapshot(status);
            break;
        }
        case IpcCommand::disconnect:
            onSnapshot(parseStatusResponse(*response));
            progress_.onSnapshot(parseStatusResponse(*response));
            break;
        default: break;
    }
}
void TrayApplication::showConfigWarning(std::string_view availability, std::string_view path, std::string_view reason) {
    const auto text = L"The configuration is " + widen(availability) + L".\n" + widen(reason) + L"\nExpected configuration: " + widen(path) +
        L"\nOpen Configuration Wizard to create or reset it, or copy a configuration supplied by your administrator to this path. Edit Config can repair readable UTF-8 text.";
    showSystemMessage(window_, text.c_str(), L"PHPVPN configuration", SystemDialogIcon::warning);
}
void TrayApplication::openEditor(const std::string& text, const std::string& path) {
    editor_.open(instance_, L"PHPVPN configuration", text, [this](const std::string& content) {
        startCommand(IpcCommand::set_config, content);
        return false;
    }, path, [this] { finishCommand(); }, [this] { endEditorSession(); });
}
void TrayApplication::endEditorSession() {
    if (pendingCommand_ == IpcCommand::set_config) {
        commands_.abandon();
        pendingCommand_.reset(); saveOperation_.reset();
    }
    editor_.endSession();
}
void TrayApplication::startConfigAction(MenuAction action) {
    if (action == MenuAction::contracts && contracts_.isOpen()) { contracts_.activate(); return; }
    if (action == MenuAction::edit && editor_.window()) { reactivateWindow(editor_.window()); return; }
    if (config_.busy()) { showError(L"Another configuration action is being checked. Wait for its result and choose this action again."); return; }
    WizardRequest request;
    request.command = requiresConfigState(action) ? WizardCommand::get_config_state : WizardCommand::get_config_document;
    if (!config_.request(request, window_, kConfigMessage)) { showError(L"The configuration action could not be sent to the service."); return; }
    configPlacement_ = menuPlacement_;
    configAction_ = action;
}
void TrayApplication::finishConfigAction() {
    ScopedWindowPlacement placement(configPlacement_);
    auto response = config_.take();
    if (!response || !configAction_) return;
    const auto action = *configAction_; configAction_.reset();
    struct WipeContractsRead {
        WizardReadResult& response; bool active;
        ~WipeContractsRead() {
            if (!active || !response.reply) return;
            const auto* document=response.reply->result.find("document");
            if (document && document->isString()) { auto& text=const_cast<std::string&>(document->stringValue()); secureWipe(text.data(),text.size()); }
        }
    } wipeContractsRead{*response,action==MenuAction::contracts};
    if (!response->reply || response->reply->error) {
        const auto failure = response->reply ? *response->reply->error : WizardIpc::safeError(response->reply.error());
        std::wstring text = widen(failure.description) + L"\n" + widen(failure.suggestedAction);
        if (failure.configPath) {
            text += L"\nExpected configuration: " + widen(*failure.configPath);
            if (action != MenuAction::contracts && failure.code != "config_directory_security_invalid")
                text += L"\nOpen Configuration Wizard or copy a configuration supplied by your administrator to this path.";
        }
        showError(text.c_str()); return;
    }
    const auto& result = response->reply->result;
    const auto& availability = result.find("availability")->stringValue();
    const auto& path = result.find("configPath")->stringValue();
    if (action == MenuAction::edit) {
        if (availability == "missing") {
            const auto text = L"No configuration exists at:\n" + widen(path) +
                L"\nCreate the default configuration now and open Edit Config? The file will be written immediately.\nYou can also choose No and use Configuration Wizard, or copy a configuration supplied by your administrator to this path.";
            if (!confirmSystemAction(window_, text.c_str(), L"Create configuration")) return;
            WizardRequest create; create.command = WizardCommand::create_default_config;
            if (config_.request(create, window_, kConfigMessage)) configAction_ = action;
            else showError(L"The default configuration request could not be sent.");
            return;
        }
        const auto* document = result.find("document");
        if (document && document->isString()) { openEditor(document->stringValue(), path); return; }
    } else if (availability == "valid") {
        if (action == MenuAction::contracts) {
            const auto* document = result.find("document");
            const auto* revision = result.find("revisionToken");
            if (!document || !document->isString() || !contracts_.open(instance_, nullptr, document->stringValue(), path,
                revision && revision->isString() ? revision->stringValue() : std::string{})) showError(L"Communication contracts could not be opened.");
            contracts_.setConnectionActive(connectedOrConnecting());
        } else {
            statistics_.open(instance_, snapshot_, statusUnavailable_);
            statusReader_.request(window_, kStatusMessage);
        }
        return;
    }
    const auto* reason = result.find("reasonCode");
    if (action == MenuAction::contracts) {
        const auto actionText = availability == "repairable" ? L"Open Configuration Wizard or Edit Config to repair the configuration." :
            availability == "invalid" ? L"Open Edit Config to repair the configuration." :
            availability == "missing" ? L"Use Configuration Wizard or Edit Config to create a configuration." :
            availability == "unreadable" ? L"The configuration file could not be accessed. Check the file permissions." :
            availability == "encoding_error" ? L"The configuration is not valid UTF-8. Save it as UTF-8 in an external editor and try again." :
            availability == "too_large" ? L"The configuration exceeds the 512 KiB limit." : L"Resolve the configuration error before opening Communication contracts.";
        const auto text = L"The configuration is " + widen(availability) + L".\n" + widen(path) + L"\n" + actionText;
        showSystemMessage(window_, text.c_str(), L"Communication contracts", SystemDialogIcon::warning); return;
    }
    showConfigWarning(availability, path, reason && reason->isString() ? reason->stringValue() : std::string_view{});
}
void TrayApplication::finishDisconnect() {
    auto response = disconnect_.take(); if (!response) return;
    disconnectPending_ = false;
    const auto status = parseStatusResponse(*response);
    disconnectProgress_.onDisconnectReply(status);
    if (!status.valid) {
        if (!disconnectProgress_.window()) showError(L"The service did not confirm Disconnect. Try Disconnect again or check the service status.");
        return;
    }
    onSnapshot(status); progress_.onSnapshot(status);
    statusReader_.stop(); statusReader_.request(window_, kStatusMessage);
}
void TrayApplication::finishInfo() {
    ScopedWindowPlacement placement(infoPlacement_);
    auto response = info_.take(); if (!response) return;
    auto value = Json::parse(*response);
    if (!value || !value->isObject()) { showError(L"The service did not return valid server information."); return; }
    if (const auto* failure = value->find("error"); failure && failure->isObject()) {
        const auto* description = failure->find("description");
        auto text = description && description->isString() ? widen(description->stringValue()) : L"Server information could not be read.";
        if (const auto* code = failure->find("code"); code && code->isString() && code->stringValue().starts_with("config_"))
            text += L"\nExpected configuration: " + expectedConfigurationPath() + L"\nOpen Configuration Wizard, or copy a configuration supplied by your administrator to this path.";
        showError(text.c_str());
        return;
    }
    infoWindow_.open(instance_, L"PHPVPN server information", Json::serializePretty(*value));
}
void TrayApplication::requestExit() {
    if (endingSession_ || exitRequested_) return;
    exitRequested_ = true; exitApproval_.reset(); SetTimer(window_, kExitTimer, 50, nullptr); advanceExit();
}
void TrayApplication::advanceExit() {
    if (!exitRequested_ || exitConfirming_ || endingSession_) return;
    const auto editor = editor_.lifecycleSnapshot();
    const auto wizard = wizard_.lifecycleSnapshot();
    const auto contracts = contracts_.lifecycleSnapshot();
    if (exitApproval_ && ((editor.open && editor != exitApproval_->editor) ||
        (wizard.open && wizard != exitApproval_->wizard) || (contracts.open && contracts != exitApproval_->contracts))) exitApproval_.reset();
    // Each participant consumes its own original deadline. No operation, retry,
    // polling deadline or enabled state is changed by this waiting coordinator.
    if (editor.saving || wizard.committing || contracts.saving) return;
    const bool dirty = editor.dirty || wizard.dirty || contracts.dirty;
    const bool unknown = editor.unknown || wizard.commitUnknown || contracts.unknown;
    if ((dirty || unknown) && !exitApproval_) {
        exitConfirming_ = true;
        const auto text = unknown && dirty ? L"Discard unsaved changes?\nThe file may already have changed. Close without checking the save result?" :
            unknown ? L"The file may already have changed. Close without checking the save result?" : L"Discard unsaved changes?";
        const bool approved = confirmSystemAction(window_, text, L"Exit PHPVPN");
        exitConfirming_ = false;
        if (endingSession_) return;
        if (!approved) { exitRequested_ = false; exitApproval_.reset(); KillTimer(window_, kExitTimer); return; }
        exitApproval_ = ExitApproval{editor,wizard,contracts};
    }
    // Modal re-entry may edit, start a save, close or recreate any participant.
    // Check the complete set before discarding the first window.
    if (editor != editor_.lifecycleSnapshot() || wizard != wizard_.lifecycleSnapshot() || contracts != contracts_.lifecycleSnapshot()) return;
    // Wizard may still be presenting its confirmed Finish notification. Its
    // approvedDiscard veto is checked before discarding any other participant.
    if (wizard.open && !wizard_.approvedDiscard(wizard)) return;
    if (editor.open && !editor_.approvedDiscard(editor)) return;
    if (contracts.open && !contracts_.approvedDiscard(contracts)) return;
    endSession();
}
void TrayApplication::endSession() {
    if (endingSession_) return;
    endingSession_ = true; exitRequested_ = false; exitApproval_.reset();
    if (window_) { KillTimer(window_, kExitTimer); KillTimer(window_, kStatusTimer); }
    polling_.stop();
    commands_.abandon(); disconnect_.abandon(); info_.abandon(); config_.abandon(); statusReader_.abandon();
    pendingCommand_.reset(); configAction_.reset(); saveOperation_.reset();
    editor_.endSession(); wizard_.endSession(); contracts_.endSession();
    progress_.endSession(); infoWindow_.endSession();
    disconnectProgress_.endSession();
    if (statistics_.window()) DestroyWindow(statistics_.window());
    shellIcon_.remove();
    if (window_) DestroyWindow(window_);
}
} // namespace phpvpn::tray
