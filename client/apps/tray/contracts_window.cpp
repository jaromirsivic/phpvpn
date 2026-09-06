#include "contracts_window.h"
#include "window_utils.h"

namespace phpvpn::tray {
namespace { constexpr wchar_t kClass[] = L"PhpVpnCommunicationContracts"; }
ContractsWindow::~ContractsWindow() { close(); }
void ContractsWindow::close() { if (window_) DestroyWindow(window_); }
bool ContractsWindow::show(HINSTANCE instance) {
    if (!window_) {
        WNDCLASSEXW type{};
        type.cbSize = sizeof(type);
        type.lpfnWndProc = procedure;
        type.hInstance = instance;
        type.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        type.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        type.lpszClassName = kClass;
        if (!RegisterClassExW(&type) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;
        const auto work = activeWorkArea();
        window_ = CreateWindowExW(WS_EX_APPWINDOW, kClass, L"Communication Contracts",
            WS_OVERLAPPEDWINDOW, work.left, work.top, 1, 1, nullptr, nullptr, instance, this);
        if (!window_) return false;
        sizeAndCenter(window_, {850, 410});
    }
    ShowWindow(window_, SW_SHOWNORMAL);
    SetForegroundWindow(window_);
    return true;
}
LRESULT CALLBACK ContractsWindow::procedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* self = reinterpret_cast<ContractsWindow*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        self = static_cast<ContractsWindow*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
        self->window_ = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    if (!self) return DefWindowProcW(window, message, wParam, lParam);
    switch (message) {
    case WM_DPICHANGED: applyDpiRect(window, lParam); return 0;
    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE) { self->close(); return 0; }
        break;
    case WM_CLOSE: self->close(); return 0;
    case WM_DESTROY: self->window_ = nullptr; return 0;
    default: break;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}
} // namespace phpvpn::tray
