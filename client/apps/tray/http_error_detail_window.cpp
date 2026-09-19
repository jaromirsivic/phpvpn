#include "http_error_detail_window.h"
#include "ui_controls.h"
#include "window_utils.h"
#include <commctrl.h>
#include <algorithm>

namespace phpvpn::tray {
namespace {
constexpr wchar_t className[] = L"PhpVpnHttpErrorDetail";
constexpr int tabsId = 780, requestId = 781, responseId = 782;
std::wstring description(const wchar_t* title, const BoundedHttpMessage& m, bool escaped) {
    auto out = std::wstring(title) + L" (Original size: " + widen(diagnosticSizeKiB(m.originalSizeBytes)) + L" KiB)";
    if (m.truncated()) out += L"\r\nTruncated: first 63 KiB and last 1 KiB, " + std::to_wstring(m.originalSizeBytes - 65536) +
        L" bytes omitted. The four dotted lines are a display separator.";
    if (escaped) out += L"\r\nInvalid or non-displayable bytes are shown as \\xHH.";
    return out;
}
}
bool HttpErrorDetailWindow::open(HINSTANCE instance, HWND owner, const HttpDiagnostic& d) {
    if (window_) { reactivateWindow(window_); return true; }
    if (!owner || !validHttpMessage(d.request) || !validHttpMessage(d.response)) return false;
    auto request = diagnosticDisplayText(d.request), response = diagnosticDisplayText(d.response);
    if (request.text.size() > kHttpDiagnosticDisplayLimit || response.text.size() > kHttpDiagnosticDisplayLimit) return false;
    instance_ = instance;
    WNDCLASSEXW type{sizeof(type)};
    type.lpfnWndProc = procedure; type.hInstance = instance; type.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    type.lpszClassName = className;
    RegisterClassExW(&type);
    window_ = CreateWindowExW(WS_EX_COMPOSITED | WS_EX_CONTROLPARENT, className, L"Error detail",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT, 1000, 680, owner, nullptr, instance, this);
    if (!window_) return false;
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_TAB_CLASSES}; InitCommonControlsEx(&controls);
    tabs_ = CreateWindowExW(0, WC_TABCONTROLW, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_CLIPSIBLINGS |
        TCS_FOCUSONBUTTONDOWN | TCS_OWNERDRAWFIXED, 0, 0, 1, 1, window_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(tabsId)), instance, nullptr);
    if (!tabs_) { close(); return false; }
    for (const auto* title : {L"Request", L"Response"}) {
        TCITEMW item{}; item.mask = TCIF_TEXT; item.pszText = const_cast<wchar_t*>(title);
        TabCtrl_InsertItem(tabs_, TabCtrl_GetItemCount(tabs_), &item);
    }
    notes_[0] = description(L"Request", d.request, request.escaped);
    notes_[0] += L"\r\nApplication reconstruction.";
    if (d.request.prefix.find(" HTTP/?\r\n") != std::string::npos) notes_[0] += L" HTTP version unavailable (HTTP/?).";
    if (d.requestHeadersSource == RequestHeadersSource::application) notes_[0] += L"\r\nSystem request headers unavailable. Showing application headers.";
    else if (d.requestHeadersSource == RequestHeadersSource::merged) notes_[0] += L"\r\nSystem headers merged with original application headers.";
    notes_[1] = description(L"Response", d.response, response.escaped);
    if (!d.responseComplete) notes_[1] += L"\r\nIncomplete response: size refers to received content.";
    if (d.responseHeadersSource != ResponseHeadersSource::winhttp) notes_[1] += L"\r\nComplete system response headers unavailable. Showing available information.";
    for (int i = 0; i < 2; ++i) {
        headings_[i] = CreateWindowExW(0, L"STATIC", notes_[i].c_str(), WS_CHILD | SS_NOPREFIX,
            0, 0, 1, 1, window_, nullptr, instance, nullptr);
        editors_[i] = CreateWindowExW(0, L"EDIT", nullptr, WS_CHILD | WS_VSCROLL | WS_HSCROLL |
            ES_MULTILINE | ES_READONLY | ES_AUTOHSCROLL | ES_AUTOVSCROLL,
            0, 0, 1, 1, window_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(i == 0 ? requestId : responseId)), instance, nullptr);
        if (!headings_[i] || !editors_[i]) { close(); return false; }
        SendMessageW(editors_[i], EM_SETLIMITTEXT, kHttpDiagnosticDisplayLimit + 1, 0);
        SetWindowTextW(editors_[i], (i == 0 ? request.text : response.text).c_str());
    }
    updateFonts(); sizeAndCenter(window_, {1000, 680}); selectTab(0);
    SetWindowPos(tabs_, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    ShowWindow(window_, SW_SHOW); SetForegroundWindow(window_); SetFocus(tabs_);
    return true;
}
void HttpErrorDetailWindow::close() {
    if (!window_) return;
    for (auto editor : editors_) if (editor) { SetWindowTextW(editor, L""); SendMessageW(editor, EM_EMPTYUNDOBUFFER, 0, 0); }
    DestroyWindow(window_);
}
void HttpErrorDetailWindow::selectTab(int index) {
    if (index < 0 || index > 1 || !tabs_) return;
    selected_ = index; TabCtrl_SetCurSel(tabs_, index);
    for (int i = 0; i < 2; ++i) {
        if (i != index && GetFocus() == editors_[i]) SetFocus(tabs_);
        SetWindowLongPtrW(editors_[i], GWL_STYLE, (GetWindowLongPtrW(editors_[i], GWL_STYLE) & ~WS_TABSTOP) | (i == index ? WS_TABSTOP : 0));
        ShowWindow(editors_[i], i == index ? SW_SHOWNA : SW_HIDE);
        ShowWindow(headings_[i], i == index ? SW_SHOWNA : SW_HIDE);
    }
    layout(); InvalidateRect(tabs_, nullptr, FALSE);
}
void HttpErrorDetailWindow::updateFonts() {
    if (!resources_.update(instance_, static_cast<int>(GetDpiForWindow(window_)))) return;
    SendMessageW(tabs_, WM_SETFONT, reinterpret_cast<WPARAM>(resources_.font(ui::Font::label)), TRUE);
    for (int i = 0; i < 2; ++i) {
        SendMessageW(editors_[i], WM_SETFONT, reinterpret_cast<WPARAM>(resources_.font(ui::Font::mono)), TRUE);
        SendMessageW(headings_[i], WM_SETFONT, reinterpret_cast<WPARAM>(resources_.font(ui::Font::label)), TRUE);
    }
    resources_.applyApplicationIcons(window_);
}
void HttpErrorDetailWindow::layout() {
    if (!tabs_ || !editors_[1]) return;
    LayoutFrame frame(window_);
    RECT client{}; GetClientRect(window_, &client);
    const auto s = [this](int n) { return scaleForDpi(n, resources_.dpi()); };
    const int margin = s(12), inset = s(8);
    const Rect tabs{margin, margin, std::max(margin + 1, static_cast<int>(client.right) - margin), std::max(margin + 1, static_cast<int>(client.bottom) - margin)};
    RECT page{tabs.left, tabs.top, tabs.right, tabs.bottom}; TabCtrl_AdjustRect(tabs_, FALSE, &page);
    card_ = {page.left, page.top, page.right, page.bottom};
    const int width = std::max(1, static_cast<int>(page.right - page.left) - inset * 2);
    HDC dc = GetDC(window_);
    const auto measured = measureText(dc, resources_.font(ui::Font::label), notes_[selected_], width, true);
    ReleaseDC(window_, dc);
    const int headingBottom = std::min(static_cast<int>(page.bottom) - s(64), static_cast<int>(page.top) + inset + measured.height);
    std::vector<ControlPlacement> controls{{tabs_, tabs}};
    for (int i = 0; i < 2; ++i) {
        controls.push_back({headings_[i], {page.left + inset, page.top + inset, page.right - inset, headingBottom}});
        controls.push_back({editors_[i], {page.left + inset, headingBottom + inset, page.right - inset, page.bottom - inset}});
    }
    positionControls(window_, controls); InvalidateRect(window_, nullptr, FALSE);
}
bool HttpErrorDetailWindow::translate(MSG& message) {
    return window_ && (message.hwnd == window_ || IsChild(window_, message.hwnd)) && IsDialogMessageW(window_, &message);
}
LRESULT CALLBACK HttpErrorDetailWindow::procedure(HWND window, UINT message, WPARAM w, LPARAM l) {
    auto* self = reinterpret_cast<HttpErrorDetailWindow*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) { self = static_cast<HttpErrorDetailWindow*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams); self->window_ = window; SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self)); }
    if (!self) return DefWindowProcW(window, message, w, l);
    switch (message) {
        case WM_DRAWITEM: if (l && drawModernTab(*reinterpret_cast<DRAWITEMSTRUCT*>(l), self->tabs_, self->resources_)) return TRUE; break;
        case WM_NOTIFY: if (l && reinterpret_cast<NMHDR*>(l)->hwndFrom == self->tabs_ && reinterpret_cast<NMHDR*>(l)->code == TCN_SELCHANGE) { self->selectTab(TabCtrl_GetCurSel(self->tabs_)); return 0; } break;
        case WM_SIZE: self->layout(); return 0;
        case WM_DPICHANGED: applyDpiRect(window, l); self->updateFonts(); self->layout(); return 0;
        case WM_SETTINGCHANGE: case WM_SYSCOLORCHANGE: case WM_THEMECHANGED: self->updateFonts(); self->layout(); return 0;
        case WM_GETMINMAXINFO: setMinimumClientTrackSize(window, *reinterpret_cast<MINMAXINFO*>(l), {480, 400}); return 0;
        case WM_ERASEBKGND: return 1;
        case WM_PAINT: {
            PAINTSTRUCT paint{}; auto dc = BeginPaint(window, &paint); PaintBuffer buffer(dc);
            FillRect(buffer.dc(), &paint.rcPaint, self->resources_.brush(ui::Color::background));
            paintCard(buffer.dc(), self->card_, self->resources_, ui::Color::disabledBackground, ui::Color::border);
            buffer.finish(); EndPaint(window, &paint); return 0;
        }
        case WM_CTLCOLOREDIT: case WM_CTLCOLORSTATIC: {
            auto dc = reinterpret_cast<HDC>(w); SetTextColor(dc, self->resources_.color(ui::Color::text));
            SetBkColor(dc, self->resources_.color(ui::Color::disabledBackground));
            return reinterpret_cast<LRESULT>(self->resources_.brush(ui::Color::disabledBackground));
        }
        case WM_CLOSE: self->close(); return 0;
        case WM_DESTROY: self->window_ = self->tabs_ = nullptr; self->editors_ = {}; self->headings_ = {}; self->notes_ = {}; return 0;
        case WM_NCDESTROY: self->resources_.clear(); SetWindowLongPtrW(window, GWLP_USERDATA, 0); break;
        default: break;
    }
    return DefWindowProcW(window, message, w, l);
}
}
