// Explicit Win32 UI integration harness. The backend is deterministic test
// state, not a claim about the real service. All windows belong to this PID.
#include "wizard_window.h"
#include "editor_window.h"
#include "contracts_window.h"
#include "window_utils.h"
#include "resource.h"
#include "platform/windows/ipc_pipe.h"
#include "core/config/config_defaults.h"
#include "core/wizard/wizard_defaults.h"
#include "core/crypto/crypto_provider.h"
#include <windows.h>
#include <commctrl.h>
#include <array>
#include <atomic>
#include <condition_variable>
#include <iostream>
#include <map>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>

namespace {
using namespace phpvpn;
using namespace phpvpn::tray;
using J = JsonValue;
constexpr int nextId = 701, backId = 702, resetId = 703, cancelId = 704;
constexpr int urlId = 710, secretId = 711, defaultSecretId = 712, ipId = 713, keyId = 714, defaultKeyId = 715;
constexpr int contractsId = 716, automaticIpId = 717;
constexpr wchar_t wizardClass[] = L"PhpVpnConfigurationWizard";
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
std::string uuid() {
    auto crypto = createPlatformCryptoProvider();
    check(crypto.ok(), "CNG provider unavailable");
    auto id = crypto.value()->randomUuidV4();
    check(id.ok(), "UUID generation failed");
    return id.value();
}
std::wstring privatePipe() { return L"\\\\.\\pipe\\phpvpn-ui-test-" + widen(uuid()); }
J str(std::string value) { return J::makeString(std::move(value)); }
bool ownWindow(HWND window) {
    DWORD pid = 0;
    GetWindowThreadProcessId(window, &pid);
    return pid == GetCurrentProcessId();
}
HWND findTop(const wchar_t* name) {
    struct Search { const wchar_t* name; HWND result = nullptr; } search{name};
    EnumThreadWindows(GetCurrentThreadId(), [](HWND window, LPARAM value) -> BOOL {
        auto& state = *reinterpret_cast<Search*>(value);
        wchar_t name[128]{};
        GetClassNameW(window, name, 128);
        if (ownWindow(window) && std::wstring_view(name) == state.name) { state.result = window; return FALSE; }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&search));
    return search.result;
}
HWND child(HWND parent, int id) {
    check(parent && ownWindow(parent), "test attempted to inspect a foreign or missing parent");
    struct Search { int id; HWND result = nullptr; } search{id};
    EnumChildWindows(parent, [](HWND window, LPARAM value) -> BOOL {
        auto& state = *reinterpret_cast<Search*>(value);
        if (GetDlgCtrlID(window) == state.id) { state.result = window; return FALSE; }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&search));
    return search.result;
}
std::wstring text(HWND window) {
    if (!window) return {};
    check(ownWindow(window), "text read outside test PID");
    std::wstring result(static_cast<std::size_t>(GetWindowTextLengthW(window)) + 1, L'\0');
    result.resize(static_cast<std::size_t>(GetWindowTextW(window, result.data(), static_cast<int>(result.size()))));
    return result;
}
std::wstring allText(HWND parent) {
    std::wstring result = text(parent);
    EnumChildWindows(parent, [](HWND window, LPARAM value) -> BOOL {
        auto& result = *reinterpret_cast<std::wstring*>(value);
        result += L"\n" + text(window);
        return TRUE;
    }, reinterpret_cast<LPARAM>(&result));
    return result;
}
void click(HWND parent, int id) {
    const HWND button = child(parent, id);
    check(button && IsWindowEnabled(button), "button missing or disabled");
    SendMessageW(button, BM_CLICK, 0, 0);
}
void edit(HWND parent, int id, const std::wstring& value) {
    const HWND field = child(parent, id);
    check(field && IsWindowEnabled(field), "edit field missing or disabled");
    check(SetWindowTextW(field, value.c_str()) != FALSE, "SetWindowText failed");
    SendMessageW(field, EM_SETMODIFY, TRUE, 0);
}
int modalAnswer = 0;
void CALLBACK answerModal(HWND, UINT, UINT_PTR, DWORD) {
    if (const auto dialog = findTop(L"#32770")) {
        HWND button = GetDlgItem(dialog, modalAnswer);
        // Windows can expose the sole OK button as IDCANCEL so that Escape
        // dismisses an MB_OK message box. Activate its real HWND in that case.
        if (!button && modalAnswer == IDOK) button = GetDlgItem(dialog, IDCANCEL);
        if (button && IsWindowEnabled(button)) SendMessageW(button, BM_CLICK, 0, 0);
    }
}
template<class Action> void answer(int button, Action action) {
    const int previous = modalAnswer;
    modalAnswer = button;
    const auto timer = SetTimer(nullptr, 0, 20, answerModal);
    check(timer != 0, "modal automation timer unavailable");
    try { action(); } catch (...) { KillTimer(nullptr, timer); modalAnswer = previous; throw; }
    KillTimer(nullptr, timer);
    modalAnswer = previous;
}

struct Backend {
    std::mutex mutex;
    std::condition_variable changed;
    bool stopping = false, editMode = false, heartbeatInterrupt = false, defaultError = false;
    unsigned startDelayMs = 0, resultDelayMs = 0, defaultDelayMs = 0;
    unsigned totalStarts = 0, finishCalls = 0, defaultCalls = 0, resetCalls = 0, closeCalls = 0, heartbeats = 0;
    unsigned configReads = 0, disconnects = 0, resumes = 0, operationsRead = 0;
    unsigned infoStarted = 0, infoCompleted = 0;
    bool releaseInfo = false;
    ULONGLONG disconnectReceivedAt = 0;
    std::uint64_t epoch = 0;
    WizardView view;
    WizardOperationResult operation;
    std::vector<WizardRequest> starts;
    std::string storedSecret = "ui-test-secret", observedSecret;
    std::optional<std::string> suggestion = "10.99.99.2";
    std::optional<WizardRequest> finishedRequest;
    Backend(bool editing = false) : editMode(editing) {
        const auto document = editing ? analyzeWizardDocument(makeDefaultEditorDocument()).value() : makeNewWizardDocument();
        view.sessionId = uuid(); view.serviceInstanceId = uuid();
        view.fields = document.fields; view.context = document.context;
        view.context->runtime.responseTimeoutSeconds = 1;
        view.mode = editing ? WizardMode::edit : WizardMode::create;
        view.availability = editing ? ConfigAvailability::valid : ConfigAvailability::missing;
        view.revisionToken = editing ? "ui-opaque-revision" : "";
        view.contractCount = editing ? 1 : 0;
        if (editing) {
            view.fields->serverUrl = "http://ui-fixture.invalid/phpvpn.php"; view.fields->serverSecret = storedSecret;
            view.fields->useDefaultServerSecretInWizard = false;
            view.fields->autoDetectIpAddressInWizard = false;
            view.fields->useDefaultAccessKeyInWizard = false;
            view.existingDefaultContractPeers = {*Ipv4Address::parse("10.99.99.3")};
        }
    }
    void release() { { std::lock_guard lock(mutex); stopping = true; } changed.notify_all(); }
    unsigned count(unsigned Backend::*member) { std::lock_guard lock(mutex); return this->*member; }
    void delay(std::unique_lock<std::mutex>& lock, unsigned ms) {
        if (ms) changed.wait_for(lock, std::chrono::milliseconds(ms), [&] { return stopping; });
    }
    std::string handle(std::string_view bytes) {
        auto request = WizardIpc::parseRequest(bytes);
        if (!request) {
            auto legacy = IpcProtocol::parseRequest(bytes);
            if (!legacy) return {};
            std::unique_lock lock(mutex);
            if (legacy->command == IpcCommand::get_info) {
                ++infoStarted;
                changed.wait_for(lock, std::chrono::milliseconds(3500), [&] { return stopping || releaseInfo; });
                ++infoCompleted;
                return "{\"uiFixtureInfo\":true}";
            }
            if (legacy->command == IpcCommand::disconnect) {
                ++disconnects; ++epoch; disconnectReceivedAt = GetTickCount64();
            }
            StatusSnapshot snapshot;
            snapshot.stateChangedAtUtc = "2026-09-06T12:00:00.000Z";
            snapshot.clientVersion = "ui-fixture";
            return IpcProtocol::buildStatusResponse(legacy->requestId, snapshot);
        }
        auto& r = request.value();
        std::unique_lock lock(mutex);
        const auto result = [&](J value) { return WizardIpc::buildResult(r.requestId, std::move(value)); };
        switch (r.command) {
        case WizardCommand::open: return result(WizardIpc::viewValue(view));
        case WizardCommand::reset: {
            ++resetCalls;
            auto defaults = makeNewWizardDocument();
            view.mode = WizardMode::reset; view.fields = defaults.fields; view.context = defaults.context;
            view.context->runtime.responseTimeoutSeconds = 1; view.contractCount = 0;
            view.existingDefaultContractPeers.clear();
            return result(WizardIpc::viewValue(view));
        }
        case WizardCommand::resume:
            ++resumes; view.connectionEpoch = epoch; view.vpnState = ConnectionState::disconnected;
            view.vpnPhase = ConnectionPhase::idle;
            return result(WizardIpc::viewValue(view));
        case WizardCommand::keepalive:
            ++heartbeats;
            if (heartbeatInterrupt) {
                heartbeatInterrupt = false; ++epoch;
                return WizardIpc::buildError(r.requestId, WizardIpc::safeError(Error(ErrorCode::wizard_connection_interrupted)));
            }
            return result(J::makeObject({{"alive", J::makeBool(true)}, {"leaseDurationMs", J::makeInteger(kWizardLeaseMs)}}));
        case WizardCommand::check_url: case WizardCommand::check_secret: case WizardCommand::check_access:
            ++totalStarts; starts.push_back(r);
            if (r.command == WizardCommand::check_secret) observedSecret = r.fields.serverSecret;
            operation = {};
            operation.operationId = uuid();
            operation.kind = r.command == WizardCommand::check_url ? WizardOperationKind::url :
                r.command == WizardCommand::check_secret ? WizardOperationKind::secret : WizardOperationKind::access;
            operation.state = WizardOperationState::succeeded; operation.connectionEpoch = epoch;
            operation.clientAttemptId = r.clientAttemptId; operation.attemptSequence = r.attemptSequence;
            if (operation.kind == WizardOperationKind::secret) operation.suggestedIp = suggestion;
            if (startDelayMs) { const auto ms = std::exchange(startDelayMs, 0u); delay(lock, ms); }
            return result(J::makeObject({{"operationId", str(operation.operationId)}, {"operationState", str("pending")}}));
        case WizardCommand::get_operation: {
            ++operationsRead;
            const auto response = operation; // Preserve the captured late result while cancellation arrives.
            if (resultDelayMs) { const auto ms = std::exchange(resultDelayMs, 0u); delay(lock, ms); }
            return result(WizardIpc::operationValue(response));
        }
        case WizardCommand::cancel_attempt:
            return result(J::makeObject({{"attemptSequence", J::makeUnsignedInteger(r.attemptSequence)}, {"attemptState", str("closed")}}));
        case WizardCommand::close:
            ++closeCalls;
            return result(J::makeObject({{"closed", J::makeBool(true)}}));
        case WizardCommand::finish:
            finishedRequest = r;
            ++finishCalls; operation = {};
            operation.operationId = uuid(); operation.kind = WizardOperationKind::finish;
            operation.state = WizardOperationState::succeeded; operation.connectionEpoch = epoch;
            operation.receipt = WizardReceipt{r.commitId, "ui-confirmed-revision"};
            return result(J::makeObject({{"operationId", str(operation.operationId)}, {"operationState", str("pending")}}));
        case WizardCommand::get_default_config:
            ++defaultCalls;
            delay(lock, defaultDelayMs);
            if (defaultError) return WizardIpc::buildError(r.requestId, WizardIpc::safeError(Error(ErrorCode::ipc_unavailable)));
            return result(J::makeObject({{"document", str(makeDefaultEditorDocument())}}));
        case WizardCommand::get_config_state: case WizardCommand::get_config_document:
            ++configReads;
            return WizardIpc::buildError(r.requestId, WizardIpc::safeError(Error(ErrorCode::internal)));
        default: return WizardIpc::buildError(r.requestId, WizardIpc::safeError(Error(ErrorCode::ipc_unknown_command)));
        }
    }
};

struct Gui {
    Backend backend;
    std::wstring pipe = privatePipe();
    IpcPipeServer server;
    ContractsWindow contracts;
    WizardWindow wizard{pipe};
    EditorWindow editor{pipe};
    unsigned confirmed = 0;
    explicit Gui(bool editing = false) : backend(editing), server([this](std::string_view request) { return backend.handle(request); }, options()) {
        check(server.start().ok(), "private UI pipe could not start");
    }
    ~Gui() { backend.release(); }
    IpcPipeServerOptions options() {
        IpcPipeServerOptions options;
        options.pipeName = pipe;
        options.cancelHandlers = [this] { backend.release(); };
        options.classify = [](std::string_view bytes) {
            const auto legacy = IpcProtocol::parseRequest(bytes);
            if (legacy && legacy->command == IpcCommand::get_info) return IpcWorkClass::diagnostic;
            const auto request = WizardIpc::parseRequest(bytes);
            if (request && (request->command == WizardCommand::check_url || request->command == WizardCommand::check_secret ||
                request->command == WizardCommand::check_access)) return IpcWorkClass::diagnostic;
            if (request && (request->command == WizardCommand::get_operation || request->command == WizardCommand::get_default_config))
                return IpcWorkClass::file;
            return IpcWorkClass::shortControl;
        };
        return options;
    }
    void pump() {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            if (!wizard.translate(message) && !editor.translate(message)) { TranslateMessage(&message); DispatchMessageW(&message); }
        }
    }
    template<class Predicate> void until(Predicate predicate, const char* failure, DWORD timeout = 3000) {
        const auto end = GetTickCount64() + timeout;
        do { pump(); if (predicate()) return; MsgWaitForMultipleObjects(0, nullptr, FALSE, 10, QS_ALLINPUT); }
        while (GetTickCount64() < end);
        throw std::runtime_error(failure);
    }
    void wait(DWORD duration) {
        const auto end = GetTickCount64() + duration;
        until([&] { return GetTickCount64() >= end; }, "message pump timed out", duration + 1000);
    }
    HWND hwnd() { return findTop(wizardClass); }
    void open() {
        check(wizard.open(GetModuleHandleW(nullptr), [&] { ++confirmed; check(contracts.show(GetModuleHandleW(nullptr)), "Contracts HWND creation failed"); }),
              "Wizard HWND creation failed");
        until([&] { return hwnd() && child(hwnd(), urlId); }, "Wizard did not load Server URL");
        std::cout << "[ EVIDENCE ] Wizard HWND DPI " << GetDpiForWindow(hwnd()) << ", private test backend\n";
    }
    void nextTo(int field) {
        click(hwnd(), nextId);
        until([&] { return hwnd() && child(hwnd(), field) && IsWindowEnabled(child(hwnd(), nextId)); }, "Next did not reach expected step");
    }
    void escape() {
        MSG message{}; message.hwnd = GetFocus(); message.message = WM_KEYDOWN; message.wParam = VK_ESCAPE;
        check(wizard.translate(message), "Escape was not translated");
        until([&] { return !wizard.isOpen(); }, "Escape did not close the Wizard");
    }
};

void footer(HWND window, std::initializer_list<int> ids) {
    RECT client{}; GetClientRect(window, &client);
    std::vector<RECT> rectangles;
    for (int id : ids) {
        const HWND button = child(window, id);
        check(button != nullptr, "footer button is missing");
        // Back is intentionally hidden on the first step. All other footer
        // actions must remain visible even in a compact client rectangle.
        if (id == backId && !IsWindowVisible(button)) continue;
        check(IsWindowVisible(button) != FALSE, "footer button is hidden");
        RECT rect{}; GetWindowRect(button, &rect);
        MapWindowPoints(nullptr, window, reinterpret_cast<POINT*>(&rect), 2);
        check(rect.left >= 0 && rect.top >= 0 && rect.right <= client.right && rect.bottom <= client.bottom &&
              rect.right > rect.left && rect.bottom > rect.top, "footer button falls outside client area");
        for (auto previous : rectangles) { RECT overlap{}; check(!IntersectRect(&overlap, &rect, &previous), "footer buttons overlap"); }
        rectangles.push_back(rect);
    }
}
void resizeClient(HWND window, int width, int height) {
    RECT rect{0, 0, width, height};
    AdjustWindowRectExForDpi(&rect, static_cast<DWORD>(GetWindowLongPtrW(window, GWL_STYLE)), FALSE,
        static_cast<DWORD>(GetWindowLongPtrW(window, GWL_EXSTYLE)), GetDpiForWindow(window));
    SetWindowPos(window, nullptr, 30, 30, rect.right - rect.left, rect.bottom - rect.top, SWP_NOZORDER | SWP_NOACTIVATE);
}

void happyPath() {
    Gui gui;
    gui.open();
    check(text(child(gui.hwnd(), urlId)).find(L"MODIFY-THIS-URL") != std::wstring::npos, "missing config did not show placeholder URL");
    click(gui.hwnd(), nextId);
    gui.wait(100);
    check(gui.backend.count(&Backend::totalStarts) == 0, "placeholder URL reached the service");
    edit(gui.hwnd(), urlId, L"http://ui-fixture.invalid/phpvpn.php");
    for (auto size : {Size{1000, 680}, Size{480, 320}}) {
        resizeClient(gui.hwnd(), size.width, size.height);
        footer(gui.hwnd(), {nextId, backId, resetId, cancelId});
    }
    SetFocus(child(gui.hwnd(), urlId));
    const HWND initialFocus = GetFocus();
    MSG message{}; message.hwnd = initialFocus; message.message = WM_KEYDOWN; message.wParam = VK_TAB;
    check(gui.wizard.translate(message), "Tab was not handled");
    check(GetFocus() != initialFocus && ownWindow(GetFocus()), "Tab did not move focus to another own control");
    BYTE oldState[256]{}, shifted[256]{};
    GetKeyboardState(oldState);
    std::copy(std::begin(oldState), std::end(oldState), std::begin(shifted));
    shifted[VK_SHIFT] = 0x80;
    SetKeyboardState(shifted);
    message.hwnd = GetFocus();
    const bool shiftHandled = gui.wizard.translate(message);
    SetKeyboardState(oldState);
    check(shiftHandled && GetFocus() == initialFocus, "Shift+Tab did not reverse traversal");
    message.hwnd = initialFocus; message.wParam = VK_RETURN;
    check(gui.wizard.translate(message), "Enter was not handled");
    gui.until([&] { return child(gui.hwnd(), secretId) && IsWindowEnabled(child(gui.hwnd(), nextId)); }, "Enter did not run URL Next");
    gui.nextTo(ipId);
    check(SendMessageW(child(gui.hwnd(), defaultKeyId), BM_GETCHECK, 0, 0) == BST_CHECKED, "new identity did not select default key");
    check(!IsWindowEnabled(child(gui.hwnd(), keyId)), "default key field is editable");
    click(gui.hwnd(), nextId);
    gui.until([&] { return text(child(gui.hwnd(), nextId)) == L"Finish" && IsWindowEnabled(child(gui.hwnd(), nextId)); }, "access Next did not reach contracts");
    check(gui.backend.count(&Backend::totalStarts) == 3, "three explicit Next clicks did not create exactly three probes");
    footer(gui.hwnd(), {nextId, backId, resetId, cancelId});
    click(gui.hwnd(), nextId);
    gui.until([&] { return !gui.wizard.isOpen() && gui.contracts.isOpen(); }, "confirmed Finish did not open Contracts");
    gui.wait(800);
    check(gui.confirmed == 1 && gui.backend.count(&Backend::finishCalls) == 1, "Finish confirmation was repeated");
    check(gui.backend.count(&Backend::closeCalls) == 1, "confirmed Finish did not close its remote session exactly once");
    check(gui.backend.count(&Backend::configReads) == 0, "Finish performed another configuration read");
    const HWND contracts = findTop(L"PhpVpnCommunicationContracts");
    check(contracts && !GetWindow(contracts, GW_CHILD), "Contracts placeholder is not empty");
    check(gui.contracts.show(GetModuleHandleW(nullptr)), "reopening Contracts failed");
    check(findTop(L"PhpVpnCommunicationContracts") == contracts, "Contracts created a second instance");
    gui.contracts.close();
}

void storedIdentityAndLiteral() {
    Gui gui(true);
    const std::string exact = std::string(" space\\literal\n", 15) + std::string("\0", 1) + "tail ";
    {
        std::lock_guard lock(gui.backend.mutex);
        gui.backend.view.fields->serverSecret = exact;
    }
    gui.open();
    gui.nextTo(secretId);
    const auto shown = Json::parse(narrow(text(child(gui.hwnd(), secretId))));
    check(shown && shown->isString() && shown->stringValue() == exact, "control-character secret was not shown as an exact JSON literal");
    gui.nextTo(ipId);
    {
        std::lock_guard lock(gui.backend.mutex);
        check(gui.backend.observedSecret == exact, "JSON literal secret changed before IPC");
    }
    check(SendMessageW(child(gui.hwnd(), defaultKeyId), BM_GETCHECK, 0, 0) == BST_UNCHECKED, "stored key equal to default was wrongly classified as generated");
    check(IsWindowEnabled(child(gui.hwnd(), keyId)), "stored key is not editable");
    click(gui.hwnd(), defaultKeyId);
    edit(gui.hwnd(), ipId, L"10.99.99.10");
    check(text(child(gui.hwnd(), keyId)) == widen(defaultAccessKey("10.99.99.10")), "IP edit did not regenerate checked default key");
    click(gui.hwnd(), defaultKeyId);
    check(IsWindowEnabled(child(gui.hwnd(), keyId)) && text(child(gui.hwnd(), keyId)) == widen(defaultAccessKey("10.99.99.10")),
          "unchecking default restored the old key");
    answer(IDNO, [&] { click(gui.hwnd(), resetId); });
    check(child(gui.hwnd(), ipId) != nullptr && gui.backend.count(&Backend::resetCalls) == 0, "Reset No discarded the current step");
    answer(IDYES, [&] { click(gui.hwnd(), resetId); });
    gui.until([&] { return child(gui.hwnd(), urlId) && IsWindowEnabled(child(gui.hwnd(), nextId)); }, "Reset Yes did not return to URL");
    check(text(child(gui.hwnd(), urlId)).find(L"MODIFY-THIS-URL") != std::wstring::npos, "Reset did not load new defaults");
    check(gui.backend.count(&Backend::finishCalls) == 0, "Reset published configuration");
    gui.escape();
    check(gui.backend.count(&Backend::closeCalls) == 1, "Escape did not close the remote session exactly once");
}

void fix2PreferencesAndContractPreview() {
    for (unsigned count : {0u, 1u, 13u}) {
        Gui gui(true);
        {
            std::lock_guard lock(gui.backend.mutex);
            auto& view = gui.backend.view;
            view.contractCount = count; view.existingDefaultContractPeers.clear();
            if (count == 1) view.existingDefaultContractPeers.push_back(*Ipv4Address::parse("10.99.99.3"));
            if (count == 13) for (auto peer : defaultContractCandidates(view.context->vpnCidr))
                if (peer.toString() != "10.99.99.2") view.existingDefaultContractPeers.push_back(peer);
        }
        gui.open(); gui.nextTo(secretId); gui.nextTo(ipId);
        click(gui.hwnd(), nextId);
        gui.until([&] { return child(gui.hwnd(), contractsId) && IsWindowEnabled(child(gui.hwnd(), nextId)); }, "contracts step missing");
        check(IsWindowVisible(child(gui.hwnd(), contractsId)) && SendMessageW(child(gui.hwnd(), contractsId), BM_GETCHECK, 0, 0) == BST_CHECKED,
              "edit contracts checkbox is not visible and checked");
        check(allText(gui.hwnd()).find(L"Missing default contracts: " + std::to_wstring(13 - count)) != std::wstring::npos, "wrong missing count");
        click(gui.hwnd(), contractsId);
        check(allText(gui.hwnd()).find(L"To add on Finish: 0") != std::wstring::npos, "unchecked contracts still advertised additions");
        footer(gui.hwnd(), {nextId, backId, resetId, cancelId});
        gui.escape();
    }
    Gui gui(true);
    {
        std::lock_guard lock(gui.backend.mutex);
        auto& fields = *gui.backend.view.fields;
        fields.useDefaultServerSecretInWizard = true;
        fields.autoDetectIpAddressInWizard = true;
        fields.useDefaultAccessKeyInWizard = true;
        gui.backend.suggestion = "10.99.99.10";
    }
    gui.open(); gui.nextTo(secretId);
    check(!IsWindowEnabled(child(gui.hwnd(), secretId)) && text(child(gui.hwnd(), secretId)) == L"CHANGE_THIS_SERVER_SECRET", "stored true secret not applied");
    gui.nextTo(ipId);
    check(!IsWindowEnabled(child(gui.hwnd(), ipId)) && text(child(gui.hwnd(), ipId)) == L"10.99.99.10", "automatic suggestion did not replace placeholder");
    check(text(child(gui.hwnd(), keyId)) == widen(defaultAccessKey("10.99.99.10")), "suggestion did not recalculate key");
    for (auto size : {Size{1000, 680}, Size{480, 320}}) {
        resizeClient(gui.hwnd(), size.width, size.height);
        footer(gui.hwnd(), {nextId, backId, resetId, cancelId});
    }
    click(gui.hwnd(), automaticIpId);
    check(IsWindowEnabled(child(gui.hwnd(), ipId)), "automatic IP cannot be disabled");
    edit(gui.hwnd(), ipId, L"10.99.99.11");
    click(gui.hwnd(), nextId);
    gui.until([&] { return child(gui.hwnd(), contractsId) && IsWindowEnabled(child(gui.hwnd(), nextId)); }, "manual identity did not reach contracts");
    click(gui.hwnd(), nextId);
    gui.until([&] { return !gui.wizard.isOpen(); }, "metadata Finish did not complete");
    {
        std::lock_guard lock(gui.backend.mutex);
        check(gui.backend.finishedRequest.has_value(), "Finish fields missing");
        const auto& fields = gui.backend.finishedRequest->fields;
        check(fields.useDefaultServerSecretInWizard && !fields.autoDetectIpAddressInWizard && fields.useDefaultAccessKeyInWizard &&
              fields.virtualIp == "10.99.99.11" && fields.accessKey == defaultAccessKey(fields.virtualIp), "Finish lost metadata or concrete values");
    }
    gui.contracts.close();
}

void fix2MissingSuggestionAndUnsupportedCidr() {
    Gui gui(true);
    {
        std::lock_guard lock(gui.backend.mutex);
        gui.backend.suggestion.reset();
        gui.backend.view.fields->autoDetectIpAddressInWizard = true;
        gui.backend.view.context->vpnCidr = *Ipv4Cidr::parse("10.99.99.0/25");
        gui.backend.view.existingDefaultContractPeers.clear();
    }
    gui.open(); gui.nextTo(secretId); gui.nextTo(ipId);
    check(allText(gui.hwnd()).find(L"No verified IP suggestion") != std::wstring::npos, "missing suggestion explanation absent");
    click(gui.hwnd(), nextId);
    check(gui.backend.count(&Backend::totalStarts) == 2, "unverified automatic IP reached HTTP");
    click(gui.hwnd(), automaticIpId); edit(gui.hwnd(), ipId, L"10.99.99.10");
    click(gui.hwnd(), nextId);
    gui.until([&] { return child(gui.hwnd(), contractsId) && IsWindowEnabled(child(gui.hwnd(), nextId)); }, "manual fallback failed");
    check(IsWindowVisible(child(gui.hwnd(), contractsId)) && !IsWindowEnabled(child(gui.hwnd(), contractsId)) &&
          SendMessageW(child(gui.hwnd(), contractsId), BM_GETCHECK, 0, 0) == BST_CHECKED, "unsupported checkbox state incorrect");
    click(gui.hwnd(), nextId);
    gui.until([&] { return !gui.wizard.isOpen(); }, "unsupported CIDR Finish failed");
    {
        std::lock_guard lock(gui.backend.mutex);
        check(gui.backend.finishedRequest && !gui.backend.finishedRequest->createDefaultContracts, "unsupported CIDR sent true");
    }
    gui.contracts.close();
}

void delayed(bool start) {
    Gui gui;
    {
        std::lock_guard lock(gui.backend.mutex);
        if (start) gui.backend.startDelayMs = 6500;
        else gui.backend.resultDelayMs = 6500;
    }
    gui.open();
    edit(gui.hwnd(), urlId, L"http://ui-fixture.invalid/phpvpn.php");
    click(gui.hwnd(), nextId);
    gui.until([&] { return gui.backend.count(&Backend::totalStarts) == 1; }, "first delayed probe did not start");
    gui.until([&] { return allText(gui.hwnd()).find(L"Could not complete this action") != std::wstring::npos; },
              "lost acknowledgement/result did not become a visible error", 6500);
    gui.wait(2100);
    check(gui.backend.count(&Backend::totalStarts) == 1, "failed Next was retried automatically");
    check(child(gui.hwnd(), urlId) && !child(gui.hwnd(), secretId), "late success advanced a closed attempt");
    check(text(child(gui.hwnd(), urlId)) == L"http://ui-fixture.invalid/phpvpn.php", "IPC failure erased URL input");
    gui.nextTo(secretId);
    check(gui.backend.count(&Backend::totalStarts) == 2, "a new explicit Next did not start one new attempt");
    SendMessageW(gui.hwnd(), WM_CLOSE, 0, 0);
    gui.until([&] { return !gui.wizard.isOpen(); }, "X did not close the window");
    check(gui.backend.count(&Backend::finishCalls) == 0, "X published configuration");
}

void heartbeatEpoch() {
    Gui gui(true);
    { std::lock_guard lock(gui.backend.mutex); gui.backend.heartbeatInterrupt = true; }
    gui.open();
    gui.until([&] { return gui.backend.count(&Backend::heartbeats) >= 1; }, "real 15-second heartbeat was not sent", 16500);
    gui.until([&] { return allText(gui.hwnd()).find(L"Disconnect the VPN before continuing") != std::wstring::npos; },
              "heartbeat epoch interruption did not show disconnect gate");
    gui.until([&] { return text(child(gui.hwnd(), nextId)) == L"Continue"; }, "status reader did not confirm offline gate");
    click(gui.hwnd(), nextId);
    gui.until([&] { return child(gui.hwnd(), urlId) && IsWindowEnabled(child(gui.hwnd(), nextId)); }, "explicit resume did not return to URL");
    check(gui.backend.count(&Backend::resumes) == 1 && gui.backend.count(&Backend::totalStarts) == 0,
          "heartbeat/resume caused an automatic probe");
    click(gui.hwnd(), cancelId);
    gui.until([&] { return !gui.wizard.isOpen(); }, "Cancel did not close the window");
}

void editorReset() {
    Gui gui;
    unsigned saves = 0;
    const std::string original = "{ broken JSON, \xc4\x8d text }";
    gui.editor.open(GetModuleHandleW(nullptr), L"Edit Config UI Test", original, [&](const std::string&) { ++saves; return false; });
    HWND editor = findTop(L"PhpVpnTrayEditor");
    check(editor != nullptr, "Editor did not open");
    for (auto size : {Size{1080, 560}, Size{480, 320}}) {
        resizeClient(editor, size.width, size.height);
        footer(editor, {IDC_SAVE, 304, IDC_CANCEL});
    }
    answer(IDNO, [&] { click(editor, 304); });
    check(text(child(editor, IDC_TEXT_AREA)) == widen(original) && gui.backend.count(&Backend::defaultCalls) == 0, "Reset No changed editor text");
    answer(IDYES, [&] { click(editor, 304); });
    gui.until([&] { return IsWindowEnabled(child(editor, IDC_SAVE)); }, "Editor Reset did not finish");
    auto normalized = narrow(text(child(editor, IDC_TEXT_AREA)));
    normalized.erase(std::remove(normalized.begin(), normalized.end(), '\r'), normalized.end());
    auto expected = makeDefaultEditorDocument();
    expected.erase(std::remove(expected.begin(), expected.end(), '\r'), expected.end());
    check(normalized == expected, "Editor did not load the common service template");
    check(SendMessageW(child(editor, IDC_TEXT_AREA), EM_GETMODIFY, 0, 0) != 0 && saves == 0, "Reset saved to disk or did not mark modified text");
    edit(editor, IDC_TEXT_AREA, L"preserved failed reset");
    { std::lock_guard lock(gui.backend.mutex); gui.backend.defaultError = true; }
    answer(IDOK, [&] {
        // Nested Yes confirmation needs its own temporary answer timer.
        answer(IDYES, [&] { click(editor, 304); });
        modalAnswer = IDOK;
        gui.until([&] { return IsWindowEnabled(child(editor, IDC_SAVE)); }, "failed Reset never finished");
    });
    check(text(child(editor, IDC_TEXT_AREA)) == L"preserved failed reset", "Reset error erased editor text");
    {
        std::lock_guard lock(gui.backend.mutex);
        gui.backend.defaultError = false; gui.backend.defaultDelayMs = 400;
    }
    answer(IDYES, [&] { click(editor, 304); });
    gui.until([&] { return gui.backend.count(&Backend::defaultCalls) == 3; }, "delayed editor reset did not start");
    SendMessageW(editor, WM_CLOSE, 0, 0);
    gui.editor.open(GetModuleHandleW(nullptr), L"Edit Config UI Test", "new generation", [&](const std::string&) { ++saves; return false; });
    editor = findTop(L"PhpVpnTrayEditor");
    gui.wait(700);
    check(text(child(editor, IDC_TEXT_AREA)) == L"new generation", "late Reset result changed a new editor generation");
    click(editor, IDC_CANCEL);
    check(!findTop(L"PhpVpnTrayEditor") && saves == 0, "Cancel called Save");
}

void commandReaderIsolation() {
    Gui gui;
    // Release the diagnostic handler before either CommandReader destructor,
    // including on failure. No real HTTP or service is involved in this test.
    struct Release { Backend& backend; ~Release() { backend.release(); } };
    CommandReader info(gui.pipe), reader(gui.pipe);
    Release release{gui.backend};
    const HWND target = CreateWindowExW(0, L"STATIC", L"private command target", 0, 0, 0, 1, 1,
                                       HWND_MESSAGE, nullptr, GetModuleHandleW(nullptr), nullptr);
    check(target && ownWindow(target), "private message target could not be created");
    check(info.request(IpcCommand::get_info, {}, target, WM_APP + 98), "CommandReader refused get-info");
    gui.until([&] { return gui.backend.count(&Backend::infoStarted) == 1; }, "get-info did not occupy diagnostic worker");
    check(!info.take().has_value(), "held get-info already completed");
    // Busy must be observable, without replacing or silently dropping either
    // the accepted command or the explicit rejection.
    check(!info.request(IpcCommand::get_info, {}, target, WM_APP + 98), "busy CommandReader accepted a second command");
    const auto sent = GetTickCount64();
    check(reader.request(IpcCommand::disconnect, {}, target, WM_APP + 99), "independent CommandReader refused disconnect while get-info busy");
    std::optional<std::string> response;
    gui.until([&] { response = reader.take(); return response.has_value(); }, "CommandReader did not deliver disconnect");
    const auto confirmed = GetTickCount64();
    check(parseStatusResponse(*response).valid && gui.backend.count(&Backend::disconnects) == 1, "CommandReader response was not correlated and valid");
    check(gui.backend.count(&Backend::infoCompleted) == 0, "get-info completed before disconnect confirmation");
    check(confirmed - sent < 1000, "disconnect confirmation exceeded 1000 ms under held get-info");
    {
        std::lock_guard lock(gui.backend.mutex);
        std::cout << "  disconnect send=0 ms, server receive=" << gui.backend.disconnectReceivedAt - sent
                  << " ms, status confirmation=" << confirmed - sent << " ms; get-info still pending\n";
        gui.backend.releaseInfo = true;
    }
    gui.backend.changed.notify_all();
    std::optional<std::string> infoResponse;
    gui.until([&] { infoResponse = info.take(); return infoResponse.has_value(); }, "accepted get-info result was silently dropped");
    const auto parsedInfo = Json::parse(*infoResponse);
    check(parsedInfo && parsedInfo->find("uiFixtureInfo") && parsedInfo->find("uiFixtureInfo")->boolValue(), "get-info result was replaced");
    check(gui.backend.count(&Backend::infoStarted) == 1 && gui.backend.count(&Backend::infoCompleted) == 1, "busy get-info was queued or retried");
    reader.stop();
    DestroyWindow(target);
}

int interactive(std::string_view preview = {}) {
    Gui gui(true);
    gui.open();
    if (preview == "secret" || preview == "identity" || preview == "identity-small" || preview == "contracts")
        gui.nextTo(secretId);
    if (preview == "identity" || preview == "identity-small" || preview == "contracts") gui.nextTo(ipId);
    if (preview == "identity-small") resizeClient(gui.hwnd(), 480, 320);
    if (preview == "contracts") {
        click(gui.hwnd(), nextId);
        gui.until([&] { return text(child(gui.hwnd(), nextId)) == L"Finish" && IsWindowEnabled(child(gui.hwnd(), nextId)); }, "preview contracts did not load");
    }
    // A hidden console launcher must not hide the window being reviewed.
    ShowWindow(gui.hwnd(), SW_SHOW); SetForegroundWindow(gui.hwnd());
    std::cout << "INTERACTIVE UI READY, PID " << GetCurrentProcessId()
              << ", DPI " << GetDpiForWindow(gui.hwnd())
              << ". Private deterministic backend, no installed service or file writes.\n" << std::flush;
    const auto deadline = GetTickCount64() + 45000;
    while (gui.wizard.isOpen() || gui.contracts.isOpen()) {
        gui.pump(); MsgWaitForMultipleObjects(0, nullptr, FALSE, 20, QS_ALLINPUT);
        if (!preview.empty() && GetTickCount64() >= deadline) break;
    }
    return 0;
}
} // namespace

int main(int argc, char** argv) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_STANDARD_CLASSES | ICC_WIN95_CLASSES};
    InitCommonControlsEx(&controls);
    if (argc > 1 && std::string_view(argv[1]) == "--interactive") return interactive();
    if (argc > 2 && std::string_view(argv[1]) == "--preview") return interactive(argv[2]);
    const std::string filter = argc > 1 ? argv[1] : "";
    const std::pair<const char*, void(*)()> tests[]{
        {"happy_path_resize_keyboard_finish", happyPath},
        {"stored_identity_literal_reset_escape", storedIdentityAndLiteral},
        {"fix2_preferences_contract_preview", fix2PreferencesAndContractPreview},
        {"fix2_missing_suggestion_unsupported_cidr", fix2MissingSuggestionAndUnsupportedCidr},
        {"late_start_ack", [] { delayed(true); }},
        {"late_operation_result", [] { delayed(false); }},
        {"heartbeat_epoch_resume_cancel", heartbeatEpoch},
        {"editor_reset_failure_generation", editorReset},
        {"command_reader_private_disconnect", commandReaderIsolation}
    };
    unsigned count = 0, failures = 0;
    for (const auto& [name, run] : tests) {
        if (!filter.empty() && std::string_view(name).find(filter) == std::string_view::npos) continue;
        ++count;
        std::cout << "[ RUN ] " << name << '\n' << std::flush;
        try { run(); std::cout << "[ OK ] " << name << '\n'; }
        catch (const std::exception& error) { ++failures; std::cout << "[ FAIL ] " << name << ": " << error.what() << '\n'; }
    }
    std::cout << count << " real Win32 UI tests, " << failures << " failed.\n";
    return !count || failures ? 1 : 0;
}
