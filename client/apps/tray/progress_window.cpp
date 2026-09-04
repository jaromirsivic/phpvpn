#include "progress_window.h"
#include "resource.h"
#include "window_utils.h"

namespace phpvpn::tray {
namespace {
constexpr wchar_t kClass[] = L"PhpVpnConnectingProgress";
constexpr UINT kSnapshotMessage = WM_APP + 21;
constexpr UINT_PTR kTimer = 2;
COLORREF checkpointColor(CheckpointResult result) {
    if (result == CheckpointResult::success) return RGB(0, 125, 50);
    if (result == CheckpointResult::failed) return RGB(190, 25, 25);
    return RGB(184, 94, 0);
}
}  // namespace
ProgressWindow::~ProgressWindow() {
    if (window_) DestroyWindow(window_);
}
void ProgressWindow::open(HINSTANCE instance) {
    // A second local Connect starts a fresh presentation in the same window.
    reader_.stop();
    model_ = ProgressModel{};
    instance_ = instance;
    if (!window_) {
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
        window_ = CreateWindowExW(WS_EX_APPWINDOW, kClass, L"PHPVPN connection",
            WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
            work.left, work.top, 1, 1, nullptr, nullptr, instance, this);
        if (!window_) return;
    }
    sizeAndCenter(window_, {600, 355});
    polling_.start(nowMs());
    SetTimer(window_, kTimer, kProgressIntervalMs, nullptr);
    InvalidateRect(window_, nullptr, TRUE);
    ShowWindow(window_, SW_SHOWNORMAL);
    SetForegroundWindow(window_);
    // The post-connect status will seed the model. The first independent read
    // is due in 500 ms, preventing a stale pre-command disconnected snapshot.
}
void ProgressWindow::onSnapshot(const TrayStatus& snapshot) {
    if (!window_) return;
    model_.onSnapshot(snapshot, nowMs());
    if (model_.view().closeRequested) DestroyWindow(window_);
    else InvalidateRect(window_, nullptr, TRUE);
}
LRESULT CALLBACK ProgressWindow::procedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* self = reinterpret_cast<ProgressWindow*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        self = static_cast<ProgressWindow*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
        self->window_ = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    if (!self) return DefWindowProcW(window, message, wParam, lParam);
    switch (message) {
        case WM_TIMER:
            if (wParam == kTimer) {
                self->model_.onTick(nowMs());
                if (self->model_.view().closeRequested) DestroyWindow(window);
                else if (self->polling_.onTimer(nowMs())) self->reader_.request(window, kSnapshotMessage);
            }
            return 0;
        case kSnapshotMessage:
            if (auto snapshot = self->reader_.take()) self->onSnapshot(*snapshot);
            return 0;
        case WM_PAINT: self->paint(); return 0;
        case WM_DPICHANGED:
            applyDpiRect(window, lParam);
            InvalidateRect(window, nullptr, TRUE);
            return 0;
        case WM_CLOSE:
            self->model_.onClose();
            DestroyWindow(window);
            return 0;
        case WM_DESTROY:
            KillTimer(window, kTimer);
            self->polling_.stop();
            self->reader_.stop();
            self->window_ = nullptr;
            return 0;
        default: return DefWindowProcW(window, message, wParam, lParam);
    }
}
void ProgressWindow::paint() {
    PAINTSTRUCT paint{};
    HDC dc = BeginPaint(window_, &paint);
    RECT client{};
    GetClientRect(window_, &client);
    const int dpi = static_cast<int>(GetDpiForWindow(window_));
    const auto px = [dpi](int value) { return scaleForDpi(value, dpi); };
    const auto& view = model_.view();
    const int iconId = view.shield == Shield::green ? IDI_PHPVPN_CONNECTED :
                       view.shield == Shield::orange ? IDI_PHPVPN_CONNECTING : IDI_PHPVPN_DISCONNECTED;
    HICON icon = static_cast<HICON>(LoadImageW(instance_, MAKEINTRESOURCEW(iconId), IMAGE_ICON, px(48), px(48), 0));
    DrawIconEx(dc, px(24), px(22), icon, px(48), px(48), 0, nullptr, DI_NORMAL);
    if (icon) DestroyIcon(icon);
    HFONT title = makeFont(dpi, view.stage == ProgressStage::succeeded ? 26 : 23, true);
    HFONT normal = makeFont(dpi, 17);
    HFONT failure = makeFont(dpi, 23, true);
    HGDIOBJ oldFont = SelectObject(dc, title);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, view.stage == ProgressStage::succeeded ? RGB(0, 125, 50) : RGB(30, 30, 30));
    drawText(dc, view.stage == ProgressStage::succeeded ? L"Successfully connected" :
                 view.stage == ProgressStage::failed ? L"Connection failed" : L"Connecting",
                 {px(90), px(24), client.right - px(20), px(80)}, DT_LEFT | DT_WORDBREAK);
    SelectObject(dc, normal);
    const CheckpointResult checks[]{view.serverUrl, view.serverSecret, view.accessKey};
    const wchar_t* labels[]{L"Server URL", L"SERVER_SECRET", L"ACCESS_KEY"};
    for (int index = 0; index < 3; ++index) {
        const int y = px(99 + 39 * index);
        const COLORREF color = checkpointColor(checks[index]);
        HBRUSH brush = CreateSolidBrush(color);
        const HGDIOBJ oldBrush = SelectObject(dc, brush);
        const HGDIOBJ oldPen = SelectObject(dc, GetStockObject(NULL_PEN));
        Ellipse(dc, px(29), y + px(4), px(43), y + px(18));
        SelectObject(dc, oldPen);
        SelectObject(dc, oldBrush);
        DeleteObject(brush);
        SetTextColor(dc, color);
        const wchar_t* outcome = checks[index] == CheckpointResult::success ? L"  - success" :
                                 checks[index] == CheckpointResult::failed ? L"  - failed" : L"  - pending";
        drawText(dc, std::wstring(labels[index]) + outcome,
                 {px(60), y, client.right - px(20), y + px(32)}, DT_LEFT | DT_SINGLELINE);
    }
    if (view.stage == ProgressStage::failed) {
        SetTextColor(dc, RGB(190, 25, 25));
        SelectObject(dc, failure);
        drawText(dc, L"Cannot connect to the server", {px(24), px(224), client.right - px(24), px(260)}, DT_WORDBREAK);
        SelectObject(dc, normal);
        drawText(dc, widen(view.errorDescription), {px(24), px(266), client.right - px(24), client.bottom - px(12)}, DT_WORDBREAK);
    }
    SelectObject(dc, oldFont);
    DeleteObject(title);
    DeleteObject(normal);
    DeleteObject(failure);
    EndPaint(window_, &paint);
}
}  // namespace phpvpn::tray
