#include "../tray/window_utils.h"
#include <initguid.h>
#include "package_window.h"
#include <limits>

namespace phpvpn::package {
namespace {
constexpr auto className = L"PhpVpnPackageOperation";
constexpr UINT startMessage = WM_APP + 41;
constexpr UINT_PTR pollTimer = 1;
constexpr std::size_t batchLimit = 65536;
}
PackageWindow::~PackageWindow() {
    stop();
    if (window_) DestroyWindow(window_);
    if (font_) DeleteObject(font_);
    if (largeIcon_) DestroyIcon(largeIcon_);
    if (smallIcon_) DestroyIcon(smallIcon_);
    if (accessibility_) accessibility_->Release();
    if (comInitialized_) CoUninitialize();
}
void PackageWindow::stop() {
    shared_->cancel = true;
    if (worker_.joinable()) worker_.join();
}
bool PackageWindow::open(HINSTANCE instance, const std::filesystem::path& directory) {
    if (window_ || started_) return false;
    instance_ = instance;
    directory_ = directory;
    comInitialized_ = SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED));
    if (FAILED(CoCreateInstance(CLSID_AccPropServices, nullptr, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&accessibility_)))) return false;
    WNDCLASSEXW type{sizeof(type)};
    type.hInstance = instance;
    type.lpfnWndProc = procedure;
    type.lpszClassName = className;
    type.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    if (!RegisterClassExW(&type) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;
    // Capture the cursor before creating the window, which could change the active monitor.
    POINT cursor{};
    tray::WindowPlacementContext placement;
    if (GetCursorPos(&cursor)) placement.menuPoint = tray::Point{cursor.x, cursor.y};
    else placement.parent = GetForegroundWindow();
    window_ = CreateWindowExW(0, className, title(mode_, state_), WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 760, 520, nullptr, nullptr, instance, this);
    if (!window_) return false;
    // Keep PowerShell's physical lines intact and scroll long lines horizontally.
    log_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP |
        WS_VSCROLL | WS_HSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | ES_AUTOHSCROLL,
        0, 0, 1, 1, window_, reinterpret_cast<HMENU>(1001), instance, nullptr);
    if (!log_) { DestroyWindow(window_); return false; }
    if (FAILED(accessibility_->SetHwndPropStr(log_, static_cast<DWORD>(OBJID_CLIENT), CHILDID_SELF,
        PROPID_ACC_NAME, L"Operation output"))) { DestroyWindow(window_); return false; }
    SendMessageW(log_, EM_SETLIMITTEXT, 0x7ffffffe, 0);
    updateDpi();
    tray::sizeAndCenter(window_, {760, 520}, placement);
    layout();
    ShowWindow(window_, SW_SHOW);
    UpdateWindow(window_);
    SetFocus(log_);
    if (!SetTimer(window_, pollTimer, 30, nullptr) || !PostMessageW(window_, startMessage, 0, 0)) {
        state_ = State::failed;
        SetWindowTextW(window_, title(mode_, state_));
        append(L"The operation could not be scheduled. Close this window and try again.\r\n");
        InvalidateRect(window_, nullptr, TRUE);
    }
    return true;
}
void PackageWindow::start() {
    if (started_ || state_ != State::running) return;
    started_ = true;
    try {
        const auto shared = shared_;
        const auto directory = directory_;
        const auto mode = mode_;
        worker_ = std::thread([shared, directory, mode] {
            const auto result = runScript(mode, directory, [shared](std::wstring text) {
                std::lock_guard lock(shared->mutex);
                shared->pending += text;
            }, shared->cancel);
            std::lock_guard lock(shared->mutex);
            shared->result = result;
            shared->done = true;
        });
    } catch (...) {
        std::lock_guard lock(shared_->mutex);
        shared_->result.error = L"The background worker could not be started.";
        shared_->done = true;
    }
}
void PackageWindow::append(const std::wstring& text) {
    if (text.empty() || !log_) return;
    std::wstring display;
    display.reserve(text.size());
    // Write-Host emits LF, while EDIT needs CRLF. Hold a trailing CR so a
    // CRLF pair split between polls is inserted as one line break.
    for (const auto character : text) {
        if (pendingCr_) {
            display.push_back(L'\r');
            pendingCr_ = false;
            if (character == L'\n') { display.push_back(L'\n'); continue; }
        }
        if (character == L'\r') pendingCr_ = true;
        else if (character == L'\n') display += L"\r\n";
        else display.push_back(character);
    }
    if (display.empty()) return;
    // Detect failed insertions via notifications and never silently claim
    // success after a display error. The explicit limit exceeds EDIT's default.
    SendMessageW(log_, EM_SETSEL, static_cast<WPARAM>(-1), static_cast<LPARAM>(-1));
    SendMessageW(log_, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(display.c_str()));
    SendMessageW(log_, EM_SCROLLCARET, 0, 0);
}
void PackageWindow::poll() {
    std::wstring batch;
    Result result;
    bool done = false;
    {
        std::lock_guard lock(shared_->mutex);
        auto count = (std::min)(batchLimit, shared_->pending.size());
        // Do not split a UTF-16 surrogate pair between UI batches.
        if (count < shared_->pending.size() && count && shared_->pending[count - 1] >= 0xd800 && shared_->pending[count - 1] <= 0xdbff) --count;
        batch = shared_->pending.substr(0, count);
        shared_->pending.erase(0, count);
        done = shared_->done && shared_->pending.empty();
        if (done) result = shared_->result;
    }
    append(batch);
    if (!done || state_ != State::running) return;
    if (worker_.joinable()) worker_.join();
    if (captureFailed_) append(L"\r\nLauncher error: the text control could not display the entire output. The operation cannot be reported as successful.\r\n");
    if (!result.error.empty()) append(L"\r\nLauncher error: " + result.error + L"\r\n");
    else append(L"\r\nPowerShell exit code: " + std::to_wstring(result.exitCode) + L"\r\n");
    state_ = result.succeeded() && !captureFailed_ ? State::succeeded : State::failed;
    SetWindowTextW(window_, title(mode_, state_));
    KillTimer(window_, pollTimer);
    InvalidateRect(window_, nullptr, TRUE);
}
void PackageWindow::layout() {
    if (!log_ || IsIconic(window_)) return;
    RECT client{};
    GetClientRect(window_, &client);
    const int margin = MulDiv(20, static_cast<int>(GetDpiForWindow(window_)), 96);
    MoveWindow(log_, margin, margin, (std::max)(1L, client.right - 2 * margin),
        (std::max)(1L, client.bottom - 2 * margin), TRUE);
}
void PackageWindow::updateDpi() {
    const auto dpi = GetDpiForWindow(window_);
    auto font = tray::makeFont(static_cast<int>(dpi), 14, false, true);
    if (font) {
        SendMessageW(log_, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        if (font_) DeleteObject(font_);
        font_ = font;
    }
    auto load = [&](int metric) {
        const auto size = GetSystemMetricsForDpi(metric, dpi);
        return static_cast<HICON>(LoadImageW(instance_, MAKEINTRESOURCEW(100), IMAGE_ICON, size, size, 0));
    };
    auto large = load(SM_CXICON), smallImage = load(SM_CXSMICON);
    if (large) { SendMessageW(window_, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(large)); if (largeIcon_) DestroyIcon(largeIcon_); largeIcon_ = large; }
    if (smallImage) { SendMessageW(window_, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(smallImage)); if (smallIcon_) DestroyIcon(smallIcon_); smallIcon_ = smallImage; }
}
LRESULT CALLBACK PackageWindow::procedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* self = reinterpret_cast<PackageWindow*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        self = static_cast<PackageWindow*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
        self->window_ = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    if (!self) return DefWindowProcW(window, message, wParam, lParam);
    const auto result = self->message(message, wParam, lParam);
    if (message == WM_NCDESTROY) {
        SetWindowLongPtrW(window, GWLP_USERDATA, 0);
        self->window_ = self->log_ = nullptr;
    }
    return result;
}
LRESULT PackageWindow::message(UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case startMessage: start(); return 0;
    case WM_TIMER: if (wParam == pollTimer) poll(); return 0;
    case WM_SIZE: layout(); return 0;
    case WM_GETMINMAXINFO:
        tray::setMinimumClientTrackSize(window_, *reinterpret_cast<MINMAXINFO*>(lParam), {520, 360}); return 0;
    case WM_DPICHANGED: tray::applyDpiRect(window_, lParam); updateDpi(); layout(); return 0;
    case WM_SETFOCUS: if (log_) SetFocus(log_); return 0;
    case WM_COMMAND:
        if (HIWORD(wParam) == EN_ERRSPACE || HIWORD(wParam) == EN_MAXTEXT) {
            captureFailed_ = true;
            SetWindowTextW(window_, L"Output display failed. Wait for the operation to finish.");
        }
        break;
    case WM_CLOSE:
        if (state_ == State::running) {
            if (!closeNotice_) { append(L"\r\nPlease wait for the operation to finish before closing this window.\r\n"); closeNotice_ = true; }
            return 0;
        }
        DestroyWindow(window_); return 0;
    case WM_QUERYENDSESSION: return TRUE;
    case WM_ENDSESSION: if (wParam) { stop(); DestroyWindow(window_); } return 0;
    case WM_DESTROY:
        if (accessibility_ && log_) { const MSAAPROPID property = PROPID_ACC_NAME; accessibility_->ClearHwndProps(log_, static_cast<DWORD>(OBJID_CLIENT), CHILDID_SELF, &property, 1); }
        KillTimer(window_, pollTimer); stop(); return 0;
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        auto dc = BeginPaint(window_, &paint);
        const auto brush = CreateSolidBrush(color(state_));
        FillRect(dc, &paint.rcPaint, brush);
        DeleteObject(brush);
        EndPaint(window_, &paint);
        return 0;
    }
    }
    return DefWindowProcW(window_, message, wParam, lParam);
}
} // namespace phpvpn::package
