#include "http_headers_window.h"
#include "window_utils.h"
#include "ui_controls.h"
#include "system_dialog.h"
#include "core/config/config.h"
#include "core/protocol/http_headers.h"
#include <commctrl.h>
#include <richedit.h>
#include <limits>
#include <utility>
#include <array>

namespace phpvpn::tray {
namespace {
constexpr wchar_t className[] = L"PhpVpnHttpHeaders";
constexpr int textId = 790, resetId = 791;
Result<std::string> readText(HWND field) {
    GETTEXTLENGTHEX measure{GTL_USECRLF | GTL_PRECISE | GTL_NUMCHARS, 1200};
    const auto measured = SendMessageW(field, EM_GETTEXTLENGTHEX, reinterpret_cast<WPARAM>(&measure), 0);
    if (measured < 0 || measured >= (std::numeric_limits<int>::max)() / 2) return Error(ErrorCode::config_too_large);
    const int length = static_cast<int>(measured);
    std::wstring text(static_cast<std::size_t>(length) + 1, L'\0');
    GETTEXTEX get{static_cast<DWORD>(text.size() * sizeof(wchar_t)), GT_USECRLF, 1200, nullptr, nullptr};
    const int copied = static_cast<int>(SendMessageW(field, EM_GETTEXTEX, reinterpret_cast<WPARAM>(&get), reinterpret_cast<LPARAM>(text.data())));
    if (copied != length) return Error(ErrorCode::config_encoding_error);
    if (!length) return std::string{};
    const int bytes = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), copied, nullptr, 0, nullptr, nullptr);
    if (!bytes) return Error(ErrorCode::config_encoding_error);
    std::string out(static_cast<std::size_t>(bytes), '\0');
    if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), copied, out.data(), bytes, nullptr, nullptr))
        return Error(ErrorCode::config_encoding_error);
    return out;
}
}
bool HttpHeadersWindow::open(HINSTANCE instance, HWND owner, const std::string& value,
    std::function<Status(std::string)> accept, std::function<void()> closed) {
    if (window_) { reactivateWindow(window_); return true; }
    if (!IsWindow(owner)) return false;
    // The classic EDIT hard-wraps very long physical lines internally. Plain
    // Rich Edit keeps even long Cookie values on one horizontally scrollable line.
    static const HMODULE richEdit = LoadLibraryExW(L"Msftedit.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!richEdit) return false;
    // Normalize only the display copy. Cancel preserves the original raw fields.
    auto display = normalizeHttpHeaders(value);
    if (!display) return false;
    instance_ = instance; owner_ = owner; accept_ = std::move(accept); closed_ = std::move(closed);
    WNDCLASSEXW type{sizeof(type)};
    type.lpfnWndProc = procedure; type.hInstance = instance; type.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    type.lpszClassName = className; RegisterClassExW(&type);
    window_ = CreateWindowExW(WS_EX_COMPOSITED | WS_EX_CONTROLPARENT, className, L"HTTP headers",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT, 760, 420, owner, nullptr, instance, this);
    if (!window_) { accept_ = {}; closed_ = {}; owner_ = nullptr; return false; }
    resources_.update(instance_, static_cast<int>(GetDpiForWindow(window_)));
    field_ = CreateWindowExW(0, MSFTEDIT_CLASS, nullptr, WS_CHILD | WS_TABSTOP | WS_VSCROLL | WS_HSCROLL |
        ES_MULTILINE | ES_WANTRETURN | ES_AUTOHSCROLL | ES_AUTOVSCROLL, 0, 0, 1, 1, window_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(textId)), instance, nullptr);
    // The shared textbox subclass intentionally accepts single-line EDIT only.
    // Multiline editors use the same parent-painted surface as Edit Config.
    if (!field_) { close(false); return false; }
    SendMessageW(field_, EM_SETTEXTMODE, TM_PLAINTEXT, 0);
    SendMessageW(field_, EM_EXLIMITTEXT, 0, (std::numeric_limits<int>::max)() - 1);
    if (!SetWindowTextW(field_, widen(*display).c_str())) { close(false); return false; }
    ShowWindow(field_, SW_SHOWNA);
    for (const auto& [id, title] : {std::pair{resetId, L"Reset"}, {IDCANCEL, L"Cancel"}, {IDOK, L"OK"}}) {
        HWND button = CreateWindowExW(0, L"BUTTON", title, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
            0, 0, 1, 1, window_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance, nullptr);
        if (!button || !attachModernButton(button, resources_, id == resetId ? ButtonRole::reset : id == IDOK ? ButtonRole::primary : ButtonRole::neutral)) {
            close(false); return false;
        }
    }
    updateFonts(); sizeAndCenter(window_, {760, 420}, {owner}); layout();
    restoreOwner_ = IsWindowEnabled(owner) != FALSE;
    EnableWindow(owner, FALSE);
    ShowWindow(window_, SW_SHOW); SetForegroundWindow(window_); SetFocus(field_);
    return true;
}
void HttpHeadersWindow::close(bool notify) {
    if (!window_) return;
    const auto owner = owner_;
    const bool restore = std::exchange(restoreOwner_, false);
    auto closed = notify ? std::move(closed_) : std::function<void()>{};
    closed_ = {}; accept_ = {};
    SetWindowTextW(field_, L""); SendMessageW(field_, EM_EMPTYUNDOBUFFER, 0, 0);
    // Re-enable before destroying the active owned window, as with native dialogs.
    if (restore && IsWindow(owner)) EnableWindow(owner, TRUE);
    DestroyWindow(window_);
    if (notify && IsWindow(owner)) {
        SetActiveWindow(owner);
        if (closed) closed();
    }
}
void HttpHeadersWindow::command(int id) {
    if (id == resetId) { SetWindowTextW(field_, widen(kDefaultHttpHeaders).c_str()); SetFocus(field_); return; }
    if (id == IDCANCEL) { close(); return; }
    if (id != IDOK || !accept_) return;
    auto text = readText(field_);
    Status result = text ? accept_(std::move(text).value()) : Status(text.error());
    if (result) { close(); return; }
    const auto message = result.code() == ErrorCode::config_too_large ? "The HTTP headers cannot fit in the configuration file (512 KiB limit)." :
        result.code() == ErrorCode::config_encoding_error ? "The text contains an invalid Unicode character." : httpHeadersErrorDescription(result.error());
    showSystemMessage(window_, widen(message).c_str(), L"HTTP headers", SystemDialogIcon::error);
    if (field_) SetFocus(field_);
}
void HttpHeadersWindow::updateFonts(int dpi) {
    if (!window_) return;
    resources_.update(instance_, dpi ? dpi : static_cast<int>(GetDpiForWindow(window_)));
    SendMessageW(field_, EM_SETBKGNDCOLOR, 0, resources_.color(ui::Color::surface));
    SendMessageW(field_, WM_SETFONT, reinterpret_cast<WPARAM>(resources_.font(ui::Font::mono)), TRUE);
    for (int id : {resetId, IDCANCEL, IDOK}) SendMessageW(GetDlgItem(window_, id), WM_SETFONT,
        reinterpret_cast<WPARAM>(resources_.font(ui::Font::label)), TRUE);
    resources_.applyApplicationIcons(window_);
}
void HttpHeadersWindow::layout() {
    if (!field_) return;
    LayoutFrame frame(window_);
    RECT client{}; GetClientRect(window_, &client);
    geometry_ = computeHttpHeadersLayout({client.right, client.bottom}, resources_.dpi());
    const int inset = scaleForDpi(6, resources_.dpi());
    const Rect textRect{geometry_.text.left + inset, geometry_.text.top + inset,
        geometry_.text.right - inset, geometry_.text.bottom - inset};
    const std::array controls{ControlPlacement{field_, textRect}, ControlPlacement{GetDlgItem(window_, resetId), geometry_.reset},
        ControlPlacement{GetDlgItem(window_, IDCANCEL), geometry_.cancel}, ControlPlacement{GetDlgItem(window_, IDOK), geometry_.ok}};
    positionControls(window_, controls);
    InvalidateRect(window_, nullptr, FALSE);
}
bool HttpHeadersWindow::translate(MSG& message) {
    if (!window_ || (message.hwnd != window_ && !IsChild(window_, message.hwnd))) return false;
    if (message.message == WM_KEYDOWN && message.wParam == VK_ESCAPE) { close(); return true; }
    if (message.message == WM_KEYDOWN && message.wParam == VK_TAB) {
        if (const auto next = GetNextDlgTabItem(window_, GetFocus(), (GetKeyState(VK_SHIFT) & 0x8000) != 0)) SetFocus(next);
        return true;
    }
    if (message.message == WM_KEYDOWN && message.wParam == VK_RETURN) {
        if (GetFocus() == field_) return false;
        const int id = GetDlgCtrlID(GetFocus()); command(id == resetId || id == IDCANCEL ? id : IDOK); return true;
    }
    return IsDialogMessageW(window_, &message) != FALSE;
}
LRESULT CALLBACK HttpHeadersWindow::procedure(HWND window, UINT message, WPARAM w, LPARAM l) {
    auto* self = reinterpret_cast<HttpHeadersWindow*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) { self = static_cast<HttpHeadersWindow*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams); self->window_ = window; SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self)); }
    if (!self) return DefWindowProcW(window, message, w, l);
    switch (message) {
        case WM_COMMAND:
            if (HIWORD(w) == BN_CLICKED) self->command(LOWORD(w));
            else if (HIWORD(w) == EN_SETFOCUS || HIWORD(w) == EN_KILLFOCUS) InvalidateRect(window, nullptr, FALSE);
            else if (HIWORD(w) == EN_MAXTEXT || HIWORD(w) == EN_ERRSPACE)
                showSystemMessage(window, L"The edit control could not accept all of the text. Shorten the headers and try again.", L"HTTP headers", SystemDialogIcon::error);
            return 0;
        case DM_GETDEFID: return MAKELRESULT(IDOK, DC_HASDEFID);
        case WM_SIZE: self->layout(); return 0;
        case WM_DPICHANGED: applyDpiRect(window, l); self->updateFonts(HIWORD(w)); self->layout(); return 0;
        case WM_SETTINGCHANGE: case WM_SYSCOLORCHANGE: case WM_THEMECHANGED: self->updateFonts(); self->layout(); return 0;
        case WM_GETMINMAXINFO: setMinimumClientTrackSize(window, *reinterpret_cast<MINMAXINFO*>(l), {480, 260}); return 0;
        case WM_ERASEBKGND: return 1;
        case WM_PAINT: {
            PAINTSTRUCT paint{}; auto dc = BeginPaint(window, &paint); PaintBuffer buffer(dc);
            FillRect(buffer.dc(), &paint.rcPaint, self->resources_.brush(ui::Color::background));
            const bool focused = GetFocus() == self->field_;
            paintRoundedShape(buffer.dc(), self->geometry_.text, scaleForDpi(12, self->resources_.dpi()),
                self->resources_, ui::Color::surface, focused ? ui::Color::focus : ui::Color::border, false,
                std::max(1, scaleForDpi(focused ? 2 : 1, self->resources_.dpi())));
            buffer.finish(); EndPaint(window, &paint); return 0;
        }
        case WM_CTLCOLOREDIT: {
            auto dc = reinterpret_cast<HDC>(w); SetTextColor(dc, self->resources_.color(ui::Color::text));
            SetBkColor(dc, self->resources_.color(ui::Color::surface));
            return reinterpret_cast<LRESULT>(self->resources_.brush(ui::Color::surface));
        }
        case WM_CLOSE: self->close(); return 0;
        case WM_DESTROY:
            self->window_ = self->field_ = self->owner_ = nullptr; self->accept_ = {}; self->closed_ = {}; return 0;
        case WM_NCDESTROY: self->resources_.clear(); SetWindowLongPtrW(window, GWLP_USERDATA, 0); break;
    }
    return DefWindowProcW(window, message, w, l);
}
}
