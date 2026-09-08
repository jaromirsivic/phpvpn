// Explicit Win32 UI integration harness. The backend is deterministic test
// state, not a claim about the real service. All windows belong to this PID.
#include "wizard_window.h"
#include "wizard_text.h"
#include "tray_application.h"
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
#include <filesystem>
#include <fstream>
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
#include "ui_diagnostics.inc"
void click(HWND parent, int id) {
    const HWND button = child(parent, id);
    check(button && IsWindowEnabled(button), "button missing or disabled");
    uiDiagnostics.state("click-before", parent); uiDiagnostics.add("click-id", id);
    SendMessageW(button, BM_CLICK, 0, 0);
    uiDiagnostics.state("click-after", parent);
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
    bool lostFinish = false, lostClose = false, gateHeld = false;
    bool pendingFinish = false, holdStatus = false, releaseHeld = false;
    std::optional<WizardCommand> heldCommand;
    unsigned heldStarted = 0, heldCompleted = 0;
    ConnectionState gateState = ConnectionState::connected;
    ConnectionPhase gatePhase = ConnectionPhase::running;
    unsigned commitReads = 0, connects = 0;
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
            if (legacy->command == IpcCommand::connect) ++connects;
            StatusSnapshot snapshot;
            if (gateHeld) { snapshot.state = gateState; snapshot.phase = gatePhase; }
            snapshot.stateChangedAtUtc = "2026-09-06T12:00:00.000Z";
            snapshot.clientVersion = "ui-fixture";
            if (holdStatus && legacy->command == IpcCommand::get_status) {
                ++heldStarted; changed.wait(lock, [&] { return stopping || releaseHeld; }); ++heldCompleted;
            }
            return IpcProtocol::buildStatusResponse(legacy->requestId, snapshot);
        }
        auto& r = request.value();
        uiDiagnostics.add("backend-command:command/sequence", static_cast<UINT_PTR>(r.command), r.attemptSequence);
        std::unique_lock lock(mutex);
        const auto result = [&](J value) {
            if (heldCommand == r.command) {
                ++heldStarted; changed.wait(lock, [&] { return stopping || releaseHeld; }); ++heldCompleted;
            }
            uiDiagnostics.add("backend-reply:command/sequence", static_cast<UINT_PTR>(r.command), r.attemptSequence);
            return WizardIpc::buildResult(r.requestId, std::move(value));
        };
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
            auto visible = response;
            if (pendingFinish && visible.kind == WizardOperationKind::finish) {
                visible.state = WizardOperationState::running; visible.receipt.reset();
            }
            return result(WizardIpc::operationValue(visible));
        }
        case WizardCommand::cancel_attempt:
            return result(J::makeObject({{"attemptSequence", J::makeUnsignedInteger(r.attemptSequence)}, {"attemptState", str("closed")}}));
        case WizardCommand::close:
            ++closeCalls;
            if (lostClose) return {};
            return result(J::makeObject({{"closed", J::makeBool(true)}}));
        case WizardCommand::finish:
            finishedRequest = r;
            ++finishCalls; operation = {};
            operation.operationId = uuid(); operation.kind = WizardOperationKind::finish;
            operation.state = WizardOperationState::succeeded; operation.connectionEpoch = epoch;
            operation.receipt = WizardReceipt{r.commitId, "ui-confirmed-revision"};
            if (lostFinish) return {};
            return result(J::makeObject({{"operationId", str(operation.operationId)}, {"operationState", str("pending")}}));
        case WizardCommand::get_commit:
            ++commitReads;
            check(operation.receipt.has_value(), "receipt query without publication");
            return result(J::makeObject({{"commitState", str("confirmed")}, {"receipt", WizardIpc::receiptValue(*operation.receipt)}, {"error", J::makeNull()}}));
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
    unsigned notifications = 0, closesAtNotification = 0;
    std::function<void()> onSuccessNotification;
    UINT_PTR successTimer = 0;
    inline static Gui* active = nullptr;
    static void CALLBACK acknowledgeSuccess(HWND, UINT, UINT_PTR, DWORD) {
        const auto dialog = findTop(L"#32770");
        if (!active || !dialog || text(dialog) != L"Configuration saved") return;
        uiDiagnostics.add("success-dialog", reinterpret_cast<UINT_PTR>(dialog));
        ++active->notifications;
        active->closesAtNotification = active->backend.count(&Backend::closeCalls);
        if (active->onSuccessNotification) active->onSuccessNotification();
        const auto button = GetDlgItem(dialog, IDOK) ? GetDlgItem(dialog, IDOK) : GetDlgItem(dialog, IDCANCEL);
        if (button) SendMessageW(button, BM_CLICK, 0, 0);
    }
    explicit Gui(bool editing = false) : backend(editing), server([this](std::string_view request) { return backend.handle(request); }, options()) {
        check(server.start().ok(), "private UI pipe could not start");
        active = this;
        successTimer = SetTimer(nullptr, 0, 20, acknowledgeSuccess);
        check(successTimer != 0, "success notification timer unavailable");
    }
    ~Gui() { KillTimer(nullptr, successTimer); active = nullptr; backend.release(); }
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
            // Never record character/key payloads from the input queue.
            uiDiagnostics.add("queued:hwnd/msg/wparam", reinterpret_cast<UINT_PTR>(message.hwnd), message.message,
                message.message == WM_TIMER || message.message == WM_COMMAND || message.message == WM_QUIT ? message.wParam : 0);
            if (!wizard.translate(message) && !editor.translate(message)) { TranslateMessage(&message); DispatchMessageW(&message); }
        }
    }
    template<class Predicate> void until(Predicate predicate, const char* failure, DWORD timeout = 3000) {
        uiDiagnostics.add(failure, timeout); uiDiagnostics.state("until-begin", hwnd());
        const auto end = GetTickCount64() + timeout;
        do { pump(); if (predicate()) { uiDiagnostics.state("until-pass", hwnd()); return; } MsgWaitForMultipleObjects(0, nullptr, FALSE, 10, QS_ALLINPUT); }
        while (GetTickCount64() < end);
        uiDiagnostics.state("until-FAIL", hwnd());
        std::cout << "[ UI TIMEOUT ] " << failure << " limit_ms=" << timeout << " elapsed_ms=" << GetTickCount64() - (end - timeout)
            << " starts=" << backend.count(&Backend::totalStarts) << " operation_reads=" << backend.count(&Backend::operationsRead)
            << " finishes=" << backend.count(&Backend::finishCalls) << " modal=" << (findTop(L"#32770") != nullptr)
            << " focus=" << GetDlgCtrlID(GetFocus()) << '\n';
        if (const HWND root = hwnd()) for (int id : {nextId, urlId, secretId, ipId, contractsId}) {
            const HWND control = child(root, id);
            std::cout << "[ UI CONTROL ] id=" << id << " present=" << (control != nullptr)
                << " visible=" << (IsWindowVisible(control) != FALSE) << " enabled=" << (IsWindowEnabled(control) != FALSE) << '\n';
        }
        throw std::runtime_error(failure);
    }
    void wait(DWORD duration) {
        const auto end = GetTickCount64() + duration;
        until([&] { return GetTickCount64() >= end; }, "message pump timed out", duration + 1000);
    }
    HWND hwnd() { return findTop(wizardClass); }
    void open(bool gate = false) {
        check(wizard.open(GetModuleHandleW(nullptr), [&] { ++confirmed; }),
              "Wizard HWND creation failed");
        until([&] { return hwnd() && (gate ? text(child(hwnd(), nextId)) == wizardText::disconnect : child(hwnd(), urlId) != nullptr); }, "Wizard did not load expected first screen");
        std::cout << "[ EVIDENCE ] Wizard HWND DPI " << GetDpiForWindow(hwnd()) << ", private test backend\n";
    }
    void nextTo(int field) {
        click(hwnd(), nextId);
        const char* failure = field == secretId ? "Next URL -> secret did not reach expected step" :
            field == ipId ? "Next secret -> identity did not reach expected step" : "Next did not reach expected step";
        until([&] { return hwnd() && child(hwnd(), field) && IsWindowEnabled(child(hwnd(), nextId)); }, failure);
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

void captureWizard(HWND window, const wchar_t* name) {
    wchar_t directory[32768]{};
    if (!GetEnvironmentVariableW(L"PHPVPN_WP04_CAPTURE_DIR", directory, 32768)) return;
    check(ownWindow(window), "capture outside fixture process");
    RedrawWindow(window, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
    RECT client{}; GetClientRect(window, &client);
    HDC source = GetDC(window), dc = CreateCompatibleDC(source);
    BITMAPINFO info{}; info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = client.right; info.bmiHeader.biHeight = -client.bottom;
    info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32;
    void* pixels = nullptr;
    HBITMAP bitmap = CreateDIBSection(source, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
    check(bitmap && dc, "capture GDI allocation failed");
    auto old = SelectObject(dc, bitmap);
    const bool printed = PrintWindow(window, dc, PW_CLIENTONLY) != FALSE;
    BITMAPFILEHEADER header{}; header.bfType = 0x4d42;
    header.bfOffBits = sizeof(header) + sizeof(BITMAPINFOHEADER);
    header.bfSize = header.bfOffBits + client.right * client.bottom * 4;
    std::ofstream out(std::filesystem::path(directory) / name, std::ios::binary);
    out.write(reinterpret_cast<const char*>(&header), sizeof(header));
    out.write(reinterpret_cast<const char*>(&info.bmiHeader), sizeof(BITMAPINFOHEADER));
    out.write(static_cast<const char*>(pixels), client.right * client.bottom * 4);
    const bool written = out.good();
    SelectObject(dc, old); DeleteObject(bitmap); DeleteDC(dc); ReleaseDC(window, source);
    check(printed && written, "fixture capture failed");
    std::wcout << L"[ EVIDENCE ] " << name << L": " << client.right << L"x" << client.bottom << L" pixels, DPI " << GetDpiForWindow(window) << L"\n";
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
    gui.until([&] { return !gui.wizard.isOpen(); }, "confirmed Finish did not close Wizard");
    gui.wait(800);
    check(gui.confirmed == 1 && gui.backend.count(&Backend::finishCalls) == 1, "Finish confirmation was repeated");
    check(gui.backend.count(&Backend::closeCalls) == 1, "confirmed Finish did not close its remote session exactly once");
    check(gui.backend.count(&Backend::configReads) == 0, "Finish performed another configuration read");
    check(!findTop(L"PhpVpnCommunicationContracts") && !gui.contracts.isOpen(), "Finish opened Contracts");
    check(gui.notifications == 1 && gui.closesAtNotification == 0, "success notice did not precede exactly one close");
    check(gui.backend.count(&Backend::connects) == 0, "Finish connected VPN");
}

void wizardGateConsentAndIdle() {
    Gui gui(true);
    {
        std::lock_guard lock(gui.backend.mutex);
        gui.backend.gateHeld = true;
        gui.backend.view.vpnState = ConnectionState::connected;
        gui.backend.view.vpnPhase = ConnectionPhase::running;
    }
    gui.open(true);
    captureWizard(gui.hwnd(), L"wizard-gate.bmp");
    check(!IsWindowVisible(child(gui.hwnd(), backId)), "gate exposes Back");
    check(GetFocus() == child(gui.hwnd(), cancelId), "gate focus is not Cancel");
    check(allText(gui.hwnd()).find(L"The VPN will be disconnected for everyone.") != std::wstring::npos, "global consent text missing");
    // No modal-answer helper: a second consent dialog would fail this bounded run.
    click(gui.hwnd(), nextId);
    gui.until([&] { return gui.backend.count(&Backend::disconnects) == 1; }, "gate did not issue one Disconnect");
    {
        std::lock_guard lock(gui.backend.mutex);
        gui.backend.gateState = ConnectionState::disconnected;
        gui.backend.gatePhase = ConnectionPhase::stopping;
    }
    gui.wait(800);
    check(gui.backend.count(&Backend::resumes) == 0 && !child(gui.hwnd(), urlId), "gate accepted disconnected before idle");
    {
        std::lock_guard lock(gui.backend.mutex);
        gui.backend.gateState = ConnectionState::connected;
        gui.backend.gatePhase = ConnectionPhase::idle;
    }
    gui.wait(800);
    check(gui.backend.count(&Backend::resumes) == 0, "gate accepted idle before disconnected");
    {
        std::lock_guard lock(gui.backend.mutex);
        gui.backend.gateState = ConnectionState::disconnected;
    }
    gui.until([&] { return child(gui.hwnd(), urlId) != nullptr; }, "disconnected/idle did not resume");
    check(gui.backend.count(&Backend::resumes) == 1 && gui.backend.count(&Backend::disconnects) == 1, "gate repeated global action");
    check(gui.backend.count(&Backend::totalStarts) == 0, "resume retried checks automatically");
    check(IsWindowVisible(child(gui.hwnd(), backId)) && !IsWindowEnabled(child(gui.hwnd(), backId)), "URL Back must be visible and disabled");
    gui.escape();
}

void wizardStableFieldsAndWarning() {
    Gui gui(true); gui.open();
    captureWizard(gui.hwnd(), L"wizard-url.bmp");
    check(text(child(gui.hwnd(), nextId)) == wizardText::verify, "wrong network action text");
    check(IsWindowVisible(child(gui.hwnd(), backId)) && !IsWindowEnabled(child(gui.hwnd(), backId)), "URL Back state incorrect");
    HWND field = child(gui.hwnd(), urlId);
    const auto original = text(field);
    SetFocus(field); SendMessageW(field, EM_SETSEL, 0, 4); SendMessageW(field, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(L"https"));
    SendMessageW(field, EM_SETSEL, 2, 7);
    for (auto size : {Size{480, 320}, Size{1000, 680}, Size{760, 520}}) {
        resizeClient(gui.hwnd(), size.width, size.height);
        check(child(gui.hwnd(), urlId) == field && GetFocus() == field, "resize replaced the field or focus");
        DWORD start = 0, end = 0; SendMessageW(field, EM_GETSEL, reinterpret_cast<WPARAM>(&start), reinterpret_cast<LPARAM>(&end));
        check(start == 2 && end == 7 && SendMessageW(field, EM_CANUNDO, 0, 0), "resize lost selection or undo");
        footer(gui.hwnd(), {resetId, backId, cancelId, nextId});
    }
    SendMessageW(field, EM_UNDO, 0, 0);
    check(text(field) == original, "undo no longer restores exact URL");
    gui.nextTo(secretId);
    resizeClient(gui.hwnd(), scaleForDpi(1000, static_cast<int>(GetDpiForWindow(gui.hwnd()))), scaleForDpi(680, static_cast<int>(GetDpiForWindow(gui.hwnd()))));
    captureWizard(gui.hwnd(), L"wizard-secret.bmp");
    check(IsWindowEnabled(child(gui.hwnd(), backId)), "Back disabled on secret");
    SendMessageW(child(gui.hwnd(), secretId), EM_SETSEL, 2, 5);
    RECT input{}, toggle{}; GetWindowRect(child(gui.hwnd(), secretId), &input); GetWindowRect(child(gui.hwnd(), defaultSecretId), &toggle);
    check(input.bottom < toggle.top, "secret switch is above field");
    gui.nextTo(ipId);
    resizeClient(gui.hwnd(), scaleForDpi(1000, static_cast<int>(GetDpiForWindow(gui.hwnd()))), scaleForDpi(680, static_cast<int>(GetDpiForWindow(gui.hwnd()))));
    check(IsWindowEnabled(child(gui.hwnd(), backId)), "Back disabled on identity");
    click(gui.hwnd(), backId);
    DWORD restoredStart = 0, restoredEnd = 0;
    SendMessageW(child(gui.hwnd(), secretId), EM_GETSEL, reinterpret_cast<WPARAM>(&restoredStart), reinterpret_cast<LPARAM>(&restoredEnd));
    check(restoredStart == 2 && restoredEnd == 5, "Back lost secret selection");
    gui.nextTo(ipId);
    for (const auto pair : {std::pair{ipId, automaticIpId}, std::pair{keyId, defaultKeyId}}) {
        GetWindowRect(child(gui.hwnd(), pair.first), &input); GetWindowRect(child(gui.hwnd(), pair.second), &toggle);
        check(input.bottom < toggle.top, "identity switch is above field");
    }
    click(gui.hwnd(), defaultKeyId);
    captureWizard(gui.hwnd(), L"wizard-identity.bmp");
    HWND warning = nullptr;
    EnumChildWindows(gui.hwnd(), [](HWND window, LPARAM value) -> BOOL {
        if (text(window) == wizardText::warning) *reinterpret_cast<HWND*>(value) = window;
        return TRUE;
    }, reinterpret_cast<LPARAM>(&warning));
    check(warning != nullptr, "full destructive-message warning missing");
    RECT bounds{}, body{}; GetWindowRect(warning, &bounds); GetWindowRect(GetParent(warning), &body);
    check(bounds.left >= body.left && bounds.right <= body.right && bounds.top >= body.top && bounds.bottom <= body.bottom,
          "identity warning not entirely visible at default size");
    check(allText(gui.hwnd()).find(L"A suggested IP is not a reservation.") != std::wstring::npos, "IP reservation warning missing");
    gui.escape();
}

void wizardResources() {
    const auto cycle = [](bool stress) {
        Gui gui(true); gui.open();
        if (stress) {
            const auto process = GetCurrentProcess();
            RedrawWindow(gui.hwnd(), nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
            const auto gdi = GetGuiResources(process, GR_GDIOBJECTS), user = GetGuiResources(process, GR_USEROBJECTS);
            for (int i = 0; i < 500; ++i) RedrawWindow(gui.hwnd(), nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
            check(GetGuiResources(process, GR_GDIOBJECTS) == gdi && GetGuiResources(process, GR_USEROBJECTS) == user, "Wizard paint leaks native objects");
        }
        gui.escape();
    };
    cycle(false);
    const auto process = GetCurrentProcess();
    const auto gdi = GetGuiResources(process, GR_GDIOBJECTS), user = GetGuiResources(process, GR_USEROBJECTS);
    cycle(true);
    for (int i = 0; i < 20; ++i) cycle(false);
    const auto gdiAfter = GetGuiResources(process, GR_GDIOBJECTS), userAfter = GetGuiResources(process, GR_USEROBJECTS);
    std::cout << "[ EVIDENCE ] Wizard 500 paints and 20 cycles: GDI " << gdiAfter << " vs " << gdi << ", USER " << userAfter << " vs " << user << "\n";
    check(gdiAfter <= gdi + 5 && userAfter <= user + 5, "Wizard open/close leaks native objects");
}
class WizardFixtureShell final : public ShellAdapter {
public:
    bool notify(DWORD, NOTIFYICONDATAW&) override { return true; }
};
void wizardProductionFinish(bool lost) {
    Gui gui(true);
    {
        std::lock_guard lock(gui.backend.mutex);
        gui.backend.lostFinish = lost; gui.backend.lostClose = lost;
    }
    TrayApplication app({gui.pipe, L"Local\\phpvpn-wp04-" + widen(uuid()), std::make_unique<WizardFixtureShell>(), L"WP04 synthetic"});
    check(app.initialise(GetModuleHandleW(nullptr)), "production TrayApplication could not initialise");
    SendMessageW(app.window(), WM_COMMAND, 206, 0);
    gui.until([&] { return gui.hwnd() && child(gui.hwnd(), urlId); }, "production Wizard action failed");
    gui.nextTo(secretId); gui.nextTo(ipId); gui.nextTo(contractsId);
    captureWizard(gui.hwnd(), L"wizard-contracts.bmp");
    check(allText(gui.hwnd()).find(wizardText::finishHint) != std::wstring::npos, "Finish hint promises wrong action");
    click(gui.hwnd(), nextId);
    if (lost) {
        gui.until([&] { return text(child(gui.hwnd(), nextId)) == L"Verify save" && IsWindowEnabled(child(gui.hwnd(), nextId)); }, "lost Finish did not offer Verify save", 6500);
        gui.wait(700);
        check(gui.notifications == 0 && gui.backend.count(&Backend::commitReads) == 0 && gui.backend.count(&Backend::finishCalls) == 1,
              "lost Finish guessed success or retried");
        click(gui.hwnd(), nextId);
    }
    gui.until([&] { return !gui.hwnd(); }, "production Finish did not close", 6500);
    gui.wait(800);
    check(gui.notifications == 1 && gui.closesAtNotification == 0, "notification/close order wrong");
    check(gui.backend.count(&Backend::finishCalls) == 1 && gui.backend.count(&Backend::closeCalls) == 1, "Finish or close repeated");
    check(gui.backend.count(&Backend::commitReads) == (lost ? 1u : 0u), "receipt query count wrong");
    check(gui.backend.count(&Backend::configReads) == 0 && gui.backend.count(&Backend::connects) == 0, "Finish read config or connected");
    check(!findTop(L"PhpVpnCommunicationContracts"), "production Finish opened Contracts");
    std::cout << "[ EVIDENCE ] production Finish=1 close=1 configReads=0 Connect=0 Contracts=0 getCommit=" << (lost ? 1 : 0) << "\n";
}
void wizardLifecycleSnapshotAndDiscard() {
    Gui gui(true); gui.open();
    const auto clean = gui.wizard.lifecycleSnapshot();
    check(clean.open && !clean.dirty && !clean.committing && !clean.commitUnknown && !clean.commitDeadlineMs,
        "initial lifecycle is not clean");
    check(clean.windowGeneration && clean.sessionGeneration && clean.draftGeneration, "missing lifecycle identity");
    const wchar_t invalidUtf16[]{static_cast<wchar_t>(0xd800), 0};
    check(SetWindowTextW(child(gui.hwnd(), urlId), invalidUtf16) != FALSE, "invalid native input setup failed");
    const auto invalidDraft = gui.wizard.lifecycleSnapshot();
    check(invalidDraft.dirty && invalidDraft.draftGeneration > clean.draftGeneration && !gui.wizard.approvedDiscard(clean),
        "rejected UTF-16 input reused clean approval");
    const auto field = child(gui.hwnd(), urlId);
    SetFocus(field); SendMessageW(field, EM_SETSEL, 0, 4);
    SendMessageW(field, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(L"https"));
    const auto draft = gui.wizard.lifecycleSnapshot();
    check(draft.dirty && draft.windowGeneration == clean.windowGeneration && draft.draftGeneration > clean.draftGeneration,
        "edit did not advance draft identity");
    check(!gui.wizard.approvedDiscard(clean), "old clean snapshot discarded changed draft");
    SendMessageW(field, EM_UNDO, 0, 0);
    const auto undone = gui.wizard.lifecycleSnapshot();
    check(undone.draftGeneration > draft.draftGeneration && !gui.wizard.approvedDiscard(draft), "undo reused approval");
    DWORD first = 0, last = 0;
    SendMessageW(field, EM_GETSEL, reinterpret_cast<WPARAM>(&first), reinterpret_cast<LPARAM>(&last));
    const auto value = text(field);
    for (int i = 0; i < 20; ++i) check(gui.wizard.lifecycleSnapshot() == undone, "snapshot mutates state");
    DWORD afterFirst = 0, afterLast = 0;
    SendMessageW(field, EM_GETSEL, reinterpret_cast<WPARAM>(&afterFirst), reinterpret_cast<LPARAM>(&afterLast));
    check(field == child(gui.hwnd(), urlId) && value == text(field) && first == afterFirst && last == afterLast,
        "snapshot disturbed native field");
    check(gui.wizard.approvedDiscard(undone) && !gui.wizard.isOpen(), "approved discard did not close immediately");
    gui.open();
    check(!gui.wizard.approvedDiscard(undone), "old window snapshot closed reopened Wizard");
    const auto reopened = gui.wizard.lifecycleSnapshot();
    check(reopened.windowGeneration != undone.windowGeneration, "reopen reused window generation");
    gui.nextTo(secretId);
    const auto attempted = gui.wizard.lifecycleSnapshot();
    check(attempted.attemptSequence == 1 && attempted.attemptGeneration > reopened.attemptGeneration &&
        attempted.operationGeneration > reopened.operationGeneration && !gui.wizard.approvedDiscard(reopened),
        "attempt reused approval identity");
    answer(IDYES, [&] { click(gui.hwnd(), resetId); });
    gui.until([&] { return child(gui.hwnd(), urlId) && IsWindowEnabled(child(gui.hwnd(), nextId)); }, "reset did not settle");
    const auto reset = gui.wizard.lifecycleSnapshot();
    check(reset.dirty && reset.sessionGeneration > attempted.sessionGeneration && reset.attemptSequence == 0 &&
        !gui.wizard.approvedDiscard(attempted), "reset retained old approval");
    check(gui.wizard.approvedDiscard(reset), "reset draft discard failed");
    gui.wait(100);
    check(gui.backend.count(&Backend::finishCalls) == 0 && gui.backend.count(&Backend::commitReads) == 0 &&
        gui.backend.count(&Backend::configReads) == 0 && gui.backend.count(&Backend::closeCalls) == 0 &&
        gui.backend.count(&Backend::connects) == 0 && gui.backend.count(&Backend::disconnects) == 0 && gui.notifications == 0,
        "approved discard triggered IPC or success UI");
    check(!findTop(L"#32770") && !findTop(L"PhpVpnCommunicationContracts"), "discard created a dialog or Contracts");
}
void wizardLifecycleCommitDeadline() {
    Gui gui(true); gui.open();
    gui.nextTo(secretId); gui.nextTo(ipId); gui.nextTo(contractsId);
    { std::lock_guard lock(gui.backend.mutex); gui.backend.pendingFinish = true; }
    const auto before = gui.wizard.lifecycleSnapshot();
    const auto pressed = nowMs();
    click(gui.hwnd(), nextId);
    const auto committing = gui.wizard.lifecycleSnapshot();
    check(committing.committing && !committing.commitUnknown && committing.commitDeadlineMs &&
        *committing.commitDeadlineMs >= pressed + 15000 && *committing.commitDeadlineMs <= nowMs() + 15000,
        "Finish did not preserve original 15s deadline");
    check(committing.operationGeneration > before.operationGeneration &&
        committing.attemptSequence == before.attemptSequence && !gui.wizard.approvedDiscard(committing),
        "committing is discardable or changed attempt sequence");
    check(SendMessageW(gui.hwnd(), WM_QUERYENDSESSION, 0, ENDSESSION_LOGOFF) == TRUE, "query blocks shutdown");
    SendMessageW(gui.hwnd(), WM_ENDSESSION, FALSE, ENDSESSION_LOGOFF);
    check(gui.wizard.lifecycleSnapshot() == committing, "query/false-end mutated Wizard");
    gui.until([&] {
        const auto current = gui.wizard.lifecycleSnapshot();
        check(current.commitDeadlineMs == committing.commitDeadlineMs && current.operationGeneration == committing.operationGeneration &&
            current.sessionGeneration == committing.sessionGeneration && current.windowGeneration == committing.windowGeneration,
            "polling renewed commit identity or deadline");
        return current.commitUnknown;
    }, "commit did not expire at original deadline", 16500);
    check(nowMs() >= *committing.commitDeadlineMs && nowMs() < *committing.commitDeadlineMs + 750,
        "commit timeout exceeded original deadline tolerance");
    const auto unknown = gui.wizard.lifecycleSnapshot();
    check(!unknown.committing && !gui.wizard.approvedDiscard(committing), "stale committing snapshot discarded unknown");
    check(gui.wizard.approvedDiscard(unknown), "unknown discard failed");
    gui.wait(100);
    check(gui.backend.count(&Backend::finishCalls) == 1 && gui.backend.count(&Backend::commitReads) == 0 &&
        gui.backend.count(&Backend::closeCalls) == 0 && gui.backend.count(&Backend::configReads) == 0 &&
        gui.backend.count(&Backend::connects) == 0 && gui.backend.count(&Backend::disconnects) == 0 && gui.notifications == 0,
        "deadline/discard retried, verified or claimed success");
    std::cout << "[ EVIDENCE ] original commit deadline=15000ms; Finish=1 getCommit=0 close=0 config=0 Connect=0 Disconnect=0\n";
}
void wizardLifecycleEndSessionCycles() {
    ULONGLONG worst = 0;
    for (unsigned i = 0; i < 24; ++i) {
        Gui gui(true);
        auto wizard = std::make_unique<WizardWindow>(gui.pipe);
        unsigned callbacks = 0;
        if (i % 3 == 0) { std::lock_guard lock(gui.backend.mutex); gui.backend.heldCommand = WizardCommand::open; }
        check(wizard->open(GetModuleHandleW(nullptr), [&] { ++callbacks; }), "cycle open failed");
        if (i % 3 != 0) {
            gui.until([&] { return gui.hwnd() && child(gui.hwnd(), urlId); }, "cycle did not load");
            if (i % 3 == 1) {
                { std::lock_guard lock(gui.backend.mutex); gui.backend.heldCommand = WizardCommand::check_url; }
                click(gui.hwnd(), nextId);
            } else {
                gui.nextTo(secretId); gui.nextTo(ipId); gui.nextTo(contractsId);
                { std::lock_guard lock(gui.backend.mutex); gui.backend.heldCommand = WizardCommand::finish; }
                click(gui.hwnd(), nextId);
                check(wizard->lifecycleSnapshot().committing, "cycle did not enter committing");
            }
        }
        gui.until([&] { return gui.backend.count(&Backend::heldStarted) == 1; }, "cycle reader did not block");
        const auto state = wizard->lifecycleSnapshot();
        const HWND oldWindow = gui.hwnd();
        check(SendMessageW(oldWindow, WM_QUERYENDSESSION, 0, ENDSESSION_LOGOFF) == TRUE, "cycle query rejected");
        SendMessageW(oldWindow, WM_ENDSESSION, FALSE, ENDSESSION_LOGOFF);
        check(state == wizard->lifecycleSnapshot(), "cycle false-end changed state");
        const auto started = GetTickCount64();
        SendMessageW(oldWindow, WM_ENDSESSION, TRUE, ENDSESSION_LOGOFF);
        const auto closed = wizard->lifecycleSnapshot();
        check(!closed.open && closed.windowGeneration != state.windowGeneration && closed.sessionGeneration != state.sessionGeneration &&
            closed.attemptGeneration != state.attemptGeneration && closed.operationGeneration != state.operationGeneration,
            "end-session did not invalidate identities");
        wizard->endSession();
        check(closed == wizard->lifecycleSnapshot() && !wizard->open(GetModuleHandleW(nullptr)), "end-session is not terminal/idempotent");
        wizard.reset();
        worst = std::max(worst, GetTickCount64() - started);
        check(worst < 250 && gui.backend.count(&Backend::heldCompleted) == 0, "cleanup waited for reader");
        { std::lock_guard lock(gui.backend.mutex); gui.backend.releaseHeld = true; }
        gui.backend.changed.notify_all();
        gui.until([&] { return gui.backend.count(&Backend::heldCompleted) == 1; }, "held reader did not release");
        gui.wait(50);
        check(callbacks == 0 && gui.notifications == 0 && !gui.hwnd() && !findTop(L"#32770"), "late result reached dead UI");
        check(gui.backend.count(&Backend::finishCalls) == (i % 3 == 2 ? 1u : 0u) && gui.backend.count(&Backend::closeCalls) == 0 &&
            gui.backend.count(&Backend::commitReads) == 0 && gui.backend.count(&Backend::configReads) == 0 &&
            gui.backend.count(&Backend::connects) == 0 && gui.backend.count(&Backend::disconnects) == 0, "session-end sent unexpected IPC");
    }
    std::cout << "[ EVIDENCE ] 24 create/read-or-check-or-commit/end-session/destroy cycles; worst cleanup=" << worst << "ms; late callback=0\n";
}
void wizardLifecycleHeldHeartbeatAndGate() {
    for (bool gate : {false, true}) {
        Gui gui(true);
        { std::lock_guard lock(gui.backend.mutex);
            if (gate) { gui.backend.view.vpnState = ConnectionState::connected; gui.backend.view.vpnPhase = ConnectionPhase::running; gui.backend.holdStatus = true; }
            else gui.backend.heldCommand = WizardCommand::keepalive;
        }
        gui.open(gate);
        gui.until([&] { return gui.backend.count(&Backend::heldStarted) >= 1; }, "heartbeat/status did not block", 17000);
        const auto start = GetTickCount64();
        gui.wizard.endSession();
        check(GetTickCount64() - start < 250 && gui.backend.count(&Backend::heldCompleted) == 0, "end-session joined heartbeat/status");
        gui.backend.release(); gui.wait(100);
        check(!gui.wizard.isOpen() && gui.confirmed == 0 && gui.notifications == 0 && !findTop(L"#32770"), "late heartbeat/status changed UI");
        check(gui.backend.count(&Backend::closeCalls) == 0 && gui.backend.count(&Backend::resumes) == 0 &&
            gui.backend.count(&Backend::disconnects) == 0 && gui.backend.count(&Backend::finishCalls) == 0, "session-end performed remote action");
    }
}
void wizardLifecycleApplicationWrappers() {
    Gui gui(true);
    TrayApplication app({gui.pipe, L"Local\\phpvpn-wp04-seam-" + widen(uuid()), std::make_unique<WizardFixtureShell>(), L"WP04 synthetic"});
    check(app.initialise(GetModuleHandleW(nullptr)), "app seam initialisation failed");
    SendMessageW(app.window(), WM_COMMAND, 206, 0);
    gui.until([&] { return gui.hwnd() && child(gui.hwnd(), urlId); }, "app Wizard did not load");
    const auto state = app.wizardLifecycleSnapshot();
    check(state.open && app.approvedDiscardWizard(state) && !app.wizardLifecycleSnapshot().open, "app discard wrapper failed");
    SendMessageW(app.window(), WM_COMMAND, 206, 0);
    gui.until([&] { return gui.hwnd() && child(gui.hwnd(), urlId); }, "app Wizard did not reopen");
    check(!app.approvedDiscardWizard(state), "app wrapper accepted stale generation");
    app.endWizardSession();
    check(!app.wizardLifecycleSnapshot().open && IsWindow(app.window()), "participant changed application lifecycle");
    check(gui.backend.count(&Backend::closeCalls) == 0 && gui.backend.count(&Backend::disconnects) == 0, "wrapper sent unexpected action");
}
void wizardLifecycleEndInsideSuccessNotification() {
    Gui gui(true); gui.open();
    gui.nextTo(secretId); gui.nextTo(ipId); gui.nextTo(contractsId);
    click(gui.hwnd(), contractsId);
    check(gui.wizard.lifecycleSnapshot().dirty, "Finish setup did not make a dirty draft");
    gui.onSuccessNotification = [&] {
        const auto saved = gui.wizard.lifecycleSnapshot();
        check(saved.open && !saved.dirty && !saved.committing && !saved.commitUnknown, "confirmed snapshot still claims unsaved changes");
        gui.wizard.endSession();
    };
    click(gui.hwnd(), nextId);
    gui.until([&] { return !gui.wizard.isOpen(); }, "end-session inside success notice did not close");
    gui.wait(100);
    check(gui.notifications == 1 && gui.confirmed == 0 && !findTop(L"#32770"), "modal unwind delivered late callback");
    check(gui.backend.count(&Backend::finishCalls) == 1 && gui.backend.count(&Backend::closeCalls) == 0 &&
        gui.backend.count(&Backend::commitReads) == 0 && gui.backend.count(&Backend::configReads) == 0 &&
        gui.backend.count(&Backend::disconnects) == 0, "modal unwind started IPC after end-session");
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
        check(allText(gui.hwnd()).find(L"Missing defaults:\n" + std::to_wstring(13 - count)) != std::wstring::npos, "wrong missing count");
        click(gui.hwnd(), contractsId);
        check(allText(gui.hwnd()).find(L"To add on Finish:\n0") != std::wstring::npos, "unchecked contracts still advertised additions");
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
    STARTUPINFOW startup{sizeof(startup)};
    GetStartupInfoW(&startup);
    std::cout << "[ EDITOR LAUNCH ] startupHidden="
        << ((startup.dwFlags & STARTF_USESHOWWINDOW) && startup.wShowWindow == SW_HIDE)
        << " console=" << (GetConsoleWindow() != nullptr)
        << " visible=" << (IsWindowVisible(editor) != FALSE)
        << " focus=" << (GetFocus() == child(editor, IDC_TEXT_AREA)) << '\n' << std::flush;
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
    answer(IDYES, [&] { SendMessageW(editor, WM_CLOSE, 0, 0); });
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
#include "dev_preview.inc"

// A single short, isolated regression check for the native switch skin.
void modernToggleSwitches() {
    const auto checked = [](HWND h) { return SendMessageW(h, BM_GETCHECK, 0, 0) == BST_CHECKED; };
    const auto space = [](HWND h) {
        SetFocus(h);
        SendMessageW(h, WM_KEYDOWN, VK_SPACE, 0);
        SendMessageW(h, WM_KEYUP, VK_SPACE, 0);
    };
    const auto accessibility = [&](HWND h, const std::wstring& expected, bool action) {
        IAccessible* acc = nullptr;
        check(SUCCEEDED(AccessibleObjectFromWindow(h, static_cast<DWORD>(OBJID_CLIENT), IID_IAccessible,
            reinterpret_cast<void**>(&acc))), "switch accessibility missing");
        VARIANT self{}; self.vt = VT_I4; self.lVal = CHILDID_SELF;
        BSTR name = nullptr; VARIANT role{}, state{};
        const bool named = SUCCEEDED(acc->get_accName(self, &name)) && name && expected == std::wstring(name, SysStringLen(name));
        SysFreeString(name);
        const bool validRole = SUCCEEDED(acc->get_accRole(self, &role)) && role.vt == VT_I4 && role.lVal == ROLE_SYSTEM_CHECKBUTTON;
        const bool validState = SUCCEEDED(acc->get_accState(self, &state)) && state.vt == VT_I4 &&
            ((state.lVal & STATE_SYSTEM_CHECKED) != 0) == checked(h) &&
            ((state.lVal & STATE_SYSTEM_UNAVAILABLE) != 0) == !IsWindowEnabled(h);
        const bool before = checked(h);
        const bool acted = !action || SUCCEEDED(acc->accDoDefaultAction(self));
        if (action) {
            MSG message{};
            while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
                TranslateMessage(&message); DispatchMessageW(&message);
            }
        }
        acc->Release();
        check(named, "native switch name mismatch"); check(validRole, "native switch role mismatch");
        check(validState, "native switch state mismatch");
        check(acted && (!action || checked(h) != before), "native switch action mismatch");
    };
    Gui gui(true); gui.open(); gui.nextTo(secretId);
    const auto exercise = [&](int id, const wchar_t* capture) {
        const auto h = child(gui.hwnd(), id);
        check(h && IsWindowVisible(h) && IsWindowEnabled(h), "wizard switch missing");
        const bool initial = checked(h);
        space(h); check(checked(h) != initial, "Space did not toggle");
        space(h); check(checked(h) == initial, "Space did not restore");
        // Click on the text well beyond the switch track.
        RECT rect{}; GetClientRect(h, &rect);
        const LPARAM point = MAKELPARAM(MulDiv(80, GetDpiForWindow(h), 96), rect.bottom / 2);
        SendMessageW(h, WM_LBUTTONDOWN, MK_LBUTTON, point);
        SendMessageW(h, WM_LBUTTONUP, 0, point);
        check(checked(h) != initial, "label click did not toggle");
        accessibility(h, text(h), true);
        check(checked(h) == initial, "accessible action did not restore");
        EnableWindow(h, FALSE); accessibility(h, text(h), false);
        captureWizard(gui.hwnd(), L"toggle-disabled.bmp");
        EnableWindow(h, TRUE);
        SetFocus(h); SendMessageW(h, WM_UPDATEUISTATE, MAKEWPARAM(UIS_CLEAR, UISF_HIDEFOCUS), 0);
        captureWizard(gui.hwnd(), capture);
        MSG tab{}; tab.hwnd = h; tab.message = WM_KEYDOWN; tab.wParam = VK_TAB;
        check(gui.wizard.translate(tab) && GetFocus() != h, "Tab did not leave switch");
    };
    exercise(defaultSecretId, L"toggle-secret.bmp");
    gui.nextTo(ipId);
    exercise(automaticIpId, L"toggle-automatic.bmp");
    exercise(defaultKeyId, L"toggle-key.bmp");
    resizeClient(gui.hwnd(), 720, 540); gui.pump();
    captureWizard(gui.hwnd(), L"toggle-identity-small.bmp");
    gui.nextTo(contractsId); exercise(contractsId, L"toggle-wizard-contracts.bmp");
    gui.wizard.endSession();

    ContractsWindow contracts(gui.pipe);
    check(contracts.open(GetModuleHandleW(nullptr), nullptr, devDocument(), "synthetic", "toggle"), "Contracts open failed");
    const auto root = contracts.window(), first = child(root, 1000), second = child(root, 1004), last = child(root, 2004);
    check(first && second && last, "maximum 252 native rows missing");
    check(text(first).empty(), "Contracts repeats visual label");
    const bool a = checked(first), b = checked(second);
    space(first); check(checked(first) != a && checked(second) == b, "first row changed another row");
    space(second); check(checked(first) != a && checked(second) != b, "second row changed another row");
    accessibility(first, L"10.99.99.2_10.99.99.3 Enabled", true);
    check(checked(first) == a && checked(second) != b, "accessible action changed another row");
    EnableWindow(second, FALSE); accessibility(second, L"10.99.99.2_10.99.99.4 Enabled", false);
    captureWizard(root, L"toggle-contracts.bmp"); EnableWindow(second, TRUE);
    SetFocus(last); gui.pump();
    RECT lastRect{}, viewportRect{}; GetWindowRect(last, &lastRect);
    const auto viewport = GetParent(GetParent(last)); GetWindowRect(viewport, &viewportRect);
    check(lastRect.top >= viewportRect.top && lastRect.bottom <= viewportRect.bottom, "last switch not revealed by scroll");
    resizeClient(root, 900, 600); SetFocus(first); gui.pump();
    check(checked(first) == a && checked(second) != b, "resize/scroll changed values");
    MSG tab{}; tab.hwnd = first; tab.message = WM_KEYDOWN; tab.wParam = VK_TAB;
    check(contracts.translate(tab) && GetFocus() == child(root, 1001), "Contracts Tab order changed");
    captureWizard(root, L"toggle-contracts-small.bmp");
    contracts.endSession();
    check(gui.backend.count(&Backend::finishCalls) == 0, "switch check published configuration");
    std::cout << "[ EVIDENCE ] Four Wizard options, mouse label, Space, Tab, native MSAA name/role/checked/unavailable/action, 252 Contracts rows, independent values, resize and scroll.\n";
}
} // namespace

int main(int argc, char** argv) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_STANDARD_CLASSES | ICC_WIN95_CLASSES};
    InitCommonControlsEx(&controls);
    uiDiagnostics.begin();
    UiDiagnostics::Scope diagnosticScope;
    if (argc > 1 && std::string_view(argv[1]) == "--dev-preview") return devPreview(argc, argv);
    if (argc == 2 && std::string_view(argv[1]) == "--interactive") return interactive();
    if (argc == 3 && std::string_view(argv[1]) == "--preview") {
        const std::string_view scene = argv[2];
        if (scene == "secret" || scene == "identity" || scene == "identity-small" || scene == "contracts") return interactive(scene);
    }
    if (argc > 2 || (argc == 2 && (std::string_view(argv[1]).starts_with("--") ||
        std::string_view(argv[1]).find_first_not_of(" \t\r\n") == std::string_view::npos))) {
        std::cerr << "Invalid UI harness arguments\n"; return 1;
    }
    const std::string filter = argc > 1 ? argv[1] : "";
    const std::pair<const char*, void(*)()> tests[]{
        {"modern_toggle_switches", modernToggleSwitches},
        {"wp06_scene_matrix", devSceneMatrix},
        {"wp06_preview_over_45_seconds", devLongPreview},
        {"wp06_connected_countdown", devConnectedCountdown},
        {"wp06_unknown_check_confirmed_repeat", devSaveUnknownRepeat},
        {"wp06_password_children_parent_ok", devPasswordChildren},
        {"wp06_saving_original_deadline", devSavingDeadline},
        {"wp06_private_endpoint_cleanup", devEndpointIsolation},
        {"wp06_tray_actions_exit", devTrayActions},
        {"wp06_wizard_memory_publication", devWizardPublication},
        {"happy_path_resize_keyboard_finish", happyPath},
        {"wp04_gate_consent_idle", wizardGateConsentAndIdle},
        {"wp04_resources", wizardResources},
        {"wp04_stable_fields_warning", wizardStableFieldsAndWarning},
        {"wp04_production_finish", [] { wizardProductionFinish(false); }},
        {"wp04_production_lost_finish", [] { wizardProductionFinish(true); }},
        {"wp04_lifecycle_snapshot_discard", wizardLifecycleSnapshotAndDiscard},
        {"wp04_lifecycle_commit_deadline", wizardLifecycleCommitDeadline},
        {"wp04_lifecycle_end_session_cycles", wizardLifecycleEndSessionCycles},
        {"wp04_lifecycle_held_heartbeat_gate", wizardLifecycleHeldHeartbeatAndGate},
        {"wp04_lifecycle_application_wrappers", wizardLifecycleApplicationWrappers},
        {"wp04_lifecycle_end_inside_success_notice", wizardLifecycleEndInsideSuccessNotification},
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
