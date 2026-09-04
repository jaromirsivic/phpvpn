#include "editor_window.h"
#include "core/ipc/ipc_protocol.h"
#include "resource.h"
#include "window_utils.h"
#include <algorithm>

namespace phpvpn::tray {
namespace { constexpr wchar_t kClass[] = L"PhpVpnTrayEditor"; }
EditorWindow::~EditorWindow() { if (window_) DestroyWindow(window_); }
void EditorWindow::setSaving(std::uint64_t generation, bool saving) {
    if (!window_ || generation != generation_) return;
    EnableWindow(field_, !saving);
    EnableWindow(GetDlgItem(window_, IDC_SAVE), !saving);
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
    save_ = std::move(save);
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
    std::string crlf;
    crlf.reserve(content.size());
    for (const char character : content) {
        if (character == '\r') continue;
        if (character == '\n') crlf.push_back('\r');
        crlf.push_back(character);
    }
    SetWindowTextW(field_, widen(crlf).c_str());
    if (save_) {
        CreateWindowExW(0, L"BUTTON", L"Save", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
            0, 0, 1, 1, window_, reinterpret_cast<HMENU>(IDC_SAVE), instance, nullptr);
        CreateWindowExW(0, L"BUTTON", L"Cancel", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            0, 0, 1, 1, window_, reinterpret_cast<HMENU>(IDC_CANCEL), instance, nullptr);
    }
    updateFonts();
    layout();
    ShowWindow(window_, SW_SHOW);
    SetForegroundWindow(window_);
    SetFocus(field_);
}
void EditorWindow::updateFonts() {
    const int dpi = static_cast<int>(GetDpiForWindow(window_));
    HFONT text = makeFont(dpi, 14, false, true);
    HFONT button = makeFont(dpi, 15);
    SendMessageW(field_, WM_SETFONT, reinterpret_cast<WPARAM>(text), TRUE);
    for (const int id : {IDC_SAVE, IDC_CANCEL}) {
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
    HDWP batch = BeginDeferWindowPos(save_ ? 3 : 1);
    const auto move = [&](HWND child, Rect rect) {
        if (!child) return;
        if (batch) batch = DeferWindowPos(batch, child, nullptr, rect.left, rect.top,
            rect.right - rect.left, rect.bottom - rect.top, SWP_NOZORDER | SWP_NOACTIVATE);
    };
    move(field_, next.textArea);
    move(GetDlgItem(window_, IDC_SAVE), next.saveButton);
    move(GetDlgItem(window_, IDC_CANCEL), next.cancelButton);
    const bool moved = batch && EndDeferWindowPos(batch);
    if (!moved) {
        const auto fallback = [](HWND child, Rect rect) {
            if (child) SetWindowPos(child, nullptr, rect.left, rect.top, rect.right - rect.left,
                                   rect.bottom - rect.top, SWP_NOZORDER | SWP_NOACTIVATE);
        };
        fallback(field_, next.textArea);
        fallback(GetDlgItem(window_, IDC_SAVE), next.saveButton);
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
            if (LOWORD(wParam) == IDC_CANCEL || LOWORD(wParam) == IDCANCEL) {
                DestroyWindow(window);
                return 0;
            }
            if ((LOWORD(wParam) == IDC_SAVE || LOWORD(wParam) == IDOK) && self->save_) {
                const int length = GetWindowTextLengthW(self->field_);
                std::wstring text(static_cast<std::size_t>(length) + 1, L'\0');
                GetWindowTextW(self->field_, text.data(), length + 1);
                text.resize(static_cast<std::size_t>(length));
                auto document = narrow(text);
                document.erase(std::remove(document.begin(), document.end(), '\r'), document.end());
                if (self->save_(document)) {
                    MessageBoxW(window, L"The configuration was stored. It takes effect on the next connection attempt.",
                                L"Edit Config", MB_OK | MB_ICONINFORMATION);
                    DestroyWindow(window);
                }
                return 0;
            }
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
