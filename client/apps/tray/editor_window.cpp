#include "editor_window.h"
#include "core/ipc/ipc_protocol.h"
#include "core/ipc/wizard_ipc.h"
#include "core/crypto/crypto_provider.h"
#include "platform/windows/ipc_pipe.h"
#include "resource.h"
#include "window_utils.h"
#include <algorithm>
#include <atomic>

namespace phpvpn::tray {
namespace {
constexpr wchar_t kClass[] = L"PhpVpnTrayEditor";
constexpr int kReset = 304;
constexpr UINT_PTR kResetTimer = 1;
}
struct EditorWindow::ResetTask {
    std::uint64_t generation = 0;
    std::atomic<bool> done{false};
    std::string document;
    std::wstring error;
};
EditorWindow::~EditorWindow() {
    if (window_) DestroyWindow(window_);
    // No worker ever owns HWND or this. Its only I/O has a five-second deadline.
    if (resetThread_.joinable()) resetThread_.join();
}
void EditorWindow::setSaving(std::uint64_t generation, bool saving) {
    if (!window_ || generation != generation_) return;
    saving_ = saving;
    updateEnabled();
}
void EditorWindow::updateEnabled() {
    const bool resetting = resetTask_ && resetTask_->generation == generation_;
    EnableWindow(field_, !saving_ && !resetting);
    EnableWindow(GetDlgItem(window_, IDC_SAVE), !saving_ && !resetting);
    EnableWindow(GetDlgItem(window_, kReset), !saving_ && !resetTask_);
}
void EditorWindow::setContent(const std::string& content) {
    document_.load(content);
    const auto presentation = document_.presentation();
    std::string crlf;
    crlf.reserve(presentation.size());
    char previous = 0;
    for (const char character : presentation) {
        if (character == '\n' && previous != '\r') crlf.push_back('\r');
        crlf.push_back(character);
        previous = character;
    }
    SetWindowTextW(field_, widen(crlf).c_str());
    SendMessageW(field_, EM_SETMODIFY, FALSE, 0);
    SetWindowTextW(window_, (title_ + (document_.literal() ? L" - JSON string literal (raw NUL shown as \\u0000)" : L"")).c_str());
}
void EditorWindow::reset() {
    if (!save_ || saving_ || resetTask_) return;
    if (MessageBoxW(window_,
        L"Restore the default configuration in this editor? The file will change only after you choose Save.",
        L"Edit Config", MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES) return;
    auto task = std::make_shared<ResetTask>();
    task->generation = generation_;
    resetTask_ = task;
    updateEnabled();
    if (!SetTimer(window_, kResetTimer, 50, nullptr)) {
        resetTask_.reset();
        updateEnabled();
        MessageBoxW(window_, L"The default configuration could not be loaded. Your text has been preserved.",
                    L"Edit Config", MB_OK | MB_ICONERROR);
        return;
    }
    try {
        resetThread_ = std::thread([task, pipeName = pipeName_] {
            auto load = [&]() -> Status {
                auto crypto = createPlatformCryptoProvider();
                if (!crypto) return crypto.error();
                auto id = crypto.value()->randomUuidV4();
                if (!id) return id.error();
                WizardRequest request;
                request.command = WizardCommand::get_default_config;
                request.requestId = id.value();
                auto encoded = WizardIpc::buildRequest(request);
                if (!encoded) return encoded.error();
                auto bytes = sendIpcRequestTo(pipeName, encoded.value(), 5000);
                if (!bytes) return bytes.error();
                auto reply = WizardIpc::parseReply(request.command, request.requestId, bytes.value());
                if (!reply) return reply.error();
                if (reply->error) {
                    task->error = widen(reply->error->description + " " + reply->error->suggestedAction);
                    return Error(ErrorCode::response_invalid);
                }
                const auto* document = reply->result.find("document");
                // Closed reply validation checks the field type. Validate the
                // shared template too before replacing any user's draft.
                auto normalized = normalizeConfigText(document->stringValue());
                if (!normalized) return normalized.error();
                ConfigWarnings warnings;
                auto config = parseAndValidateConfig(normalized.value(), warnings);
                if (!config) return config.error();
                task->document = std::move(normalized.value());
                return Status::success();
            };
            try {
                const auto status = load();
                if (!status && task->error.empty()) task->error = widen(errorCodeDescription(status.error().code));
            } catch (...) {
                task->error = L"The default configuration could not be loaded.";
            }
            task->done.store(true, std::memory_order_release);
        });
    } catch (...) {
        KillTimer(window_, kResetTimer);
        resetTask_.reset();
        updateEnabled();
        MessageBoxW(window_, L"The default configuration could not be loaded. Your text has been preserved.",
                    L"Edit Config", MB_OK | MB_ICONERROR);
    }
}
void EditorWindow::pollReset() {
    if (!resetTask_ || !resetTask_->done.load(std::memory_order_acquire)) return;
    if (resetThread_.joinable()) resetThread_.join();
    auto task = std::move(resetTask_);
    KillTimer(window_, kResetTimer);
    updateEnabled();
    if (task->generation != generation_) return;
    if (!task->error.empty()) {
        const auto message = task->error + L"\n\nYour text has been preserved.";
        MessageBoxW(window_, message.c_str(), L"Edit Config", MB_OK | MB_ICONERROR);
        return;
    }
    setContent(task->document);
    SendMessageW(field_, EM_SETMODIFY, TRUE, 0);
    SetFocus(field_);
}
void EditorWindow::completeSave(std::uint64_t generation) {
    if (!window_ || generation != generation_) return;
    MessageBoxW(window_, L"The configuration was stored. It takes effect on the next connection attempt.",
                L"Edit Config", MB_OK | MB_ICONINFORMATION);
    DestroyWindow(window_);
}
void EditorWindow::open(HINSTANCE instance, const std::wstring& title, const std::string& content, Save save) {
    if (window_) DestroyWindow(window_);
    ++generation_;
    saving_ = false;
    save_ = std::move(save);
    title_ = title;
    oldClient_ = {};
    oldLayout_ = {};
    WNDCLASSEXW type{};
    type.cbSize = sizeof(type);
    type.style = CS_HREDRAW | CS_VREDRAW;
    type.lpfnWndProc = procedure;
    type.hInstance = instance;
    type.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    type.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    type.lpszClassName = kClass;
    RegisterClassExW(&type);
    const auto work = activeWorkArea();
    window_ = CreateWindowExW(WS_EX_APPWINDOW, kClass, title.c_str(),
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, work.left, work.top, 1, 1,
        nullptr, nullptr, instance, this);
    if (!window_) return;
    // client.md Edit Config: 1080 x 560 is the CLIENT size at 96 DPI.
    sizeAndCenter(window_, defaultEditorClientSize(96));
    DWORD style = WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | WS_HSCROLL |
                   ES_MULTILINE | ES_WANTRETURN | ES_AUTOVSCROLL | ES_AUTOHSCROLL;
    if (!save_) style |= ES_READONLY;
    field_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", nullptr, style,
        0, 0, 1, 1, window_, reinterpret_cast<HMENU>(IDC_TEXT_AREA), instance, nullptr);
    SendMessageW(field_, EM_SETLIMITTEXT, kIpcMaxMessageBytes, 0);
    setContent(content);
    if (save_) {
        CreateWindowExW(0, L"BUTTON", L"Save", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
            0, 0, 1, 1, window_, reinterpret_cast<HMENU>(IDC_SAVE), instance, nullptr);
        CreateWindowExW(0, L"BUTTON", L"Reset", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            0, 0, 1, 1, window_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kReset)), instance, nullptr);
        CreateWindowExW(0, L"BUTTON", L"Cancel", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            0, 0, 1, 1, window_, reinterpret_cast<HMENU>(IDC_CANCEL), instance, nullptr);
    }
    updateFonts();
    layout();
    if (resetTask_) {
        // A previous generation can still be finishing. Poll its bounded task,
        // but never allow it to replace this newly opened document.
        SetTimer(window_, kResetTimer, 50, nullptr);
        pollReset();
    }
    updateEnabled();
    ShowWindow(window_, SW_SHOW);
    SetForegroundWindow(window_);
    SetFocus(field_);
}
void EditorWindow::updateFonts() {
    const int dpi = static_cast<int>(GetDpiForWindow(window_));
    HFONT text = makeFont(dpi, 14, false, true);
    HFONT button = makeFont(dpi, 15);
    SendMessageW(field_, WM_SETFONT, reinterpret_cast<WPARAM>(text), TRUE);
    for (const int id : {IDC_SAVE, kReset, IDC_CANCEL}) {
        if (const HWND child = GetDlgItem(window_, id)) SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(button), TRUE);
    }
    if (textFont_) DeleteObject(textFont_);
    if (buttonFont_) DeleteObject(buttonFont_);
    textFont_ = text;
    buttonFont_ = button;
}
void EditorWindow::layout() {
    if (!field_) return;
    RECT client{};
    GetClientRect(window_, &client);
    const Size size{client.right, client.bottom};
    const auto next = computeEditorLayout(size, static_cast<int>(GetDpiForWindow(window_)), static_cast<bool>(save_));
    HDWP batch = BeginDeferWindowPos(save_ ? 4 : 1);
    const auto move = [&](HWND child, Rect rect) {
        if (!child) return;
        if (batch) batch = DeferWindowPos(batch, child, nullptr, rect.left, rect.top,
            rect.right - rect.left, rect.bottom - rect.top, SWP_NOZORDER | SWP_NOACTIVATE);
    };
    move(field_, next.textArea);
    move(GetDlgItem(window_, IDC_SAVE), next.saveButton);
    move(GetDlgItem(window_, kReset), next.resetButton);
    move(GetDlgItem(window_, IDC_CANCEL), next.cancelButton);
    const bool moved = batch && EndDeferWindowPos(batch);
    if (!moved) {
        const auto fallback = [](HWND child, Rect rect) {
            if (child) SetWindowPos(child, nullptr, rect.left, rect.top, rect.right - rect.left,
                                   rect.bottom - rect.top, SWP_NOZORDER | SWP_NOACTIVATE);
        };
        fallback(field_, next.textArea);
        fallback(GetDlgItem(window_, IDC_SAVE), next.saveButton);
        fallback(GetDlgItem(window_, kReset), next.resetButton);
        fallback(GetDlgItem(window_, IDC_CANCEL), next.cancelButton);
    }
    const auto exposed = exposedParentRegion(oldClient_, size, oldLayout_, next);
    const RECT region{exposed.left, exposed.top, exposed.right, exposed.bottom};
    InvalidateRect(window_, &region, TRUE);
    UpdateWindow(window_);
    oldClient_ = size;
    oldLayout_ = next;
}
LRESULT CALLBACK EditorWindow::procedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* self = reinterpret_cast<EditorWindow*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        self = static_cast<EditorWindow*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
        self->window_ = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    if (!self) return DefWindowProcW(window, message, wParam, lParam);
    switch (message) {
        case WM_COMMAND:
            if (LOWORD(wParam) == kReset) { self->reset(); return 0; }
            if (LOWORD(wParam) == IDC_CANCEL || LOWORD(wParam) == IDCANCEL) {
                DestroyWindow(window);
                return 0;
            }
            if ((LOWORD(wParam) == IDC_SAVE || LOWORD(wParam) == IDOK) && self->save_) {
                if (self->saving_ || (self->resetTask_ && self->resetTask_->generation == self->generation_)) return 0;
                const int length = GetWindowTextLengthW(self->field_);
                std::wstring text(static_cast<std::size_t>(length) + 1, L'\0');
                GetWindowTextW(self->field_, text.data(), length + 1);
                text.resize(static_cast<std::size_t>(length));
                const auto utf8 = narrow(text);
                if (!text.empty() && utf8.empty()) {
                    MessageBoxW(window, L"The edited text contains an invalid Unicode sequence. Correct it before saving.", L"Edit Config", MB_OK | MB_ICONERROR);
                    return 0;
                }
                auto document = self->document_.edited(utf8, SendMessageW(self->field_, EM_GETMODIFY, 0, 0) != 0);
                if (!document) {
                    MessageBoxW(window, L"Enter a complete JSON string literal, including its surrounding quotes. The file has not been changed.", L"Edit Config", MB_OK | MB_ICONERROR);
                    return 0;
                }
                if (self->save_(*document)) {
                    MessageBoxW(window, L"The configuration was stored. It takes effect on the next connection attempt.",
                                L"Edit Config", MB_OK | MB_ICONINFORMATION);
                    DestroyWindow(window);
                }
                return 0;
            }
            break;
        case WM_TIMER:
            if (wParam == kResetTimer) { self->pollReset(); return 0; }
            break;
        case WM_SIZE: self->layout(); return 0;
        case WM_DPICHANGED:
            applyDpiRect(window, lParam);
            self->updateFonts();
            self->layout();
            return 0;
        case WM_GETMINMAXINFO: {
            const int dpi = static_cast<int>(GetDpiForWindow(window));
            MONITORINFO monitor{};
            monitor.cbSize = sizeof(monitor);
            GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &monitor);
            RECT outer{0, 0, scaleForDpi(320, dpi), scaleForDpi(160, dpi)};
            AdjustWindowRectExForDpi(&outer, static_cast<DWORD>(GetWindowLongPtrW(window, GWL_STYLE)),
                                     FALSE, WS_EX_APPWINDOW, static_cast<UINT>(dpi));
            auto* info = reinterpret_cast<MINMAXINFO*>(lParam);
            info->ptMinTrackSize.x = std::min(outer.right - outer.left, monitor.rcWork.right - monitor.rcWork.left);
            info->ptMinTrackSize.y = std::min(outer.bottom - outer.top, monitor.rcWork.bottom - monitor.rcWork.top);
            return 0;
        }
        case WM_CLOSE: DestroyWindow(window); return 0;
        case WM_DESTROY:
            KillTimer(window, kResetTimer);
            ++self->generation_;
            self->window_ = nullptr;
            self->field_ = nullptr;
            if (self->textFont_) DeleteObject(self->textFont_);
            if (self->buttonFont_) DeleteObject(self->buttonFont_);
            self->textFont_ = self->buttonFont_ = nullptr;
            return 0;
        default: break;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}
}  // namespace phpvpn::tray
