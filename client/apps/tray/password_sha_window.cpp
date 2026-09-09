#include "password_sha_window.h"
#include "ui_controls.h"
#include "window_utils.h"
#include <windowsx.h>

namespace phpvpn::tray {
namespace {
constexpr wchar_t className[] = L"PhpVpnPasswordSha";
constexpr int defaultId = 503, randomId = 504;
}
bool PasswordShaWindow::open(HINSTANCE instance, HWND parent) {
    if (window_) { reactivateWindow(window_); return true; }
    instance_ = instance; parent_ = parent;
    WNDCLASSEXW type{sizeof(type)}; type.hInstance = instance; type.lpfnWndProc = procedure;
    type.lpszClassName = className; type.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    if (!RegisterClassExW(&type) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;
    const auto title = widen(target_.passwordTitle());
    // Owned but independently switchable. Ownership does not disable parent.
    window_ = CreateWindowExW(WS_EX_COMPOSITED | WS_EX_APPWINDOW | WS_EX_CONTROLPARENT, className, title.c_str(),
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, 0, 0, 1, 1, parent, nullptr, instance, this);
    if (!window_) return false;
    body_=CreateWindowExW(WS_EX_CONTROLPARENT,L"STATIC",L"",WS_CHILD|WS_VISIBLE|WS_CLIPCHILDREN|WS_VSCROLL,
        0,0,1,1,window_,nullptr,instance,nullptr);
    if(!body_ || !SetWindowSubclass(body_,bodyProcedure,2,reinterpret_cast<DWORD_PTR>(this))) { close(); return false; }
    auto control = [&](const wchar_t* kind, const wchar_t* text, DWORD style, int id) {
        const bool modernText = std::wstring_view(kind) == L"EDIT" && !(style & ES_MULTILINE);
        auto child = CreateWindowExW(0, kind, text, WS_CHILD | (modernText ? 0 : WS_VISIBLE) | style, 0, 0, 1, 1, (id==IDOK || id==defaultId || id==randomId)?window_:body_,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance, nullptr);
        if (modernText && child) {
            if (!attachModernTextBox(child, resources_, ui::Color::background)) { DestroyWindow(child); return HWND{}; }
            ShowWindow(child, SW_SHOWNA);
        }
        return child;
    };
    const wchar_t* texts[]{L"Generate key from password", L"Enter a password to create a SHA-256 communication key.",
        L"Password", L"The key updates automatically as you type.", L"SHA-256 key", L""};
    for (std::size_t i = 0; i < labels_.size(); ++i) labels_[i] = control(L"STATIC", texts[i], SS_NOPREFIX, 0);
    input_ = control(L"EDIT", L"", WS_TABSTOP | ES_AUTOHSCROLL, 501);
    hash_ = control(L"EDIT", L"", WS_TABSTOP | ES_AUTOHSCROLL | ES_READONLY, 502);
    default_ = control(L"BUTTON", L"Default", WS_TABSTOP | BS_OWNERDRAW, defaultId);
    random_ = control(L"BUTTON", L"Random", WS_TABSTOP | BS_OWNERDRAW, randomId);
    ok_ = control(L"BUTTON", L"OK", WS_TABSTOP | BS_OWNERDRAW, IDOK);
    bool complete = input_ && hash_ && attachModernButton(default_, resources_, ButtonRole::reset)
        && attachModernButton(random_, resources_, ButtonRole::reset)
        && attachModernButton(ok_, resources_, ButtonRole::primary);
    for (auto label : labels_) complete = complete && label;
    if (!complete || !inputAdapter_.attach(input_, 510, [this](std::wstring_view text) { return update(text); })) { close(); return false; }
    SendMessageW(input_, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"CHANGE-THIS-PASSWORD"));
    resources(); sizeAndCenter(window_, ui::passwordClient, {parent, {}}); layout(); update(L"");
    reactivateWindow(window_); SetFocus(input_);
    return true;
}
bool PasswordShaWindow::update(std::wstring_view text) {
    const auto error = model_.update(crypto_, {reinterpret_cast<const char16_t*>(text.data()), text.size()});
    const wchar_t* notice = L"";
    if (error == PasswordKeyError::tooLong) notice = L"Password must contain at most 255 Unicode characters.";
    else if (error == PasswordKeyError::invalidUtf16) notice = L"Password contains an incomplete Unicode character.";
    else if (error == PasswordKeyError::unsupportedControl) notice = L"Password contains an unsupported control character.";
    else if (error == PasswordKeyError::cryptoFailed) notice = L"The SHA-256 key could not be generated.";
    SetWindowTextW(labels_[5], notice);
    auto key = widen(model_.key()); SetWindowTextW(hash_, key.c_str()); secureWipe(key.data(), key.size() * sizeof(wchar_t));
    EnableWindow(ok_, model_.valid());
    return error != PasswordKeyError::tooLong && error != PasswordKeyError::unsupportedControl;
}
void PasswordShaWindow::randomPassword() {
    constexpr std::wstring_view alphabet = L"ghjkmnpqrstuvwxyz";
    constexpr auto limit = 256 - (256 % alphabet.size());
    SecureBuffer bytes(64);
    std::array<wchar_t, 64> password{};
    struct Wiper {
        std::array<wchar_t, 64>& value;
        ~Wiper() { secureWipe(value.data(), value.size() * sizeof(wchar_t)); }
    } wipe{password};
    std::size_t used = 0;
    while (used < password.size()) {
        if (!crypto_.randomBytes(bytes.mutableSpan())) {
            SetWindowTextW(labels_[5], L"A random password could not be generated. The current password has been kept.");
            return;
        }
        for (auto byte : bytes.span()) {
            // Reject 255 for this 17-character alphabet, so every character
            // has exactly 15 possible byte values and no modulo bias.
            if (byte >= limit) continue;
            password[used++] = alphabet[byte % alphabet.size()];
            if (used == password.size()) break;
        }
    }
    const std::wstring_view text(password.data(), password.size());
    inputAdapter_.set(text);
    update(text);
}
bool PasswordShaWindow::translate(MSG& message) const {
    if (!window_ || (message.hwnd != window_ && !IsChild(window_, message.hwnd))) return false;
    if (message.message == WM_KEYDOWN && message.wParam == VK_ESCAPE) { SendMessageW(window_, WM_CLOSE, 0, 0); return true; }
    if (message.message == WM_KEYDOWN && message.wParam == VK_RETURN && GetFocus() != default_ && GetFocus() != random_ && GetFocus() != ok_) {
        SendMessageW(window_, WM_COMMAND, IDOK, 0); return true;
    }
    return IsDialogMessageW(window_, &message) != FALSE;
}
void PasswordShaWindow::resources() {
    if (!resources_.update(instance_, static_cast<int>(GetDpiForWindow(window_)), false)) return;
    for (auto h : labels_) SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(resources_.font(ui::Font::label)), TRUE);
    SendMessageW(labels_[0], WM_SETFONT, reinterpret_cast<WPARAM>(resources_.font(ui::Font::card)), TRUE);
    SendMessageW(labels_[3], WM_SETFONT, reinterpret_cast<WPARAM>(resources_.font(ui::Font::hint)), TRUE);
    for (auto h : {input_, hash_, default_, random_, ok_}) SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(resources_.font(h == hash_ ? ui::Font::mono : ui::Font::value)), TRUE);
}
void PasswordShaWindow::layout() {
    if (!ok_) return;
    LayoutFrame frame(window_);
    RECT r{}; GetClientRect(window_, &r); const auto s = [this](int v) { return scaleForDpi(v, resources_.dpi()); };
    const int margin = s(24), footer = (std::max)(0,static_cast<int>(r.bottom)-s(60));
    positionControls(window_, std::vector<ControlPlacement>{
        {body_, {0, 0, r.right, footer}},
        {default_, {margin, footer+s(16), margin+s(104), footer+s(52)}},
        {random_, {margin+s(116), footer+s(16), margin+s(220), footer+s(52)}},
        {ok_, {r.right-margin-s(104), footer+s(16), r.right-margin, footer+s(52)}}});
    SCROLLINFO info{sizeof(info),SIF_RANGE|SIF_PAGE|SIF_POS}; info.nMax=s(296)-1; info.nPage=footer;
    scroll_=std::clamp(scroll_,0,(std::max)(0,info.nMax+1-footer)); info.nPos=scroll_; SetScrollInfo(body_,SB_VERT,&info,TRUE);
    RECT body{}; GetClientRect(body_,&body); const int right=(std::max)(margin,static_cast<int>(body.right)-margin);
    std::vector<ControlPlacement> p;
    auto add = [&](HWND h, int y, int height) { p.push_back({h, {margin, s(y)-scroll_, right, s(y + height)-scroll_}}); };
    add(labels_[0], 24, 28); add(labels_[1], 60, 42); add(labels_[2], 108, 20);
    add(input_, 132, 36); add(labels_[3], 176, 20); add(labels_[4], 204, 20); add(hash_, 228, 36); add(labels_[5], 268, 28);
    positionControls(body_, p);
}
LRESULT CALLBACK PasswordShaWindow::bodyProcedure(HWND h,UINT m,WPARAM w,LPARAM l,UINT_PTR,DWORD_PTR data) {
    auto* self=reinterpret_cast<PasswordShaWindow*>(data);
    if(m==WM_COMMAND || m==WM_CTLCOLORSTATIC || m==WM_CTLCOLOREDIT) return SendMessageW(self->window_,m,w,l);
    if(m==WM_VSCROLL || m==WM_MOUSEWHEEL) {
        SCROLLINFO info{sizeof(info),SIF_ALL}; GetScrollInfo(h,SB_VERT,&info); int position=self->scroll_;
        if(m==WM_MOUSEWHEEL) position-=GET_WHEEL_DELTA_WPARAM(w)/WHEEL_DELTA*scaleForDpi(48,self->resources_.dpi());
        else switch(LOWORD(w)) {
        case SB_LINEUP:position-=scaleForDpi(24,self->resources_.dpi());break;
        case SB_LINEDOWN:position+=scaleForDpi(24,self->resources_.dpi());break;
        case SB_PAGEUP:position-=info.nPage;break;case SB_PAGEDOWN:position+=info.nPage;break;
        case SB_THUMBPOSITION:case SB_THUMBTRACK:position=info.nTrackPos;break;
        }
        self->scroll_=std::clamp(position,0,(std::max)(0,info.nMax+1-static_cast<int>(info.nPage))); self->layout(); InvalidateRect(h,nullptr,TRUE); return 0;
    }
    if (m == WM_ERASEBKGND) return 1;
    if (m == WM_PAINT) {
        PAINTSTRUCT p{}; auto target = BeginPaint(h, &p); PaintBuffer buffer(target);
        FillRect(buffer.dc(), &p.rcPaint, self->resources_.brush(ui::Color::background));
        buffer.finish(); EndPaint(h, &p); return 0;
    }
    if(m==WM_NCDESTROY) RemoveWindowSubclass(h,bodyProcedure,2);
    return DefSubclassProc(h,m,w,l);
}
void PasswordShaWindow::close() { if (window_) { DestroyWindow(window_); if (closed_) closed_(target_); } }
LRESULT CALLBACK PasswordShaWindow::procedure(HWND h, UINT m, WPARAM w, LPARAM l) {
    auto* self = reinterpret_cast<PasswordShaWindow*>(GetWindowLongPtrW(h, GWLP_USERDATA));
    if (m == WM_NCCREATE) { self = static_cast<PasswordShaWindow*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams); self->window_ = h; SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self)); }
    if (!self) return DefWindowProcW(h, m, w, l);
    switch (m) {
    case WM_COMMAND:
        if ((LOWORD(w)==501 || LOWORD(w)==502) && HIWORD(w)==EN_SETFOCUS) {
            RECT area{}, target{}; GetClientRect(self->body_,&area); GetWindowRect(reinterpret_cast<HWND>(l),&target);
            MapWindowPoints(nullptr,self->body_,reinterpret_cast<POINT*>(&target),2);
            if(target.top<0) self->scroll_+=target.top; else if(target.bottom>area.bottom) self->scroll_+=target.bottom-area.bottom;
            self->layout(); return 0;
        }
        if (LOWORD(w) == randomId) { self->randomPassword(); return 0; }
        if (LOWORD(w) == defaultId) {
            // Captured numeric/canonical pair, with no Wizard candidate-range restriction.
            auto password = widen(self->target_.contractId());
            self->inputAdapter_.set(password);
            self->update(password);
            secureWipe(password.data(), password.size() * sizeof(wchar_t));
            return 0;
        }
        if (LOWORD(w) == IDOK && self->model_.valid()) {
            if (self->apply_ && self->apply_(self->target_, self->model_.key())) self->close();
            return 0;
        }
        if (LOWORD(w) == IDCANCEL) { self->close(); return 0; }
        if (LOWORD(w) == 501 && HIWORD(w) == EN_CHANGE && !self->inputAdapter_.bypassing()) {
            const auto n = GetWindowTextLengthW(self->input_); std::wstring text(n + 1, L'\0');
            GetWindowTextW(self->input_, text.data(), n + 1); text.resize(n); self->update(text);
            secureWipe(text.data(), text.size() * sizeof(wchar_t));
        }
        return 0;
    case WM_SIZE: self->layout(); return 0;
    case WM_GETMINMAXINFO: {
        const UINT dpi = GetDpiForWindow(h);
        RECT minimum{0, 0, scaleForDpi(480, static_cast<int>(dpi)), scaleForDpi(240, static_cast<int>(dpi))};
        AdjustWindowRectExForDpi(&minimum, static_cast<DWORD>(GetWindowLongPtrW(h, GWL_STYLE)), FALSE,
            static_cast<DWORD>(GetWindowLongPtrW(h, GWL_EXSTYLE)), dpi);
        auto* info = reinterpret_cast<MINMAXINFO*>(l);
        info->ptMinTrackSize = {minimum.right - minimum.left, minimum.bottom - minimum.top};
        MONITORINFO monitor{sizeof(monitor)};
        if (GetMonitorInfoW(MonitorFromWindow(h, MONITOR_DEFAULTTONEAREST), &monitor)) {
            info->ptMinTrackSize.x = (std::min)(info->ptMinTrackSize.x, monitor.rcWork.right - monitor.rcWork.left);
            info->ptMinTrackSize.y = (std::min)(info->ptMinTrackSize.y, monitor.rcWork.bottom - monitor.rcWork.top);
        }
        return 0;
    }
    case WM_MOUSEWHEEL: return SendMessageW(self->body_,m,w,l);
    case WM_DPICHANGED: self->resources(); applyDpiRect(h, l); self->layout(); return 0;
    case WM_SETTINGCHANGE: case WM_SYSCOLORCHANGE: self->resources(); self->layout(); break;
    case WM_QUERYENDSESSION: return TRUE;
    case WM_ENDSESSION: if (w) { self->closed_={}; self->close(); } return 0;
    case WM_CLOSE: self->close(); return 0;
    case WM_DESTROY:
        self->inputAdapter_.clear(); SetWindowTextW(self->hash_, L""); SendMessageW(self->hash_, EM_EMPTYUNDOBUFFER, 0, 0);
        self->model_.clear(); self->window_ = nullptr; return 0;
    case WM_NCDESTROY:
        self->body_ = self->input_ = self->hash_ = self->default_ = self->random_ = self->ok_ = nullptr; self->labels_ = {}; self->resources_.clear();
        SetWindowLongPtrW(h, GWLP_USERDATA, 0); break;
    case WM_CTLCOLORSTATIC: case WM_CTLCOLOREDIT: {
        const auto dc = reinterpret_cast<HDC>(w); SetTextColor(dc, self->resources_.color(ui::Color::text));
        const auto field = reinterpret_cast<HWND>(l);
        const auto color=(field==self->input_ || field==self->hash_)?ui::Color::surface:ui::Color::background;
        SetBkColor(dc, self->resources_.color(color)); return reinterpret_cast<LRESULT>(self->resources_.brush(color));
    }
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT p{}; auto targetDc = BeginPaint(h, &p); PaintBuffer buffer(targetDc); auto dc = buffer.dc();
        RECT r{}; GetClientRect(h, &r); FillRect(dc, &r, self->resources_.brush(ui::Color::background));
        RECT bodyBounds{}; GetWindowRect(self->body_, &bodyBounds);
        MapWindowPoints(nullptr, h, reinterpret_cast<POINT*>(&bodyBounds), 2);
        const RECT divider{0, bodyBounds.bottom, r.right, bodyBounds.bottom + 1};
        FillRect(dc, &divider, self->resources_.brush(ui::Color::separator));
        buffer.finish(); EndPaint(h, &p); return 0;
    }
    }
    return DefWindowProcW(h, m, w, l);
}
} // namespace phpvpn::tray
