#include "wizard_window.h"
#include "wizard_layout.h"
#include "wizard_text.h"
#include "ui_controls.h"
#include "window_utils.h"
#include "core/crypto/crypto_provider.h"
#include "core/config/config_defaults.h"
#include "platform/windows/ipc_pipe.h"
#include <commctrl.h>
#include <shlobj.h>
#include <limits>
#include <atomic>

namespace phpvpn::tray {
namespace {
constexpr wchar_t windowClass[] = L"PhpVpnConfigurationWizard";
constexpr wchar_t bodyClass[] = L"PhpVpnWizardContent";
constexpr UINT primaryMessage = WM_APP + 51, controlMessage = WM_APP + 52, heartbeatMessage = WM_APP + 53;
constexpr UINT disconnectMessage = WM_APP + 54, statusMessage = WM_APP + 55;
constexpr int nextId = 701, backId = 702, resetId = 703;
constexpr int urlId = 710, secretId = 711, defaultSecretId = 712, ipId = 713, keyId = 714, defaultKeyId = 715, contractsId = 716;
constexpr int automaticIpId = 717;
std::uint64_t nextWindowGeneration() {
    static std::atomic<std::uint64_t> value{0};
    return ++value;
}
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
    if (window_) destroy();
}
WizardLifecycleSnapshot WizardWindow::lifecycleSnapshot() const {
    return {isOpen(), isOpen() && draftDirty_, isOpen() && model_.committing(),
        isOpen() && model_.step() == WizardStep::commitUnknown,
        windowGeneration_, generation_, draftGeneration_, attemptGeneration_, operationGeneration_,
        model_.attempt() ? model_.attempt()->sequence : 0, model_.view().connectionEpoch,
        commitDeadline_ ? std::optional<std::int64_t>{commitDeadline_} : std::nullopt};
}
void WizardWindow::abandonReaders() {
    primary_.abandon(); control_.abandon(); heartbeat_.abandon(); disconnect_.abandon(); status_.abandon();
}
bool WizardWindow::approvedDiscard(const WizardLifecycleSnapshot& approved) {
    if (!window_ || endingSession_ || model_.committing() || confirmationShown_ || lifecycleSnapshot() != approved) return false;
    abandonReaders();
    destroy();
    return true;
}
void WizardWindow::endSession() {
    if (endingSession_) return;
    endingSession_ = true;
    abandonReaders();
    destroy();
}
void WizardWindow::draftChanged() { draftDirty_ = true; ++draftGeneration_; }
bool WizardWindow::open(HINSTANCE instance, std::function<void()> onConfirmed) {
    if (endingSession_) return false;
    if (window_) { reactivateWindow(window_); return true; }
    instance_ = instance; onConfirmed_ = std::move(onConfirmed);
    ++generation_;
    windowGeneration_ = nextWindowGeneration(); ++draftGeneration_; ++attemptGeneration_; ++operationGeneration_;
    draftDirty_ = false; commitDeadline_ = 0;
    model_ = WizardModel{}; closing_ = resetting_ = disconnecting_ = confirmationShown_ = offlineKnown_ = false;
    commitId_.clear(); commitOperation_.clear(); scroll_ = 0;
    WNDCLASSEXW type{}; type.cbSize = sizeof(type); type.hInstance = instance;
    type.lpfnWndProc = procedure; type.lpszClassName = windowClass; type.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    type.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    RegisterClassExW(&type);
    type.lpfnWndProc = bodyProcedure; type.lpszClassName = bodyClass;
    RegisterClassExW(&type);
    window_ = CreateWindowExW(WS_EX_CONTROLPARENT, windowClass, L"Configuration Wizard", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT, 1000, 680, nullptr, nullptr, instance, this);
    if (!window_) return false;
    body_ = CreateWindowExW(WS_EX_CONTROLPARENT, bodyClass, L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_CLIPCHILDREN,
        0, 0, 1, 1, window_, nullptr, instance, this);
    steps_ = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_OWNERDRAW, 0, 0, 1, 1, window_, nullptr, instance, nullptr);
    for (const auto& [id, text] : {std::pair{resetId, L"Reset"}, {backId, L"Back"}, {nextId, wizardText::verify}})
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
    value.commitId = commitId_; value.createDefaultContracts = model_.shouldAddDefaultContracts();
    if (model_.attempt()) {
        value.clientAttemptId = model_.attempt()->clientAttemptId;
        value.attemptSequence = model_.attempt()->sequence;
        value.operationId = model_.attempt()->operationId;
    }
    if (model_.committing()) value.operationId = commitOperation_;
    return value;
}
bool WizardWindow::send(WizardCommand command, WizardReader& reader) {
    if (!window_ || endingSession_) return false;
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
    if (primary_.busy() || control_.busy() || closing_ || resetting_ || disconnecting_ || model_.busy() || confirmationShown_) return;
    if (model_.step() == WizardStep::disconnectGate) {
        if (offlineKnown_) {
            if (!send(WizardCommand::resume, primary_)) error(WizardIpc::safeError(Error(ErrorCode::ipc_unavailable)));
        } else {
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
    if (!attempt) {
        error(WizardIpc::safeError(attempt.error()));
        const int id = model_.step() == WizardStep::serverUrl ? urlId : model_.step() == WizardStep::serverSecret ? secretId :
            attempt.error().code == ErrorCode::config_invalid_address ? ipId : keyId;
        HWND focus = GetDlgItem(body_, id);
        if (focus && !IsWindowEnabled(focus)) focus = GetDlgItem(body_, id == ipId ? automaticIpId : id == keyId ? defaultKeyId : defaultSecretId);
        if (focus) { SetFocus(focus); revealFocus(); }
        return;
    }
    const auto command = attempt->kind == WizardOperationKind::url ? WizardCommand::check_url :
        attempt->kind == WizardOperationKind::secret ? WizardCommand::check_secret : WizardCommand::check_access;
    ++attemptGeneration_; ++operationGeneration_;
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
    ++operationGeneration_;
    if (!send(WizardCommand::finish, primary_)) model_.commitUnknown();
    render();
}
void WizardWindow::cancel(bool reset) {
    if (!window_ || endingSession_) return;
    if (model_.committing()) {
        MessageBoxW(window_, L"The service is saving the configuration. Wait for its receipt before closing or resetting.", L"Configuration Wizard", MB_OK | MB_ICONINFORMATION); return;
    }
    if (control_.busy() || closing_ || resetting_) return;
    const auto approved = lifecycleSnapshot();
    if (reset && MessageBoxW(window_, L"Reset this entire wizard to the default settings? Your file will change only after Finish.",
        L"Configuration Wizard", MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES) return;
    if (endingSession_ || !window_ || lifecycleSnapshot() != approved) return;
    if (model_.step() == WizardStep::commitUnknown && reset) {
        MessageBoxW(window_, L"First verify the previous save. Its result is unknown and the file may already have changed.", L"Configuration Wizard", MB_OK | MB_ICONINFORMATION); return;
    }
    ++operationGeneration_;
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
    draftDirty_ = false;
    closing_ = true;
    render();
    const auto confirmedGeneration = windowGeneration_;
    MessageBoxW(window_, wizardText::saved, wizardText::savedHeading, MB_OK | MB_ICONINFORMATION);
    if (endingSession_ || !window_ || windowGeneration_ != confirmedGeneration) return;
    if (!send(WizardCommand::close, control_)) {
        const auto callback = onConfirmed_;
        destroy();
        if (callback && !endingSession_) callback();
        return;
    }
    // Drain the bounded close request before destroying its reader. A lost
    // close reply cannot undo an already confirmed publication.
    render();
}
void WizardWindow::receive(WizardReader& reader) {
    if (!window_ || endingSession_) return;
    auto result = reader.take();
    if (!result) return;
    if (result->generation != generation_) return;
    const auto command = result->request.command;
    if (command == WizardCommand::close && confirmationShown_) {
        const auto callback = onConfirmed_;
        destroy();
        if (callback && !endingSession_) callback();
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
        ++draftGeneration_; ++attemptGeneration_; ++operationGeneration_;
        if (command == WizardCommand::open) { draftDirty_ = false; commitDeadline_ = 0; }
        if (command == WizardCommand::reset) { draftDirty_ = true; commitDeadline_ = 0; }
        offlineKnown_ = model_.view().vpnState == ConnectionState::disconnected && model_.view().vpnPhase == ConnectionPhase::idle;
        if (command == WizardCommand::open || command == WizardCommand::reset || command == WizardCommand::resume) {
            if (command == WizardCommand::reset) { resetting_ = false; selections_.clear(); }
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
            const bool changesIp = model_.autoDetectIpAddress() && operation->suggestedIp &&
                model_.fields().virtualIp != *operation->suggestedIp;
            const bool applied = model_.applyOperation(*operation, nowMs());
            if (applied && changesIp && previousStep == WizardStep::serverSecret && model_.step() == WizardStep::identity)
                draftChanged();
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
    if (!window_ || endingSession_) return;
    // The confirmed publication has one notification and one bounded close.
    // No polling or lease expiry may re-enter that sequence from the modal loop.
    if (confirmationShown_) return;
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
    windowGeneration_ = nextWindowGeneration(); ++draftGeneration_; ++attemptGeneration_; ++operationGeneration_;
    primary_.stop(); control_.stop(); heartbeat_.stop(); disconnect_.stop(); status_.stop();
    updating_ = true;
    for (const auto& item : children_) {
        const int id = GetDlgCtrlID(item.first);
        if (id == urlId || id == secretId || id == ipId || id == keyId) {
            SetWindowTextW(item.first, L""); SendMessageW(item.first, EM_EMPTYUNDOBUFFER, 0, 0);
        }
    }
    if (window_) { KillTimer(window_, 1); DestroyWindow(window_); }
    updating_ = false; selections_.clear();
    children_.clear(); previousChildren_.clear(); cards_.clear(); separators_.clear(); body_ = steps_ = nullptr;
    model_ = WizardModel{}; commitId_.clear(); commitOperation_.clear();
    draftDirty_ = false; commitDeadline_ = 0; onConfirmed_ = {};
}
HWND WizardWindow::add(const wchar_t* type, const std::wstring& text, DWORD style, int id, int height) {
    const int margin = scaleForDpi(inCard_ ? 48 : 24, dpi_);
    if (type == std::wstring_view(L"BUTTON") && (style & BS_TYPEMASK) == BS_AUTOCHECKBOX) {
        style |= BS_MULTILINE;
        HDC dc = GetDC(body_); auto old = SelectObject(dc, font_);
        RECT bounds{0, 0, std::max(20, contentWidth_ - 2 * margin - scaleForDpi(54, dpi_)), 0};
        DrawTextW(dc, text.c_str(), static_cast<int>(text.size()), &bounds, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
        SelectObject(dc, old); ReleaseDC(body_, dc);
        height = std::max(scaleForDpi(32, dpi_), static_cast<int>(bounds.bottom) + scaleForDpi(6, dpi_));
    }
    if (!height) height = scaleForDpi(32, dpi_);
    if (!id) id = staticId_++;
    HWND child = nullptr;
    for (auto& previous : previousChildren_) if (previous.first && GetDlgCtrlID(previous.first) == id) {
        child = std::exchange(previous.first, nullptr); break;
    }
    const bool created = !child;
    if (created) child = CreateWindowExW(type == std::wstring_view(L"EDIT") ? WS_EX_CLIENTEDGE : 0, type, text.c_str(),
        WS_CHILD | WS_VISIBLE | style, margin, cursorY_, std::max(1, contentWidth_ - 2 * margin), height, body_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance_, nullptr);
    else {
        std::wstring current(static_cast<std::size_t>(GetWindowTextLengthW(child)) + 1, L'\0');
        current.resize(GetWindowTextW(child, current.data(), static_cast<int>(current.size())));
        // In particular, do not reset EDIT undo/selection during resize or polling.
        if (current != text) SetWindowTextW(child, text.c_str());
    }
    if (created && type == std::wstring_view(L"BUTTON") && (style & BS_TYPEMASK) == BS_AUTOCHECKBOX) {
        if (!attachToggleSwitch(child, resources_)) { DestroyWindow(child); return nullptr; }
    }
    EnableWindow(child, TRUE);
    SetPropW(child, L"WizardBackground", reinterpret_cast<HANDLE>(static_cast<INT_PTR>(inCard_ ? cardColor_ : ui::Color::background) + 1));
    SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
    if (type == std::wstring_view(L"EDIT")) SendMessageW(child, EM_SETLIMITTEXT, kMaxConfigFileBytes, 0);
    if (created) if (const auto saved = selections_.find(id); saved != selections_.end())
        SendMessageW(child, EM_SETSEL, saved->second.first, saved->second.second);
    children_.emplace_back(child, Rect{margin, cursorY_, contentWidth_ - margin, cursorY_ + height});
    cursorY_ += height + scaleForDpi(8, dpi_); return child;
}
void WizardWindow::label(const std::wstring& text, bool heading) {
    HDC dc = GetDC(body_); auto old = SelectObject(dc, heading ? headingFont_ : font_);
    RECT bounds{0, 0, std::max(40, contentWidth_ - scaleForDpi(inCard_ ? 96 : 48, dpi_)), 0};
    DrawTextW(dc, text.c_str(), static_cast<int>(text.size()), &bounds, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
    SelectObject(dc, old); ReleaseDC(body_, dc);
    HWND child = add(L"STATIC", text, SS_LEFT | SS_NOPREFIX, 0, std::max(scaleForDpi(18, dpi_), static_cast<int>(bounds.bottom)));
    if (heading) SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(headingFont_), TRUE);
}
void WizardWindow::beginCard(ui::Color color) {
    cardTop_ = cursorY_; cardColor_ = color; inCard_ = true;
    cursorY_ += scaleForDpi(16, dpi_);
}
void WizardWindow::endCard() {
    cursorY_ += scaleForDpi(8, dpi_);
    cards_.emplace_back(Rect{scaleForDpi(24, dpi_), cardTop_, contentWidth_ - scaleForDpi(24, dpi_), cursorY_}, cardColor_);
    cursorY_ += scaleForDpi(16, dpi_); inCard_ = false;
}
void WizardWindow::separator() {
    const int margin = scaleForDpi(inCard_ ? 48 : 24, dpi_);
    separators_.push_back({margin, cursorY_, contentWidth_ - margin, cursorY_ + scaleForDpi(1, dpi_)});
    cursorY_ += scaleForDpi(12, dpi_);
}
void WizardWindow::render() {
    if (!body_) return;
    updating_ = true;
    const HWND previousFocus = GetFocus(); const int focusId = GetDlgCtrlID(previousFocus);
    DWORD selectionStart = 0, selectionEnd = 0;
    const bool textFocus = focusId == urlId || focusId == secretId || focusId == ipId || focusId == keyId;
    if (textFocus) SendMessageW(previousFocus, EM_GETSEL, reinterpret_cast<WPARAM>(&selectionStart), reinterpret_cast<LPARAM>(&selectionEnd));
    previousChildren_ = std::move(children_); children_.clear();
    staticId_ = 1000; cards_.clear(); separators_.clear(); inCard_ = false;
    const int dpi = static_cast<int>(GetDpiForWindow(window_));
    if (!font_ || dpi != dpi_) {
        dpi_ = dpi;
        resources_.update(instance_, dpi_, false);
        font_ = resources_.font(ui::Font::label); headingFont_ = resources_.font(ui::Font::heading);
    }
    layout(); cursorY_ = scaleForDpi(20, dpi_);
    const auto step = model_.step();
    const bool locked = model_.busy() || primary_.busy() || control_.busy() || closing_ || resetting_ || disconnecting_;
    std::wstring steps = L"CONFIGURE THIS COMPUTER\n\n";
    if (step == WizardStep::disconnectGate) steps += L"Disconnect VPN\n\n";
    constexpr const wchar_t* names[]{L"1  Server URL", L"2  Server secret", L"3  IP and access key"};
    for (std::size_t i = 0; i < 3; ++i) {
        steps += names[i];
        steps += model_.checks()[i] == phpvpn::CheckpointResult::success ? L"\n    Completed\n\n" : model_.checks()[i] == phpvpn::CheckpointResult::failed ? L"\n    Failed\n\n" : L"\n    Upcoming\n\n";
    }
    steps += L"4  Communication contracts";
    SetWindowTextW(steps_, steps.c_str()); SendMessageW(steps_, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
    InvalidateRect(steps_, nullptr, TRUE);
    if (step == WizardStep::loading) {
        label(wizardText::configure, true); label(wizardText::loading);
    } else if (step == WizardStep::disconnectGate) {
        label(wizardText::beforeSetup);
        label(wizardText::gateHeading, true);
        label(wizardText::gateDescription);
        beginCard(ui::Color::warningBackground);
        label(wizardText::gateWarning);
        label(wizardText::gateImpact);
        endCard();
        if (offlineKnown_) label(wizardText::offline);
    } else if (step == WizardStep::loadError) {
        label(L"This configuration needs attention", true);
        label(L"The service classified the file as: " + widen(configAvailabilityName(model_.view().availability)) + L".\nExpected file: " + expectedConfigurationPath());
        label(L"Choose Reset to start with the default settings. Nothing will be written until Finish. You can also close this wizard and copy a configuration supplied by your administrator to the path above, or repair valid UTF-8 text using Edit Config.");
    } else if (step == WizardStep::sessionExpired) {
        label(L"Open a new wizard session", true); label(wizardText::expired);
    } else if (step == WizardStep::commitUnknown) {
        label(L"The save result is unknown", true); label(wizardText::unknown);
        label(L"Choose Verify save to read the receipt. If the service restarted, inspect the actual file using Edit Config before starting again.");
    } else if (step == WizardStep::serverUrl) {
        label(wizardText::urlHeading, true);
        label(wizardText::urlDescription);
        beginCard();
        label(wizardText::urlLabel); add(L"EDIT", widen(model_.fields().serverUrl), WS_TABSTOP | ES_AUTOHSCROLL, urlId);
        label(wizardText::urlHint);
        label(wizardText::urlExample);
        separator();
        label(wizardText::urlRules);
        label(wizardText::urlCheck);
        endCard();
    } else if (step == WizardStep::serverSecret) {
        label(wizardText::secretHeading, true);
        label(wizardText::secretDescription);
        beginCard();
        label(model_.secretLiteralMode() ? wizardText::secretLiteral : wizardText::secretLabel);
        auto field = add(L"EDIT", widen(model_.secretEditorText()), WS_TABSTOP | ES_AUTOHSCROLL, secretId);
        EnableWindow(field, !locked && !model_.useDefaultSecret());
        label(model_.secretLiteralMode() ? wizardText::secretLiteralHint : wizardText::secretHint);
        auto box = add(L"BUTTON", wizardText::defaultSecret, WS_TABSTOP | BS_AUTOCHECKBOX, defaultSecretId);
        SendMessageW(box, BM_SETCHECK, model_.useDefaultSecret() ? BST_CHECKED : BST_UNCHECKED, 0);
        if (model_.useDefaultSecret()) label(wizardText::defaultSecretHint);
        separator();
        label(wizardText::secretCheck);
        endCard();
    } else if (step == WizardStep::identity) {
        label(wizardText::identityHeading, true);
        label(wizardText::identityPrefix + widen(model_.view().context->vpnCidr.toString()) +
            wizardText::identityDescription);
        beginCard();
        label(wizardText::ipLabel); auto ipField = add(L"EDIT", widen(model_.fields().virtualIp), WS_TABSTOP | ES_AUTOHSCROLL, ipId);
        EnableWindow(ipField, !locked && !model_.autoDetectIpAddress());
        auto automatic = add(L"BUTTON", wizardText::automaticIp, WS_TABSTOP | BS_AUTOCHECKBOX, automaticIpId);
        SendMessageW(automatic, BM_SETCHECK, model_.autoDetectIpAddress() ? BST_CHECKED : BST_UNCHECKED, 0);
        if (model_.autoDetectIpAddress() && !model_.hasSuggestedIp()) label(wizardText::noSuggestion);
        separator();
        label(wizardText::keyLabel);
        auto field = add(L"EDIT", widen(model_.fields().accessKey), WS_TABSTOP | ES_AUTOHSCROLL, keyId);
        EnableWindow(field, !locked && !model_.useDefaultAccessKey());
        auto box = add(L"BUTTON", wizardText::defaultKey, WS_TABSTOP | BS_AUTOCHECKBOX, defaultKeyId);
        SendMessageW(box, BM_SETCHECK, model_.useDefaultAccessKey() ? BST_CHECKED : BST_UNCHECKED, 0);
        label(wizardText::keyHint);
        if (model_.useDefaultAccessKey()) label(wizardText::defaultKeyHint);
        endCard();
        beginCard(ui::Color::warningBackground); label(wizardText::warning); endCard();
    } else if (step == WizardStep::contracts) {
        label(wizardText::contractsHeading, true);
        label(wizardText::contractsDescription);
        beginCard();
        auto box = add(L"BUTTON", wizardText::addContracts, WS_TABSTOP | BS_AUTOCHECKBOX, contractsId);
        SendMessageW(box, BM_SETCHECK, model_.createContracts() ? BST_CHECKED : BST_UNCHECKED, 0);
        EnableWindow(box, !locked && model_.contractsAllowed());
        separator();
        const std::wstring summary[]{L"Existing contracts:\n" + std::to_wstring(model_.view().contractCount),
            L"Missing defaults:\n" + std::to_wstring(model_.missingContractCount()),
            L"To add on Finish:\n" + std::to_wstring(model_.shouldAddDefaultContracts() ? model_.missingContractCount() : 0)};
        const int rowTop = cursorY_, padding = scaleForDpi(48, dpi_), gap = scaleForDpi(12, dpi_);
        const bool columns = contentWidth_ >= scaleForDpi(620, dpi_);
        const int width = std::max(1, (contentWidth_ - 2 * padding - 2 * gap) / 3);
        int rowBottom = rowTop;
        for (int i = 0; i < 3; ++i) {
            if (columns) cursorY_ = rowTop;
            const auto newline = summary[i].find(L'\n');
            label(summary[i].substr(0, newline));
            const auto headingIndex = children_.size() - 1;
            HWND value = add(L"STATIC", summary[i].substr(newline + 1), SS_LEFT | SS_NOPREFIX, 0, scaleForDpi(32, dpi_));
            SendMessageW(value, WM_SETFONT, reinterpret_cast<WPARAM>(resources_.font(ui::Font::counter)), TRUE);
            if (columns) {
                auto& heading = children_[headingIndex].second;
                auto& number = children_.back().second;
                heading.left = number.left = padding + i * (width + gap);
                heading.right = number.right = heading.left + width;
                HDC dc = GetDC(body_);
                heading.bottom = heading.top + measureText(dc, font_, summary[i].substr(0, newline), width, true).height;
                ReleaseDC(body_, dc);
                number.top = heading.bottom + scaleForDpi(8, dpi_); number.bottom = number.top + scaleForDpi(32, dpi_);
                rowBottom = std::max(rowBottom, number.bottom + scaleForDpi(8, dpi_));
            }
        }
        if (columns) cursorY_ = rowBottom;
        endCard();
        if (!model_.contractsAllowed()) label(wizardText::contractsUnsupported);
        label(wizardText::contractsHint);
        label(wizardText::finishHint);
    } else if (step == WizardStep::finished) {
        label(wizardText::savedHeading, true); label(wizardText::saved);
    }
    if (model_.error()) {
        const auto& failure = *model_.error();
        beginCard(ui::Color::errorBackground);
        label(L"Could not complete this action", true);
        std::wstring text = widen(failure.description) + L"\n" + widen(failure.suggestedAction);
        if (failure.code == "timeout" || failure.code == "transport_timeout") text = wizardText::timeout;
        if (failure.code == "wizard_connection_interrupted") text = wizardText::interrupted;
        if (step == WizardStep::serverUrl && model_.fields().serverUrl.find("MODIFY-THIS-URL") != std::string::npos)
            text = wizardText::urlPlaceholder;
        if (failure.httpStatus) text += L"\nHTTP status: " + std::to_wstring(*failure.httpStatus);
        if (failure.serverUrl) text += L"\nServer URL: " + widen(*failure.serverUrl);
        if (failure.configPath) text += L"\nConfiguration: " + widen(*failure.configPath);
        label(text);
        endCard();
    }
    if (locked) label(model_.committing() ? wizardText::saving : closing_ ? L"Closing this session…" : resetting_ ? L"Stopping the current check and resetting…" : disconnecting_ ? wizardText::waitingDisconnect :
        step == WizardStep::serverUrl ? wizardText::checkingUrl : step == WizardStep::serverSecret ? wizardText::checkingSecret : step == WizardStep::identity ? wizardText::checkingIdentity : L"Waiting for the service…");
    contentHeight_ = cursorY_;
    for (const auto& item : children_) if (locked) EnableWindow(item.first, FALSE);
    for (int id : {nextId, backId, resetId}) SendMessageW(GetDlgItem(window_, id), WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
    const bool back = step == WizardStep::serverSecret || step == WizardStep::identity || step == WizardStep::contracts;
    ShowWindow(GetDlgItem(window_, backId), back ? SW_SHOW : SW_HIDE);
    EnableWindow(GetDlgItem(window_, backId), back && !locked);
    const wchar_t* next = step == WizardStep::contracts ? L"Finish" : step == WizardStep::commitUnknown ? L"Verify save" :
        step == WizardStep::sessionExpired ? L"Open again" : step == WizardStep::disconnectGate ? (offlineKnown_ ? L"Continue" : wizardText::disconnect) : wizardText::verify;
    SetWindowTextW(GetDlgItem(window_, nextId), next);
    EnableWindow(GetDlgItem(window_, nextId), !locked && step != WizardStep::loadError && (step != WizardStep::loading || model_.error().has_value()));
    EnableWindow(GetDlgItem(window_, resetId), !model_.committing() && !closing_ && !resetting_ && step != WizardStep::commitUnknown && step != WizardStep::sessionExpired && !model_.view().sessionId.empty());
    for (const auto& previous : previousChildren_) if (previous.first) {
        wchar_t type[16]{}; GetClassNameW(previous.first, type, 16);
        if (std::wstring_view(type) == L"Edit") {
            auto& selection = selections_[GetDlgCtrlID(previous.first)];
            SendMessageW(previous.first, EM_GETSEL, reinterpret_cast<WPARAM>(&selection.first), reinterpret_cast<LPARAM>(&selection.second));
            SetWindowTextW(previous.first, L""); SendMessageW(previous.first, EM_EMPTYUNDOBUFFER, 0, 0);
        }
        DestroyWindow(previous.first);
    }
    previousChildren_.clear();
    // Reconcile native tab order with the visual order after optional hints or switches change.
    HWND after = HWND_TOP;
    for (const auto& item : children_) {
        SetWindowPos(item.first, after, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        after = item.first;
    }
    const bool stepChanged = renderedStep_ != step;
    renderedStep_ = step;
    if (stepChanged) scroll_ = 0;
    layout(); scroll(scroll_); InvalidateRect(body_, nullptr, TRUE); updating_ = false;
    HWND focus = GetDlgItem(body_, focusId);
    if (!focus) focus = GetDlgItem(window_, focusId);
    if (!stepChanged && focus && IsWindowEnabled(focus) && IsWindowVisible(focus)) {
        SetFocus(focus);
        if (textFocus) SendMessageW(focus, EM_SETSEL, selectionStart, selectionEnd);
    }
    else {
        HWND first = GetNextDlgTabItem(body_, nullptr, FALSE);
        SetFocus(first && IsWindowEnabled(first) ? first : GetDlgItem(window_, nextId));
    }
    RECT actualBody{}; GetClientRect(body_, &actualBody);
    if (actualBody.right != contentWidth_) render(); // Scrollbar visibility changed the wrapping width.
}
void WizardWindow::layout() {
    if (!body_) return;
    RECT rect{}; GetClientRect(window_, &rect);
    HDC dc = GetDC(window_);
    const int primaryWidth = std::max(measuredButtonWidth(dc, font_, wizardText::verify, dpi_),
        measuredButtonWidth(dc, font_, wizardText::disconnect, dpi_));
    ReleaseDC(window_, dc);
    const auto geometry = computeWizardLayout({rect.right, rect.bottom}, dpi_, primaryWidth);
    const auto move = [](HWND child, Rect bounds) { MoveWindow(child, bounds.left, bounds.top, bounds.right - bounds.left, bounds.bottom - bounds.top, TRUE); };
    move(steps_, geometry.steps); move(body_, geometry.content);
    RECT bodyRect{}; GetClientRect(body_, &bodyRect); contentWidth_ = bodyRect.right;
    std::size_t index = 0;
    for (int id : {resetId, backId, nextId}) {
        move(GetDlgItem(window_, id), geometry.buttons[index++]);
    }
}
void WizardWindow::scroll(int position) {
    if (!body_) return;
    RECT rect{}; GetClientRect(body_, &rect);
    scroll_ = std::clamp(position, 0, std::max(0, contentHeight_ - static_cast<int>(rect.bottom)));
    SCROLLINFO info{sizeof(info), SIF_RANGE | SIF_PAGE | SIF_POS, 0, std::max(0, contentHeight_ - 1), static_cast<UINT>(rect.bottom), scroll_, 0};
    SetScrollInfo(body_, SB_VERT, &info, TRUE);
    std::vector<ControlPlacement> positions;
    positions.reserve(children_.size());
    for (const auto& [child, bounds] : children_) positions.push_back({child, {bounds.left, bounds.top - scroll_, bounds.right, bounds.bottom - scroll_}});
    positionControls(body_, positions);
    InvalidateRect(body_, nullptr, TRUE);
}
void WizardWindow::revealFocus() {
    const HWND focus = GetFocus(); RECT rect{}; GetClientRect(body_, &rect);
    for (const auto& [child, bounds] : children_) if (child == focus) {
        if (bounds.top < scroll_) scroll(bounds.top);
        else if (bounds.bottom > scroll_ + rect.bottom) scroll(bounds.bottom - rect.bottom);
    }
}
void WizardWindow::drawSteps(const DRAWITEMSTRUCT& item) {
    FillRect(item.hDC, &item.rcItem, resources_.brush(ui::Color::background));
    auto old = SelectObject(item.hDC, font_); SetBkMode(item.hDC, TRANSPARENT);
    RECT client{}; GetClientRect(window_, &client);
    const bool compact = client.right < scaleForDpi(760, dpi_);
    const int active = model_.step() == WizardStep::serverUrl ? 0 : model_.step() == WizardStep::serverSecret ? 1 :
        model_.step() == WizardStep::identity ? 2 : model_.step() == WizardStep::contracts ? 3 : -1;
    constexpr const wchar_t* names[]{wizardText::urlLabel, L"Server secret", L"IP and access key", L"Contracts"};
    int y = scaleForDpi(12, dpi_);
    if (!compact) {
        SetTextColor(item.hDC, resources_.color(ui::Color::secondaryText));
        RECT heading{0, y, item.rcItem.right, y + scaleForDpi(52, dpi_)};
        DrawTextW(item.hDC, wizardText::configure, -1, &heading, DT_WORDBREAK | DT_NOPREFIX);
        y += scaleForDpi(64, dpi_);
    }
    const int row = scaleForDpi(80, dpi_), width = item.rcItem.right / 4;
    for (int i = 0; i < 4; ++i) {
        const auto state = i < 3 ? model_.checks()[i] : phpvpn::CheckpointResult::pending;
        auto color = state == phpvpn::CheckpointResult::success ? ui::Color::success :
            state == phpvpn::CheckpointResult::failed ? ui::Color::error : ui::Color::secondaryText;
        RECT rect = compact ? RECT{i * width, 0, (i + 1) * width, item.rcItem.bottom} : RECT{0, y, item.rcItem.right, y + row};
        if (i == active) {
            FillRect(item.hDC, &rect, resources_.brush(ui::Color::selectedStep));
            if (state != phpvpn::CheckpointResult::failed) color = ui::Color::primary;
        }
        SelectObject(item.hDC, resources_.font(i == active ? ui::Font::strongValue : ui::Font::label));
        SetTextColor(item.hDC, resources_.color(color));
        InflateRect(&rect, -scaleForDpi(8, dpi_), -scaleForDpi(8, dpi_));
        const int diameter = scaleForDpi(24, dpi_);
        RECT marker{rect.left, rect.top, rect.left + diameter, rect.top + diameter};
        const auto oldBrush = SelectObject(item.hDC, resources_.brush(color));
        const auto oldPen = SelectObject(item.hDC, resources_.pen(color));
        Ellipse(item.hDC, marker.left, marker.top, marker.right, marker.bottom);
        SelectObject(item.hDC, oldBrush); SelectObject(item.hDC, oldPen);
        SetTextColor(item.hDC, resources_.color(ui::Color::primaryText));
        const std::wstring symbol = state == phpvpn::CheckpointResult::success ? L"\u2713" :
            state == phpvpn::CheckpointResult::failed ? L"\u00d7" : std::to_wstring(i + 1);
        DrawTextW(item.hDC, symbol.c_str(), -1, &marker, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        SetTextColor(item.hDC, resources_.color(color));
        if (compact) rect.top += diameter + scaleForDpi(4, dpi_);
        else rect.left += diameter + scaleForDpi(8, dpi_);
        std::wstring text = !compact || i == active ? names[i] : L"";
        if (!compact) text += state == phpvpn::CheckpointResult::success ? L"\nCompleted" :
            state == phpvpn::CheckpointResult::failed ? L"\nFailed" : i == active ? L"" : L"\nUpcoming";
        DrawTextW(item.hDC, text.c_str(), static_cast<int>(text.size()), &rect, DT_WORDBREAK | DT_NOPREFIX);
        y += row;
    }
    SelectObject(item.hDC, old);
}
bool WizardWindow::translate(MSG& message) {
    if (!window_ || (message.hwnd != window_ && !IsChild(window_, message.hwnd))) return false;
    if (message.message == WM_KEYDOWN && message.wParam == VK_ESCAPE) { cancel(false); return true; }
    if (message.message == WM_KEYDOWN && message.wParam == VK_RETURN) {
        const int id = GetDlgCtrlID(GetFocus());
        if (id == resetId || id == backId || id == nextId) command(id, BN_CLICKED); else next();
        return true;
    }
    if (IsDialogMessageW(window_, &message)) { revealFocus(); return true; }
    return false;
}
void WizardWindow::command(int id, int notification) {
    if (updating_ || endingSession_ || !window_) return;
    if (notification == EN_CHANGE && id >= urlId && id <= keyId) {
        if (model_.busy() || model_.committing() || model_.step() == WizardStep::commitUnknown) return;
        // Native text changed even if strict UTF-16 conversion rejects it.
        // Such input must still revoke an earlier discard approval.
        draftChanged();
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
    const bool footerButton = id == nextId || id == backId || id == resetId;
    const HWND button = GetDlgItem(footerButton ? window_ : body_, id);
    if (!button || !IsWindowEnabled(button) || !IsWindowVisible(button)) return;
    if (id == defaultSecretId || id == defaultKeyId || id == automaticIpId || id == contractsId) draftChanged();
    if (id == nextId) next();
    else if (id == backId) { model_.back(); scroll_ = 0; render(); }
    else if (id == resetId) cancel(true);
    else if (id == defaultSecretId) { model_.setDefaultSecret(SendMessageW(GetDlgItem(body_, id), BM_GETCHECK, 0, 0) == BST_CHECKED); render(); }
    else if (id == defaultKeyId) { model_.setDefaultAccessKey(SendMessageW(GetDlgItem(body_, id), BM_GETCHECK, 0, 0) == BST_CHECKED); render(); }
    else if (id == automaticIpId) { model_.setAutoDetectIpAddress(SendMessageW(GetDlgItem(body_, id), BM_GETCHECK, 0, 0) == BST_CHECKED); render(); }
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
        const auto value = reinterpret_cast<INT_PTR>(GetPropW(reinterpret_cast<HWND>(lParam), L"WizardBackground"));
        const auto color = value ? static_cast<ui::Color>(value - 1) : ui::Color::background;
        SetBkColor(reinterpret_cast<HDC>(wParam), self->resources_.color(color));
        SetTextColor(reinterpret_cast<HDC>(wParam), self->resources_.color(ui::Color::text));
        return reinterpret_cast<LRESULT>(self->resources_.brush(color));
    }
    if (message == WM_ERASEBKGND && self->font_) return 1;
    if (message == WM_PAINT && self->font_) {
        PAINTSTRUCT paint{}; const auto dc = BeginPaint(window, &paint);
        FillRect(dc, &paint.rcPaint, self->resources_.brush(ui::Color::background));
        for (const auto& [bounds, color] : self->cards_)
            paintCard(dc, {bounds.left, bounds.top - self->scroll_, bounds.right, bounds.bottom - self->scroll_}, self->resources_, color);
        for (const auto& bounds : self->separators_) {
            RECT rect{bounds.left, bounds.top - self->scroll_, bounds.right, bounds.bottom - self->scroll_};
            FillRect(dc, &rect, self->resources_.brush(ui::Color::separator));
        }
        EndPaint(window, &paint); return 0;
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
        case WM_PAINT:
            if (self->font_ && self->body_) {
                PAINTSTRUCT paint{}; const auto dc = BeginPaint(window, &paint);
                FillRect(dc, &paint.rcPaint, self->resources_.brush(ui::Color::background));
                RECT client{}, reset{}; GetClientRect(window, &client);
                GetWindowRect(GetDlgItem(window, resetId), &reset);
                MapWindowPoints(nullptr, window, reinterpret_cast<POINT*>(&reset), 2);
                const int y = reset.top - scaleForDpi(16, self->dpi_);
                RECT divider{0, y, client.right, y + scaleForDpi(1, self->dpi_)};
                FillRect(dc, &divider, self->resources_.brush(ui::Color::separator));
                EndPaint(window, &paint); return 0;
            }
            break;
        case WM_ERASEBKGND:
            if (self->font_) {
                RECT rect{}; GetClientRect(window, &rect);
                FillRect(reinterpret_cast<HDC>(wParam), &rect, self->resources_.brush(ui::Color::background)); return 1;
            }
            break;
        case WM_NOTIFY: {
            const auto* custom = reinterpret_cast<NMCUSTOMDRAW*>(lParam);
            if (custom && custom->hdr.code == NM_CUSTOMDRAW && custom->dwDrawStage == CDDS_PREPAINT &&
                (custom->hdr.idFrom == nextId || custom->hdr.idFrom == resetId)) {
                const bool primary = custom->hdr.idFrom == nextId, enabled = IsWindowEnabled(custom->hdr.hwndFrom) != FALSE;
                const auto fill = !enabled ? ui::Color::disabledBackground : primary ? ui::Color::primary : ui::Color::background;
                FillRect(custom->hdc, &custom->rc, self->resources_.brush(fill));
                SetBkMode(custom->hdc, TRANSPARENT);
                SetTextColor(custom->hdc, self->resources_.color(!enabled ? ui::Color::disabledText : primary ? ui::Color::primaryText : ui::Color::primary));
                const auto old = SelectObject(custom->hdc, self->font_);
                wchar_t text[128]{}; GetWindowTextW(custom->hdr.hwndFrom, text, 128);
                RECT rect = custom->rc;
                DrawTextW(custom->hdc, text, -1, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                SelectObject(custom->hdc, old);
                if ((custom->uItemState & CDIS_FOCUS) != 0) { InflateRect(&rect, -3, -3); DrawFocusRect(custom->hdc, &rect); }
                return CDRF_SKIPDEFAULT;
            }
            break;
        }
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
        case WM_QUERYENDSESSION: return TRUE;
        case WM_ENDSESSION: if (wParam) self->endSession(); return 0;
        case WM_NCDESTROY: self->window_ = nullptr; SetWindowLongPtrW(window, GWLP_USERDATA, 0); break;
        default: break;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}
} // namespace phpvpn::tray
