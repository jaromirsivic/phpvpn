#include "wizard_window.h"
#include "wizard_layout.h"
#include "window_utils.h"
#include "core/crypto/crypto_provider.h"
#include "core/config/config_defaults.h"
#include "platform/windows/ipc_pipe.h"
#include <commctrl.h>
#include <shlobj.h>
#include <limits>

namespace phpvpn::tray {
namespace {
constexpr wchar_t windowClass[] = L"PhpVpnConfigurationWizard";
constexpr wchar_t bodyClass[] = L"PhpVpnWizardContent";
constexpr UINT primaryMessage = WM_APP + 51, controlMessage = WM_APP + 52, heartbeatMessage = WM_APP + 53;
constexpr UINT disconnectMessage = WM_APP + 54, statusMessage = WM_APP + 55;
constexpr int nextId = 701, backId = 702, resetId = 703, cancelId = 704;
constexpr int urlId = 710, secretId = 711, defaultSecretId = 712, ipId = 713, keyId = 714, defaultKeyId = 715, contractsId = 716;
Result<std::string> inputText(HWND field) {
    const int length = GetWindowTextLengthW(field);
    std::wstring text(static_cast<std::size_t>(length) + 1, L'\0');
    const int copied = GetWindowTextW(field, text.data(), length + 1);
    text.resize(static_cast<std::size_t>(copied));
    if (text.empty()) return std::string{};
    const int required = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), copied, nullptr, 0, nullptr, nullptr);
    if (required <= 0) return Error(ErrorCode::config_encoding_error);
    std::string result(static_cast<std::size_t>(required), '\0');
    if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), copied, result.data(), required, nullptr, nullptr)) return Error(ErrorCode::config_encoding_error);
    return result;
}
std::string newId() {
    auto crypto = createPlatformCryptoProvider();
    if (!crypto) return {};
    auto id = crypto.value()->randomUuidV4();
    return id ? id.value() : std::string{};
}
bool terminal(WizardOperationState state) {
    return state != WizardOperationState::pending && state != WizardOperationState::running && state != WizardOperationState::cancelling;
}
}
WizardWindow::~WizardWindow() {
    // Best effort per-session close. Process exit cannot claim to cancel a
    // publication whose receipt is still unknown. The service lease is final.
    primary_.stop(); control_.stop(); heartbeat_.stop(); disconnect_.stop(); status_.stop();
    if (window_ && !model_.view().sessionId.empty()) {
        try {
            auto close = request(WizardCommand::close); close.requestId = newId();
            auto encoded = WizardIpc::buildRequest(close);
            if (encoded) (void)sendIpcRequestTo(pipeName_, *encoded, 1000);
        } catch (...) {}
    }
    if (window_) DestroyWindow(window_);
    if (font_) DeleteObject(font_);
    if (headingFont_) DeleteObject(headingFont_);
}
bool WizardWindow::open(HINSTANCE instance, std::function<void()> onConfirmed) {
    if (window_) { ShowWindow(window_, SW_RESTORE); SetForegroundWindow(window_); return true; }
    instance_ = instance; onConfirmed_ = std::move(onConfirmed);
    ++generation_;
    model_ = WizardModel{}; closing_ = resetting_ = disconnecting_ = confirmationShown_ = offlineKnown_ = false;
    commitId_.clear(); commitOperation_.clear(); scroll_ = 0;
    WNDCLASSEXW type{}; type.cbSize = sizeof(type); type.hInstance = instance;
    type.lpfnWndProc = procedure; type.lpszClassName = windowClass; type.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    type.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    RegisterClassExW(&type);
    type.lpfnWndProc = bodyProcedure; type.lpszClassName = bodyClass;
    RegisterClassExW(&type);
    window_ = CreateWindowExW(WS_EX_CONTROLPARENT, windowClass, L"Configuration Wizard", WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 1000, 680, nullptr, nullptr, instance, this);
    if (!window_) return false;
    body_ = CreateWindowExW(WS_EX_CONTROLPARENT, bodyClass, L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_CLIPCHILDREN,
        0, 0, 1, 1, window_, nullptr, instance, this);
    steps_ = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_OWNERDRAW, 0, 0, 1, 1, window_, nullptr, instance, nullptr);
    for (const auto& [id, text] : {std::pair{backId, L"Back"}, {nextId, L"Next"}, {resetId, L"Reset"}, {cancelId, L"Cancel"}})
        CreateWindowExW(0, L"BUTTON", text, WS_CHILD | WS_VISIBLE | WS_TABSTOP | (id == nextId ? BS_DEFPUSHBUTTON : BS_PUSHBUTTON),
            0, 0, 1, 1, window_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance, nullptr);
    sizeAndCenter(window_, {1000, 680}); render();
    lastContact_ = lastHeartbeat_ = nowMs(); lastPoll_ = 0;
    if (!SetTimer(window_, 1, 250, nullptr)) { destroy(); return false; }
    ShowWindow(window_, SW_SHOW); SetForegroundWindow(window_);
    if (!send(WizardCommand::open, primary_)) error(WizardIpc::safeError(Error(ErrorCode::ipc_unavailable)));
    return true;
}
WizardRequest WizardWindow::request(WizardCommand command) const {
    WizardRequest value; value.command = command;
    value.serviceInstanceId = model_.view().serviceInstanceId; value.sessionId = model_.view().sessionId;
    value.connectionEpoch = model_.view().connectionEpoch; value.fields = model_.fields();
    value.commitId = commitId_; value.createDefaultContracts = model_.createContracts();
    if (model_.attempt()) {
        value.clientAttemptId = model_.attempt()->clientAttemptId;
        value.attemptSequence = model_.attempt()->sequence;
        value.operationId = model_.attempt()->operationId;
    }
    if (model_.committing()) value.operationId = commitOperation_;
    return value;
}
bool WizardWindow::send(WizardCommand command, WizardReader& reader) {
    const auto message = &reader == &primary_ ? primaryMessage : &reader == &control_ ? controlMessage : heartbeatMessage;
    return reader.request(request(command), window_, message, generation_);
}
void WizardWindow::error(WizardError value) {
    if (model_.committing() || model_.step() == WizardStep::commitUnknown) {
        model_.commitUnknown(); model_.fail(std::move(value)); render(); return;
    }
    if (value.code == "wizard_session_expired") model_.sessionExpired();
    else if (value.code == "wizard_connection_interrupted") { model_.interrupt(); offlineKnown_ = false; }
    else model_.fail(std::move(value));
    render();
}
void WizardWindow::next() {
    if (primary_.busy() || control_.busy() || closing_ || resetting_ || disconnecting_ || model_.busy()) return;
    if (model_.step() == WizardStep::disconnectGate) {
        if (offlineKnown_) {
            if (!send(WizardCommand::resume, primary_)) error(WizardIpc::safeError(Error(ErrorCode::ipc_unavailable)));
        } else {
            if (MessageBoxW(window_, L"Disconnect the VPN for every user of this computer? You can connect it again after saving the configuration.",
                L"Configuration Wizard", MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES) return;
            if (!disconnect_.request(IpcCommand::disconnect, {}, window_, disconnectMessage)) error(WizardIpc::safeError(Error(ErrorCode::ipc_unavailable)));
            else disconnecting_ = true;
        }
        render(); return;
    }
    if (model_.step() == WizardStep::sessionExpired || model_.step() == WizardStep::loading) {
        if (!send(WizardCommand::open, primary_)) error(WizardIpc::safeError(Error(ErrorCode::ipc_unavailable)));
        render(); return;
    }
    if (model_.step() == WizardStep::commitUnknown) {
        if (!send(WizardCommand::get_commit, primary_)) error(WizardIpc::safeError(Error(ErrorCode::ipc_unavailable)));
        render(); return;
    }
    if (model_.step() == WizardStep::contracts) { finish(); return; }
    auto attempt = model_.beginCheck(newId(), nowMs());
    if (!attempt) { error(WizardIpc::safeError(attempt.error())); return; }
    const auto command = attempt->kind == WizardOperationKind::url ? WizardCommand::check_url :
        attempt->kind == WizardOperationKind::secret ? WizardCommand::check_secret : WizardCommand::check_access;
    if (!send(command, primary_)) {
        model_.fail(WizardIpc::safeError(Error(ErrorCode::ipc_unavailable)));
        send(WizardCommand::cancel_attempt, control_);
    }
    render();
}
void WizardWindow::finish() {
    commitId_ = newId();
    if (commitId_.empty()) { error(WizardIpc::safeError(Error(ErrorCode::internal))); return; }
    commitOperation_.clear(); model_.setCommitting(true); commitDeadline_ = nowMs() + 15000;
    if (!send(WizardCommand::finish, primary_)) model_.commitUnknown();
    render();
}
void WizardWindow::cancel(bool reset) {
    if (model_.committing()) {
        MessageBoxW(window_, L"The service is saving the configuration. Wait for its receipt before closing or resetting.", L"Configuration Wizard", MB_OK | MB_ICONINFORMATION); return;
    }
    if (control_.busy() || closing_ || resetting_) return;
    if (reset && MessageBoxW(window_, L"Reset this entire wizard to the default settings? Your file will change only after Finish.",
        L"Configuration Wizard", MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES) return;
    if (model_.step() == WizardStep::commitUnknown && reset) {
        MessageBoxW(window_, L"First verify the previous save. Its result is unknown and the file may already have changed.", L"Configuration Wizard", MB_OK | MB_ICONINFORMATION); return;
    }
    if (model_.view().sessionId.empty()) {
        if (primary_.busy()) { closing_ = !reset; resetting_ = reset; render(); return; }
        destroy(); return;
    }
    closing_ = !reset; resetting_ = reset;
    if (model_.attempt() && !model_.attempt()->closed) {
        model_.fail(WizardIpc::safeError(Error(ErrorCode::cancelled)));
        if (!send(WizardCommand::cancel_attempt, control_)) { closing_ = resetting_ = false; error(WizardIpc::safeError(Error(ErrorCode::ipc_unavailable))); }
    } else if (!send(reset ? WizardCommand::reset : WizardCommand::close, control_)) {
        closing_ = resetting_ = false; error(WizardIpc::safeError(Error(ErrorCode::ipc_unavailable)));
    }
    render();
}
void WizardWindow::confirm(const WizardReceipt& receipt) {
    if (receipt.commitId != commitId_ || confirmationShown_) return;
    confirmationShown_ = true; model_.confirmed();
    closing_ = true;
    if (!control_.busy()) send(WizardCommand::close, control_);
    // Drain the bounded close request before destroying its reader. A lost
    // close reply cannot undo an already confirmed publication.
    render();
}
void WizardWindow::receive(WizardReader& reader) {
    auto result = reader.take();
    if (!result) return;
    if (result->generation != generation_) return;
    const auto command = result->request.command;
    if (command == WizardCommand::close && confirmationShown_) {
        const auto callback = onConfirmed_;
        destroy();
        if (callback) callback();
        return;
    }
    if (!result->reply || result->reply->error) {
        auto failure = result->reply ? *result->reply->error : WizardIpc::safeError(result->reply.error());
        if (command == WizardCommand::finish && result->reply && result->reply->error && failure.code != "wizard_commit_unknown") {
            model_.commitFailed(failure); error(std::move(failure)); return;
        }
        if (command == WizardCommand::finish || (command == WizardCommand::get_operation && model_.committing())) {
            model_.commitUnknown(); model_.fail(std::move(failure)); render(); return;
        }
        if (command == WizardCommand::get_commit && failure.code == "wizard_session_expired") {
            // Explicit receipt verification discovered a restarted service.
            // One fresh state read informs the user, never repeats publication.
            send(WizardCommand::get_config_state, primary_); render(); return;
        }
        if (command == WizardCommand::keepalive && failure.code != "wizard_session_expired" && failure.code != "wizard_connection_interrupted") return;
        if ((closing_ || resetting_) && failure.code == "busy") { render(); return; }
        if (closing_ && failure.code == "wizard_session_expired") { destroy(); return; }
        closing_ = resetting_ = false;
        if (command == WizardCommand::check_url || command == WizardCommand::check_secret || command == WizardCommand::check_access || command == WizardCommand::get_operation)
            if (!control_.busy() && model_.attempt()) send(WizardCommand::cancel_attempt, control_);
        error(std::move(failure)); return;
    }
    lastContact_ = nowMs();
    if (command == WizardCommand::keepalive) return;
    const auto& value = result->reply->result;
    if (command == WizardCommand::open || command == WizardCommand::reset || command == WizardCommand::resume) {
        auto view = WizardIpc::parseView(value);
        if (!view) { error(WizardIpc::safeError(view.error())); return; }
        model_.load(std::move(view).value(), command == WizardCommand::resume);
        offlineKnown_ = model_.view().vpnState == ConnectionState::disconnected && model_.view().vpnPhase == ConnectionPhase::idle;
        if (command == WizardCommand::open || command == WizardCommand::reset || command == WizardCommand::resume) {
            if (command == WizardCommand::reset) resetting_ = false;
            ++generation_; primary_.stop(); heartbeat_.stop(); lastHeartbeat_ = nowMs();
        }
        if (closing_ || resetting_) send(resetting_ ? WizardCommand::reset : WizardCommand::close, control_);
    } else if (command == WizardCommand::check_url || command == WizardCommand::check_secret || command == WizardCommand::check_access) {
        model_.acceptStart(value.find("operationId")->stringValue(), result->request.clientAttemptId, result->request.attemptSequence);
    } else if (command == WizardCommand::get_operation) {
        auto operation = WizardIpc::parseOperation(value);
        if (!operation) { error(WizardIpc::safeError(operation.error())); return; }
        if (operation->kind == WizardOperationKind::finish) {
            if (operation->operationId != commitOperation_) { error(WizardIpc::safeError(Error(ErrorCode::ipc_malformed))); return; }
            if (operation->state == WizardOperationState::succeeded && operation->receipt) { confirm(*operation->receipt); return; }
            if (terminal(operation->state)) {
                model_.setCommitting(false);
                if (operation->error && operation->error->code == "wizard_commit_unknown") model_.commitUnknown();
                error(operation->error.value_or(WizardIpc::safeError(Error(ErrorCode::config_write_failed))));
            }
        } else {
            const auto previousStep = model_.step();
            model_.applyOperation(*operation, nowMs());
            if (model_.step() != previousStep) scroll_ = 0;
        }
    } else if (command == WizardCommand::finish) {
        commitOperation_ = value.find("operationId")->stringValue();
    } else if (command == WizardCommand::close) { destroy(); return; }
    else if (command == WizardCommand::cancel_attempt) {
        // Reset/close are short idempotent control actions. A cancelling probe
        // is drained by the service before Reset can be accepted.
        if (closing_ || resetting_) send(resetting_ ? WizardCommand::reset : WizardCommand::close, control_);
    } else if (command == WizardCommand::get_commit) {
        const auto& state = value.find("commitState")->stringValue();
        if (state == "confirmed") {
            const auto& receipt = *value.find("receipt");
            confirm({receipt.find("commitId")->stringValue(), receipt.find("revisionToken")->stringValue()}); return;
        }
        if (state == "failed") {
            auto failure = WizardIpc::parseError(*value.find("error"));
            // This is an authoritative no-publication result. Start again only
            // through the user's explicit reopening, never replay Finish.
            if (failure) model_.commitFailed(*failure);
        } else { model_.commitUnknown(); }
    } else if (command == WizardCommand::get_config_state) {
        model_.commitUnknown();
        MessageBoxW(window_, (L"The service restarted. The previous save cannot be confirmed here. Current configuration: " +
            widen(value.find("availability")->stringValue()) + L"\n" + widen(value.find("configPath")->stringValue()) +
            L"\nUse Edit Config to inspect the file before starting a new wizard. No save was repeated.").c_str(),
            L"Save result unknown", MB_OK | MB_ICONWARNING);
    }
    render();
}
void WizardWindow::receiveDisconnect() {
    auto response = disconnect_.take(); if (!response) return;
    auto snapshot = parseStatusResponse(*response);
    if (!snapshot.valid) {
        disconnecting_ = false; error(WizardIpc::safeError(Error(ErrorCode::ipc_unavailable))); return;
    }
    if (snapshot.state == "disconnected" && snapshot.phase == "idle") {
        disconnecting_ = false; offlineKnown_ = true; send(WizardCommand::resume, primary_);
    }
    render();
}
void WizardWindow::tick() {
    const auto now = nowMs();
    if (model_.committing() && now >= commitDeadline_) { model_.commitUnknown(); render(); }
    if (!model_.view().sessionId.empty() && now - lastContact_ >= kWizardLeaseMs && model_.step() != WizardStep::sessionExpired && model_.step() != WizardStep::commitUnknown) {
        if (model_.committing()) model_.commitUnknown(); else model_.sessionExpired();
        render();
    }
    if (!model_.view().sessionId.empty() && model_.step() != WizardStep::sessionExpired && model_.step() != WizardStep::commitUnknown &&
        !heartbeat_.busy() && now - lastHeartbeat_ >= kWizardKeepaliveMs) {
        lastHeartbeat_ = now; send(WizardCommand::keepalive, heartbeat_);
    }
    if (model_.expireAttempt(now)) {
        if (!control_.busy()) send(WizardCommand::cancel_attempt, control_);
        render();
    }
    if (now - lastPoll_ < 500) return;
    lastPoll_ = now;
    if (closing_ || resetting_) {
        if (!control_.busy() && !model_.view().sessionId.empty()) send(resetting_ ? WizardCommand::reset : WizardCommand::close, control_);
    } else if (disconnecting_ || model_.step() == WizardStep::disconnectGate) status_.request(window_, statusMessage);
    else if (!primary_.busy() && ((model_.committing() && !commitOperation_.empty()) ||
        (model_.busy() && model_.attempt() && !model_.attempt()->operationId.empty()))) send(WizardCommand::get_operation, primary_);
}
void WizardWindow::destroy() {
    ++generation_;
    primary_.stop(); control_.stop(); heartbeat_.stop(); disconnect_.stop(); status_.stop();
    if (window_) { KillTimer(window_, 1); DestroyWindow(window_); }
    children_.clear(); body_ = steps_ = nullptr;
}
HWND WizardWindow::add(const wchar_t* type, const std::wstring& text, DWORD style, int id, int height) {
    const int margin = scaleForDpi(20, dpi_);
    if (type == std::wstring_view(L"BUTTON") && (style & BS_TYPEMASK) == BS_AUTOCHECKBOX) {
        style |= BS_MULTILINE;
        HDC dc = GetDC(body_); auto old = SelectObject(dc, font_);
        RECT bounds{0, 0, std::max(20, contentWidth_ - 2 * margin - scaleForDpi(28, dpi_)), 0};
        DrawTextW(dc, text.c_str(), static_cast<int>(text.size()), &bounds, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
        SelectObject(dc, old); ReleaseDC(body_, dc);
        height = std::max(scaleForDpi(32, dpi_), static_cast<int>(bounds.bottom) + scaleForDpi(6, dpi_));
    }
    if (!height) height = scaleForDpi(32, dpi_);
    HWND child = CreateWindowExW(type == std::wstring_view(L"EDIT") ? WS_EX_CLIENTEDGE : 0, type, text.c_str(),
        WS_CHILD | WS_VISIBLE | style, margin, cursorY_, contentWidth_ - 2 * margin, height, body_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance_, nullptr);
    SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
    if (type == std::wstring_view(L"EDIT")) SendMessageW(child, EM_SETLIMITTEXT, kMaxConfigFileBytes, 0);
    children_.emplace_back(child, Rect{margin, cursorY_, contentWidth_ - margin, cursorY_ + height});
    cursorY_ += height + scaleForDpi(10, dpi_); return child;
}
void WizardWindow::label(const std::wstring& text, bool heading) {
    HDC dc = GetDC(body_); auto old = SelectObject(dc, heading ? headingFont_ : font_);
    RECT bounds{0, 0, std::max(40, contentWidth_ - scaleForDpi(40, dpi_)), 0};
    DrawTextW(dc, text.c_str(), static_cast<int>(text.size()), &bounds, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
    SelectObject(dc, old); ReleaseDC(body_, dc);
    HWND child = add(L"STATIC", text, SS_LEFT, 0, std::max(scaleForDpi(22, dpi_), static_cast<int>(bounds.bottom)));
    if (heading) SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(headingFont_), TRUE);
}
void WizardWindow::render() {
    if (!body_) return;
    updating_ = true;
    const HWND previousFocus = GetFocus(); const int focusId = GetDlgCtrlID(previousFocus);
    DWORD selectionStart = 0, selectionEnd = 0;
    const bool textFocus = focusId == urlId || focusId == secretId || focusId == ipId || focusId == keyId;
    if (textFocus) SendMessageW(previousFocus, EM_GETSEL, reinterpret_cast<WPARAM>(&selectionStart), reinterpret_cast<LPARAM>(&selectionEnd));
    for (const auto& item : children_) DestroyWindow(item.first);
    children_.clear();
    const int dpi = static_cast<int>(GetDpiForWindow(window_));
    if (!font_ || dpi != dpi_) {
        dpi_ = dpi;
        if (font_) DeleteObject(font_); if (headingFont_) DeleteObject(headingFont_);
        font_ = makeFont(dpi_, 15); headingFont_ = makeFont(dpi_, 23, true);
    }
    layout(); cursorY_ = scaleForDpi(20, dpi_);
    const auto step = model_.step();
    const bool locked = model_.busy() || primary_.busy() || control_.busy() || closing_ || resetting_ || disconnecting_;
    std::wstring steps = L"CONFIGURE THIS COMPUTER\n\n";
    if (step == WizardStep::disconnectGate) steps += L"Disconnect VPN\n\n";
    constexpr const wchar_t* names[]{L"1  Server URL", L"2  Server secret", L"3  IP and access key"};
    for (std::size_t i = 0; i < 3; ++i) {
        steps += names[i];
        steps += model_.checks()[i] == phpvpn::CheckpointResult::success ? L"\n    Verified\n\n" : model_.checks()[i] == phpvpn::CheckpointResult::failed ? L"\n    Failed\n\n" : L"\n    Not verified\n\n";
    }
    steps += L"4  Communication contracts";
    SetWindowTextW(steps_, steps.c_str()); SendMessageW(steps_, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
    InvalidateRect(steps_, nullptr, TRUE);
    if (step == WizardStep::loading) { label(L"Set up PHPVPN on this computer", true); label(L"Loading the configuration from the service. Changes are saved only when you choose Finish. Afterwards, use Connect to start the VPN."); }
    else if (step == WizardStep::disconnectGate) {
        label(L"Disconnect the VPN before continuing", true);
        label(L"The configuration wizard needs the global VPN connection to be disconnected. This affects every user of this computer. Your entries have been kept. A Connect from another window invalidates earlier checks.");
        label(offlineKnown_ ? L"The VPN is now disconnected. Choose Continue to repeat the checks from Server URL." : L"Choose Disconnect to stop the VPN, then wait for the service to confirm it is fully stopped.");
    } else if (step == WizardStep::loadError) {
        label(L"This configuration needs attention", true);
        label(L"The service classified the file as: " + widen(configAvailabilityName(model_.view().availability)) + L".\nExpected file: " + expectedConfigurationPath());
        label(L"Choose Reset to start with the default settings. Nothing will be written until Finish. You can also close this wizard and copy a configuration supplied by your administrator to the path above, or repair valid UTF-8 text using Edit Config.");
    } else if (step == WizardStep::sessionExpired) {
        label(L"Open a new wizard session", true);
        label(L"This wizard session expired because contact with the service was lost. Open Configuration Wizard again to continue. Your previous checks cannot be reused.");
    } else if (step == WizardStep::commitUnknown) {
        label(L"The save result is unknown", true);
        label(L"The file may already have changed. Choose Verify save to read the receipt for this save. The wizard will not repeat the write. If the service restarted, inspect the actual file using Edit Config before starting again.");
    } else if (step == WizardStep::serverUrl) {
        label(L"Server URL Configuration", true);
        label(L"This wizard configures this computer. Changes are saved only with Finish, and you connect afterwards using Connect.\nEnter the complete address of the PHPVPN server script. Ask your administrator for it, or use the address where you uploaded phpvpn.php.");
        label(L"Example: https://vpn.example.com/phpvpn.php\nReplace MODIFY-THIS-URL with your actual server address. HTTP and HTTPS are supported. The URL cannot contain a username, query or fragment.");
        label(L"Server URL"); add(L"EDIT", widen(model_.fields().serverUrl), WS_TABSTOP | ES_AUTOHSCROLL, urlId);
        label(L"Next checks whether this endpoint responds as a PHPVPN server. Server authentication is checked in the following step.");
    } else if (step == WizardStep::serverSecret) {
        label(L"Server Secret Configuration", true);
        label(L"The server secret must match SERVER_SECRET in the PHP file on your hosting account. Ask the server administrator if you do not manage that file. The URL is a web address, not a path to the source file.");
        auto box = add(L"BUTTON", L"Use default server secret", WS_TABSTOP | BS_AUTOCHECKBOX, defaultSecretId);
        SendMessageW(box, BM_SETCHECK, model_.useDefaultSecret() ? BST_CHECKED : BST_UNCHECKED, 0);
        label(model_.secretLiteralMode() ? L"Server secret (JSON string literal, including the surrounding quotes)" : L"Server secret (exact text, including any spaces)");
        auto field = add(L"EDIT", widen(model_.secretEditorText()), WS_TABSTOP | ES_AUTOHSCROLL, secretId);
        EnableWindow(field, !locked && !model_.useDefaultSecret());
        if (model_.secretLiteralMode()) label(L"This value contains control characters. JSON escapes such as \\u0000 represent those characters. Keep the surrounding quotes. Literal backslash text and escaped control characters are different values.");
        label(L"The default is CHANGE_THIS_SERVER_SECRET. Next authenticates a fresh server response and reads server information. No automatic retry is performed.");
    } else if (step == WizardStep::identity) {
        label(L"Virtual IP and Access Key", true);
        label(L"Choose this computer's virtual IP inside " + widen(model_.view().context->vpnCidr.toString()) +
            L". A suggested IP is not a reservation. Duplicate client IPs are technically allowed, but a collision is the operator's configuration responsibility. An existing local contract cannot point to this computer itself.");
        if (model_.fields().virtualIp.empty()) label(L"Enter an IP manually. No usable suggestion is available for the preserved CIDR and contracts.");
        label(L"Virtual IP"); add(L"EDIT", widen(model_.fields().virtualIp), WS_TABSTOP | ES_AUTOHSCROLL, ipId);
        auto box = add(L"BUTTON", L"Use default access key", WS_TABSTOP | BS_AUTOCHECKBOX, defaultKeyId);
        SendMessageW(box, BM_SETCHECK, model_.useDefaultAccessKey() ? BST_CHECKED : BST_UNCHECKED, 0);
        label(L"Access Key (64 lowercase hexadecimal characters)");
        auto field = add(L"EDIT", widen(model_.fields().accessKey), WS_TABSTOP | ES_AUTOHSCROLL, keyId);
        EnableWindow(field, !locked && !model_.useDefaultAccessKey());
        label(L"The default repeats the last IP octet as two lowercase hex characters 32 times. Turning it on replaces the displayed key immediately. The server's ACCESS_KEYS entry for this IP must contain the same key. Change that entry in your hosting file manager or ask the administrator.");
        label(L"This check may remove waiting messages for this IP address from the server. The messages will be discarded and will not be delivered to the VPN. If the result does not reach this window, the check may still have removed messages. Each new click on Next starts a new check and may remove more messages. Checks are not retried automatically.");
    } else if (step == WizardStep::contracts) {
        label(L"Communication Contracts", true);
        if (model_.contractsAllowed()) {
            auto box = add(L"BUTTON", L"Create default communication contracts", WS_TABSTOP | BS_AUTOCHECKBOX, contractsId);
            SendMessageW(box, BM_SETCHECK, model_.createContracts() ? BST_CHECKED : BST_UNCHECKED, 0);
            label(L"Default peers use host addresses .2 through .15 in " + widen(model_.view().context->vpnCidr.toString()) + L", excluding this computer. Contracts to create: " + std::to_wstring(model_.contractCount()) + L".");
        } else if (model_.view().mode == WizardMode::edit || model_.view().mode == WizardMode::repair)
            label(L"Existing communication contracts will be preserved without changes. Contract count: " + std::to_wstring(model_.contractCount()) + L".");
        else label(L"No default contracts will be created because vpnCidr is not in the supported 10.A.B.0/24 form.");
        label(L"Finish saves the entire validated configuration and opens Communication Contracts. It does not connect the VPN. The latest successful save wins, including saves from another editor. Your existing connection is not started automatically.");
    }
    if (model_.error()) {
        const auto& failure = *model_.error();
        label(L"Could not complete this action", true);
        std::wstring text = widen(failure.description) + L"\n" + widen(failure.suggestedAction);
        if (failure.httpStatus) text += L"\nHTTP status: " + std::to_wstring(*failure.httpStatus);
        if (failure.serverUrl) text += L"\nServer URL: " + widen(*failure.serverUrl);
        if (failure.configPath) text += L"\nConfiguration: " + widen(*failure.configPath);
        label(text);
    }
    if (locked) label(model_.committing() ? L"Saving configuration. Waiting for the publication receipt..." : closing_ ? L"Closing this session..." : resetting_ ? L"Stopping the current check and resetting..." : disconnecting_ ? L"Waiting for the VPN to stop..." : L"Waiting for the service...");
    contentHeight_ = cursorY_;
    for (const auto& item : children_) if (locked) EnableWindow(item.first, FALSE);
    for (int id : {nextId, backId, resetId, cancelId}) SendMessageW(GetDlgItem(window_, id), WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
    const bool back = step == WizardStep::serverSecret || step == WizardStep::identity || step == WizardStep::contracts;
    ShowWindow(GetDlgItem(window_, backId), back ? SW_SHOW : SW_HIDE);
    EnableWindow(GetDlgItem(window_, backId), !locked);
    const wchar_t* next = step == WizardStep::contracts ? L"Finish" : step == WizardStep::commitUnknown ? L"Verify save" :
        step == WizardStep::sessionExpired ? L"Open again" : step == WizardStep::disconnectGate ? (offlineKnown_ ? L"Continue" : L"Disconnect") : L"Next";
    SetWindowTextW(GetDlgItem(window_, nextId), next);
    EnableWindow(GetDlgItem(window_, nextId), !locked && step != WizardStep::loadError && (step != WizardStep::loading || model_.error().has_value()));
    EnableWindow(GetDlgItem(window_, resetId), !model_.committing() && !closing_ && !resetting_ && step != WizardStep::commitUnknown && step != WizardStep::sessionExpired && !model_.view().sessionId.empty());
    EnableWindow(GetDlgItem(window_, cancelId), !model_.committing() && !closing_ && !resetting_);
    scroll(scroll_); updating_ = false;
    HWND focus = GetDlgItem(body_, focusId);
    if (!focus) focus = GetDlgItem(window_, focusId);
    if (focus && IsWindowEnabled(focus) && IsWindowVisible(focus)) {
        SetFocus(focus);
        if (textFocus) SendMessageW(focus, EM_SETSEL, selectionStart, selectionEnd);
    }
    else {
        HWND first = GetNextDlgTabItem(body_, nullptr, FALSE);
        SetFocus(first && IsWindowEnabled(first) ? first : GetDlgItem(window_, nextId));
    }
}
void WizardWindow::layout() {
    if (!body_) return;
    RECT rect{}; GetClientRect(window_, &rect);
    const auto geometry = computeWizardLayout({rect.right, rect.bottom}, dpi_);
    const auto move = [](HWND child, Rect bounds) { MoveWindow(child, bounds.left, bounds.top, bounds.right - bounds.left, bounds.bottom - bounds.top, TRUE); };
    move(steps_, geometry.steps); move(body_, geometry.content);
    RECT bodyRect{}; GetClientRect(body_, &bodyRect); contentWidth_ = bodyRect.right;
    std::size_t index = 0;
    for (int id : {cancelId, nextId, backId, resetId}) {
        move(GetDlgItem(window_, id), geometry.buttons[index++]);
    }
}
void WizardWindow::scroll(int position) {
    if (!body_) return;
    RECT rect{}; GetClientRect(body_, &rect);
    scroll_ = std::clamp(position, 0, std::max(0, contentHeight_ - static_cast<int>(rect.bottom)));
    SCROLLINFO info{sizeof(info), SIF_RANGE | SIF_PAGE | SIF_POS, 0, std::max(0, contentHeight_ - 1), static_cast<UINT>(rect.bottom), scroll_, 0};
    SetScrollInfo(body_, SB_VERT, &info, TRUE);
    for (const auto& [child, bounds] : children_) MoveWindow(child, bounds.left, bounds.top - scroll_, bounds.right - bounds.left, bounds.bottom - bounds.top, TRUE);
}
void WizardWindow::revealFocus() {
    const HWND focus = GetFocus(); RECT rect{}; GetClientRect(body_, &rect);
    for (const auto& [child, bounds] : children_) if (child == focus) {
        if (bounds.top < scroll_) scroll(bounds.top);
        else if (bounds.bottom > scroll_ + rect.bottom) scroll(bounds.bottom - rect.bottom);
    }
}
void WizardWindow::drawSteps(const DRAWITEMSTRUCT& item) {
    FillRect(item.hDC, &item.rcItem, GetSysColorBrush(COLOR_WINDOW));
    auto old = SelectObject(item.hDC, font_); SetBkMode(item.hDC, TRANSPARENT);
    const int height = item.rcItem.bottom - item.rcItem.top;
    const bool compact = height < scaleForDpi(420, dpi_);
    int y = item.rcItem.top;
    if (!compact) {
        SetTextColor(item.hDC, RGB(66, 80, 94));
        RECT heading{0, y, item.rcItem.right, y + scaleForDpi(60, dpi_)};
        DrawTextW(item.hDC, L"CONFIGURE THIS COMPUTER", -1, &heading, DT_WORDBREAK | DT_NOPREFIX);
        y += scaleForDpi(72, dpi_);
    }
    constexpr const wchar_t* full[]{L"1  Server URL", L"2  Server secret", L"3  IP and access key", L"4  Contracts"};
    constexpr const wchar_t* shortNames[]{L"1  URL", L"2  Secret", L"3  IP / Key", L"4  Contracts"};
    const int row = std::max(1, (static_cast<int>(item.rcItem.bottom) - y) / 4);
    for (std::size_t i = 0; i < 4; ++i) {
        const auto state = i < 3 ? model_.checks()[i] : phpvpn::CheckpointResult::pending;
        const auto color = state == phpvpn::CheckpointResult::success ? RGB(23, 115, 62) :
            state == phpvpn::CheckpointResult::failed ? RGB(185, 35, 44) : RGB(172, 104, 0);
        SetTextColor(item.hDC, color);
        RECT label{0, y, item.rcItem.right, y + row};
        const std::wstring text = std::wstring(compact ? shortNames[i] : full[i]) + (i == 3 ? L"" :
            state == phpvpn::CheckpointResult::success ? L"\nVerified" : state == phpvpn::CheckpointResult::failed ? L"\nFailed" : L"\nNot verified");
        DrawTextW(item.hDC, text.c_str(), static_cast<int>(text.size()), &label, DT_WORDBREAK | DT_NOPREFIX);
        y += row;
    }
    SelectObject(item.hDC, old);
}
bool WizardWindow::translate(MSG& message) {
    if (!window_ || (message.hwnd != window_ && !IsChild(window_, message.hwnd))) return false;
    if (message.message == WM_KEYDOWN && message.wParam == VK_ESCAPE) { cancel(false); return true; }
    if (message.message == WM_KEYDOWN && message.wParam == VK_RETURN) {
        const int id = GetDlgCtrlID(GetFocus());
        if (id == cancelId || id == resetId || id == backId || id == nextId) command(id, BN_CLICKED); else next();
        return true;
    }
    if (IsDialogMessageW(window_, &message)) { revealFocus(); return true; }
    return false;
}
void WizardWindow::command(int id, int notification) {
    if (updating_) return;
    if (notification == EN_CHANGE && id >= urlId && id <= keyId) {
        auto text = inputText(GetDlgItem(body_, id));
        if (!text) { error(WizardIpc::safeError(text.error())); return; }
        if (id == urlId) model_.setUrl(std::move(text).value());
        if (id == secretId) model_.setSecretEditor(std::move(text).value());
        if (id == keyId) model_.setAccessKey(std::move(text).value());
        if (id == ipId) {
            model_.setIp(std::move(text).value());
            if (model_.useDefaultAccessKey()) {
                updating_ = true; SetWindowTextW(GetDlgItem(body_, keyId), widen(model_.fields().accessKey).c_str()); updating_ = false;
            }
        }
        return;
    }
    if (notification != BN_CLICKED) return;
    if (id == nextId) next();
    else if (id == backId) { model_.back(); scroll_ = 0; render(); }
    else if (id == resetId) cancel(true);
    else if (id == cancelId) cancel(false);
    else if (id == defaultSecretId) { model_.setDefaultSecret(SendMessageW(GetDlgItem(body_, id), BM_GETCHECK, 0, 0) == BST_CHECKED); render(); }
    else if (id == defaultKeyId) { model_.setDefaultAccessKey(SendMessageW(GetDlgItem(body_, id), BM_GETCHECK, 0, 0) == BST_CHECKED); render(); }
    else if (id == contractsId) { model_.setCreateContracts(SendMessageW(GetDlgItem(body_, id), BM_GETCHECK, 0, 0) == BST_CHECKED); render(); }
}
LRESULT CALLBACK WizardWindow::bodyProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* self = reinterpret_cast<WizardWindow*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        self = static_cast<WizardWindow*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    if (!self) return DefWindowProcW(window, message, wParam, lParam);
    if (message == WM_CTLCOLORSTATIC || message == WM_CTLCOLORBTN) {
        SetBkColor(reinterpret_cast<HDC>(wParam), GetSysColor(COLOR_WINDOW));
        return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_WINDOW));
    }
    if (message == WM_COMMAND) { self->command(LOWORD(wParam), HIWORD(wParam)); return 0; }
    if (message == WM_VSCROLL) {
        SCROLLINFO info{sizeof(info), SIF_ALL}; GetScrollInfo(window, SB_VERT, &info);
        int position = self->scroll_;
        switch (LOWORD(wParam)) {
            case SB_LINEUP: position -= scaleForDpi(30, self->dpi_); break;
            case SB_LINEDOWN: position += scaleForDpi(30, self->dpi_); break;
            case SB_PAGEUP: position -= static_cast<int>(info.nPage); break;
            case SB_PAGEDOWN: position += static_cast<int>(info.nPage); break;
            case SB_THUMBTRACK: position = info.nTrackPos; break;
            case SB_TOP: position = 0; break;
            case SB_BOTTOM: position = self->contentHeight_; break;
            default: break;
        }
        self->scroll(position); return 0;
    }
    if (message == WM_MOUSEWHEEL) { self->scroll(self->scroll_ - GET_WHEEL_DELTA_WPARAM(wParam) * scaleForDpi(90, self->dpi_) / WHEEL_DELTA); return 0; }
    return DefWindowProcW(window, message, wParam, lParam);
}
LRESULT CALLBACK WizardWindow::procedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* self = reinterpret_cast<WizardWindow*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        self = static_cast<WizardWindow*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
        self->window_ = window; SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    if (!self) return DefWindowProcW(window, message, wParam, lParam);
    switch (message) {
        case WM_COMMAND: self->command(LOWORD(wParam), HIWORD(wParam)); return 0;
        case WM_DRAWITEM:
            if (const auto* item = reinterpret_cast<DRAWITEMSTRUCT*>(lParam); item && item->hwndItem == self->steps_) { self->drawSteps(*item); return TRUE; }
            break;
        case WM_TIMER: self->tick(); return 0;
        case primaryMessage: self->receive(self->primary_); return 0;
        case controlMessage: self->receive(self->control_); return 0;
        case heartbeatMessage: self->receive(self->heartbeat_); return 0;
        case disconnectMessage: self->receiveDisconnect(); return 0;
        case statusMessage:
            if (auto snapshot = self->status_.take(); snapshot && snapshot->valid) {
                const bool offline = snapshot->state == "disconnected" && snapshot->phase == "idle";
                const bool changed = self->offlineKnown_ != offline;
                self->offlineKnown_ = offline;
                if (offline && self->disconnecting_ && !self->primary_.busy()) {
                    self->disconnecting_ = false; self->send(WizardCommand::resume, self->primary_); self->render();
                } else if (changed) self->render();
            }
            return 0;
        case WM_SIZE: if (!self->updating_) self->render(); return 0;
        case WM_DPICHANGED: applyDpiRect(window, lParam); self->render(); return 0;
        case WM_GETMINMAXINFO: {
            const auto dpi = GetDpiForWindow(window);
            RECT minimum{0, 0, scaleForDpi(480, static_cast<int>(dpi)), scaleForDpi(320, static_cast<int>(dpi))};
            AdjustWindowRectExForDpi(&minimum, static_cast<DWORD>(GetWindowLongPtrW(window, GWL_STYLE)), FALSE,
                static_cast<DWORD>(GetWindowLongPtrW(window, GWL_EXSTYLE)), dpi);
            MONITORINFO monitor{}; monitor.cbSize = sizeof(monitor);
            GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &monitor);
            auto* info = reinterpret_cast<MINMAXINFO*>(lParam);
            info->ptMinTrackSize.x = std::min(minimum.right - minimum.left, monitor.rcWork.right - monitor.rcWork.left);
            info->ptMinTrackSize.y = std::min(minimum.bottom - minimum.top, monitor.rcWork.bottom - monitor.rcWork.top);
            return 0;
        }
        case WM_CLOSE: self->cancel(false); return 0;
        case WM_NCDESTROY: self->window_ = nullptr; SetWindowLongPtrW(window, GWLP_USERDATA, 0); break;
        default: break;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}
} // namespace phpvpn::tray
