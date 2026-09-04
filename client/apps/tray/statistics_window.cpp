#include "statistics_window.h"
#include "window_utils.h"

namespace phpvpn::tray {
namespace { constexpr wchar_t kClass[] = L"PhpVpnStatistics"; }
StatisticsWindow::~StatisticsWindow() { if (window_) DestroyWindow(window_); }
void StatisticsWindow::open(HINSTANCE instance, const TrayStatus& snapshot) {
    applyStatusSnapshot(snapshot_, snapshot);
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
        window_ = CreateWindowExW(WS_EX_APPWINDOW, kClass, L"PHPVPN Statistics", WS_OVERLAPPEDWINDOW,
                                  work.left, work.top, 1, 1, nullptr, nullptr, instance, this);
        if (!window_) return;
        sizeAndCenter(window_, {850, 410});
    }
    InvalidateRect(window_, nullptr, TRUE);
    ShowWindow(window_, SW_SHOWNORMAL);
    SetForegroundWindow(window_);
}
void StatisticsWindow::onSnapshot(const TrayStatus& snapshot) {
    if (window_ && applyStatusSnapshot(snapshot_, snapshot)) InvalidateRect(window_, nullptr, TRUE);
}
LRESULT CALLBACK StatisticsWindow::procedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* self = reinterpret_cast<StatisticsWindow*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        self = static_cast<StatisticsWindow*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
        self->window_ = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    if (!self) return DefWindowProcW(window, message, wParam, lParam);
    switch (message) {
        case WM_PAINT: self->paint(); return 0;
        case WM_DPICHANGED: applyDpiRect(window, lParam); return 0;
        case WM_CLOSE: DestroyWindow(window); return 0;
        case WM_DESTROY: self->window_ = nullptr; return 0;
        default: return DefWindowProcW(window, message, wParam, lParam);
    }
}
void StatisticsWindow::paint() {
    PAINTSTRUCT paint{};
    HDC dc = BeginPaint(window_, &paint);
    RECT client{};
    GetClientRect(window_, &client);
    const int dpi = static_cast<int>(GetDpiForWindow(window_));
    const auto px = [dpi](int value) { return scaleForDpi(value, dpi); };
    HFONT normal = makeFont(dpi, 16);
    HFONT mono = makeFont(dpi, 16, false, true);
    const HGDIOBJ oldFont = SelectObject(dc, normal);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(30, 30, 30));
    drawText(dc, snapshot_.valid ? L"Global service counters" : L"Waiting for service status",
             {px(20), px(15), client.right - px(20), px(46)}, DT_LEFT);
    for (std::size_t index = 0; index < kCounterFields.size(); ++index) {
        const int top = px(58 + static_cast<int>(index) * 37);
        const auto& field = kCounterFields[index];
        SelectObject(dc, normal);
        drawText(dc, field.label, {px(20), top, client.right - px(230), top + px(34)}, DT_LEFT | DT_WORDBREAK);
        SelectObject(dc, mono);
        drawText(dc, snapshot_.valid ? std::to_wstring(snapshot_.counters.*(field.member)) : L"-",
                 {client.right - px(220), top, client.right - px(20), top + px(34)}, DT_RIGHT | DT_SINGLELINE);
    }
    SelectObject(dc, oldFont);
    DeleteObject(normal);
    DeleteObject(mono);
    EndPaint(window_, &paint);
}
}  // namespace phpvpn::tray
