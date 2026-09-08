#include "ui_controls.h"
#include <algorithm>
#include <limits>
#include <commctrl.h>
#include <string>
#include <new>
#include <objidl.h>
#include <gdiplus.h>
#include <oleacc.h>
#pragma comment(lib, "gdiplus.lib")

namespace phpvpn::tray {
namespace {
struct GraphicsRuntime {
    ULONG_PTR token = 0;
    GraphicsRuntime() {
        Gdiplus::GdiplusStartupInput input;
        if (Gdiplus::GdiplusStartup(&token, &input, nullptr) != Gdiplus::Ok) token = 0;
    }
    ~GraphicsRuntime() { if (token) Gdiplus::GdiplusShutdown(token); }
};
bool smoothShape(HDC dc, Rect rect, int diameter, const UiResources& resources,
                 ui::Color fill, ui::Color border, bool ellipse, float physicalStroke = 0) {
    static GraphicsRuntime runtime;
    if (!runtime.token) return false;
    const auto color = [&](ui::Color role) {
        const auto c = resources.color(role);
        return Gdiplus::Color(255, GetRValue(c), GetGValue(c), GetBValue(c));
    };
    Gdiplus::Graphics graphics(dc);
    if (graphics.GetLastStatus() != Gdiplus::Ok ||
        graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias) != Gdiplus::Ok) return false;
    // Geometry is in physical pixels, as are the existing DPI-aware layouts.
    // Half-pixel stroke centres keep straight sections on exact pixel bounds.
    const float stroke = physicalStroke > 0 ? physicalStroke : static_cast<float>(std::max(1, scaleForDpi(1, resources.dpi())));
    const float inset = fill == border ? 0.0f : stroke / 2;
    const float x = static_cast<float>(rect.left) + inset, y = static_cast<float>(rect.top) + inset;
    const float w = static_cast<float>(rect.right - rect.left) - 2 * inset;
    const float h = static_cast<float>(rect.bottom - rect.top) - 2 * inset;
    if (w <= 0 || h <= 0) return false;
    Gdiplus::GraphicsPath path;
    if (ellipse) {
        if (path.AddEllipse(x, y, w, h) != Gdiplus::Ok) return false;
    } else {
        const float d = std::min({static_cast<float>(diameter), w, h});
        if (d <= 0) return false;
        if (path.AddArc(x, y, d, d, 180, 90) != Gdiplus::Ok ||
            path.AddArc(x + w - d, y, d, d, 270, 90) != Gdiplus::Ok ||
            path.AddArc(x + w - d, y + h - d, d, d, 0, 90) != Gdiplus::Ok ||
            path.AddArc(x, y + h - d, d, d, 90, 90) != Gdiplus::Ok ||
            path.CloseFigure() != Gdiplus::Ok) return false;
    }
    Gdiplus::SolidBrush brush(color(fill));
    Gdiplus::Pen pen(color(border), stroke);
    if (brush.GetLastStatus() != Gdiplus::Ok || pen.GetLastStatus() != Gdiplus::Ok) return false;
    if (graphics.FillPath(&brush, &path) != Gdiplus::Ok) return false;
    return fill == border || graphics.DrawPath(&pen, &path) == Gdiplus::Ok;
}
constexpr UINT_PTR textBoxId = 0x50425639;
struct ModernTextBox {
    const UiResources* resources;
    ui::Color background;
    bool invalid = false;
};
bool textBoxEnabled(HWND window) {
    for (auto current = window; current; current = GetParent(current))
        if (!IsWindowEnabled(current)) return false;
    return true;
}
ui::Color textBoxFill(HWND window, const ModernTextBox& box) {
    return !textBoxEnabled(window) ? ui::Color::disabledBackground :
        box.invalid ? ui::Color::errorBackground : ui::Color::surface;
}
void repaintTextBox(HWND window) {
    RedrawWindow(window, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME);
}
void textBoxFrame(HWND window) {
    SetWindowPos(window, nullptr, 0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    repaintTextBox(window);
}
void paintTextBoxFrame(HWND window, HDC dc, const ModernTextBox& box) {
    const int saved = SaveDC(dc);
    RECT bounds{}, client{}; GetWindowRect(window, &bounds); GetClientRect(window, &client);
    POINT origin{}; ClientToScreen(window, &origin);
    OffsetRect(&client, origin.x - bounds.left, origin.y - bounds.top);
    OffsetRect(&bounds, -bounds.left, -bounds.top);
    ExcludeClipRect(dc, client.left, client.top, client.right, client.bottom);
    PaintBuffer buffer(dc); auto target = buffer.dc();
    const auto& resources = *box.resources;
    FillRect(target, &bounds, resources.brush(box.background));
    const bool enabled = textBoxEnabled(window);
    const bool focus = enabled && GetFocus() == window;
    const auto fill = textBoxFill(window, box);
    const auto border = !enabled ? ui::Color::border : box.invalid ? ui::Color::error :
        focus ? ui::Color::focus : ui::Color::border;
    const int diameter = scaleForDpi(12, resources.dpi());
    const float stroke = focus || (enabled && box.invalid) ? 2.0f : 1.0f;
    const Rect rect{0, 0, bounds.right, bounds.bottom};
    if (!smoothShape(target, rect, diameter, resources, fill, border, false, stroke)) {
        auto pen = CreatePen(PS_SOLID, static_cast<int>(stroke), resources.color(border));
        auto oldPen = SelectObject(target, pen); auto oldBrush = SelectObject(target, resources.brush(fill));
        RoundRect(target, 0, 0, bounds.right, bounds.bottom, diameter, diameter);
        SelectObject(target, oldBrush); SelectObject(target, oldPen); DeleteObject(pen);
    }
    if (focus) {
        const int inset = 2;
        smoothShape(target, {inset, inset, bounds.right-inset, bounds.bottom-inset},
            std::max(1, diameter-2*inset), resources, fill,
            box.invalid ? ui::Color::errorBorder : ui::Color::selectedStep, false, 1.0f);
    }
    buffer.finish(); RestoreDC(dc, saved);
}
LRESULT CALLBACK textBoxProcedure(HWND window, UINT message, WPARAM w, LPARAM l,
                                  UINT_PTR id, DWORD_PTR data) {
    const auto* box = reinterpret_cast<ModernTextBox*>(data);
    // Classic EDIT does not consistently bind Select All across Windows
    // versions. Delegate the operation itself to the native selection API.
    if (message == WM_CHAR && w == 1 && textBoxEnabled(window)) {
        SendMessageW(window, EM_SETSEL, 0, -1); return 0;
    }
    if (message == WM_PRINT) {
        const auto result = DefSubclassProc(window, message, w, l & ~PRF_NONCLIENT);
        DWORD_PTR alive = 0;
        if ((l & PRF_NONCLIENT) && GetWindowSubclass(window, textBoxProcedure, id, &alive))
            paintTextBoxFrame(window, reinterpret_cast<HDC>(w), *reinterpret_cast<ModernTextBox*>(alive));
        return result;
    }
    if (message == WM_NCCALCSIZE) {
        auto& rect = w ? reinterpret_cast<NCCALCSIZE_PARAMS*>(l)->rgrc[0] : *reinterpret_cast<RECT*>(l);
        const int width = rect.right-rect.left, height = rect.bottom-rect.top;
        const int padding = scaleForDpi(8, box->resources->dpi());
        HDC dc = GetDC(window);
        const auto font = reinterpret_cast<HFONT>(SendMessageW(window, WM_GETFONT, 0, 0));
        const auto old = font ? SelectObject(dc, font) : nullptr;
        TEXTMETRICW metrics{}; GetTextMetricsW(dc, &metrics);
        if (old) SelectObject(dc, old); ReleaseDC(window, dc);
        const int x = std::min(padding, std::max(0, (width-1)/2));
        const int y = std::min(std::max(scaleForDpi(3, box->resources->dpi()), (height-static_cast<int>(metrics.tmHeight))/2),
            std::max(0, (height-1)/2));
        InflateRect(&rect, -x, -y);
        return 0;
    }
    if (message == WM_NCPAINT) {
        const auto dc = GetWindowDC(window);
        if (dc) { paintTextBoxFrame(window, dc, *box); ReleaseDC(window, dc); }
        return 0;
    }
    if (message == WM_NCHITTEST) return HTCLIENT;
    if (message == WM_NCDESTROY) {
        RemoveWindowSubclass(window, textBoxProcedure, id); delete box;
        return DefSubclassProc(window, message, w, l);
    }
    const auto result = DefSubclassProc(window, message, w, l);
    // Native notifications can synchronously destroy the control. Never use
    // the saved state after calling through the native text implementation.
    DWORD_PTR alive = 0;
    if (!GetWindowSubclass(window, textBoxProcedure, id, &alive)) return result;
    switch (message) {
    case WM_SETFONT: case WM_DPICHANGED_AFTERPARENT:
        textBoxFrame(window); break;
    case WM_SETFOCUS: case WM_KILLFOCUS: case WM_ENABLE: case WM_SETTEXT:
    case WM_SIZE: case WM_THEMECHANGED: case WM_SYSCOLORCHANGE:
        repaintTextBox(window); break;
    }
    return result;
}
LRESULT CALLBACK textBoxParentProcedure(HWND window, UINT message, WPARAM w, LPARAM l,
                                        UINT_PTR id, DWORD_PTR) {
    DWORD_PTR data = 0;
    if ((message == WM_CTLCOLOREDIT || message == WM_CTLCOLORSTATIC) && l &&
        GetWindowSubclass(reinterpret_cast<HWND>(l), textBoxProcedure, textBoxId, &data)) {
        const auto& box = *reinterpret_cast<ModernTextBox*>(data);
        const auto field = reinterpret_cast<HWND>(l);
        const auto fill = textBoxFill(field, box);
        const auto dc = reinterpret_cast<HDC>(w);
        SetBkColor(dc, box.resources->color(fill));
        SetTextColor(dc, box.resources->color(textBoxEnabled(field) ? ui::Color::text : ui::Color::disabledText));
        return reinterpret_cast<LRESULT>(box.resources->brush(fill));
    }
    if (message == WM_NCDESTROY) RemoveWindowSubclass(window, textBoxParentProcedure, id);
    return DefSubclassProc(window, message, w, l);
}
void paintToggle(HWND window, HDC dc, const UiResources& resources) {
    PaintBuffer buffer(dc); dc = buffer.dc();
    const int saved = SaveDC(dc);
    RECT bounds{}; GetClientRect(window, &bounds);
    const auto background = reinterpret_cast<HBRUSH>(SendMessageW(GetParent(window), WM_CTLCOLORSTATIC,
        reinterpret_cast<WPARAM>(dc), reinterpret_cast<LPARAM>(window)));
    FillRect(dc, &bounds, background ? background : resources.brush(ui::Color::surface));
    const auto s = [&](int value) { return scaleForDpi(value, resources.dpi()); };
    const bool checked = SendMessageW(window, BM_GETCHECK, 0, 0) == BST_CHECKED;
    const bool enabled = IsWindowEnabled(window) != FALSE;
    const bool pressed = (SendMessageW(window, BM_GETSTATE, 0, 0) & BST_PUSHED) != 0;
    const int length = GetWindowTextLengthW(window);
    std::wstring text(static_cast<std::size_t>(length) + 1, L'\0');
    text.resize(GetWindowTextW(window, text.data(), length + 1));
    const int width = s(36), height = s(20), inset = s(3);
    const int x = text.empty() ? (bounds.right - width) / 2 : s(3);
    const int y = (bounds.bottom - height) / 2;
    const auto fill = !enabled ? ui::Color::border : checked ?
        (pressed ? ui::Color::primaryPressed : ui::Color::primary) : ui::Color::disabledText;
    paintRoundedShape(dc, {x, y, x + width, y + height}, height, resources, fill, fill);
    const auto thumbColor = enabled ? ui::Color::primaryText : ui::Color::disabledBackground;
    const int thumb = x + (checked ? width - height : 0);
    paintRoundedShape(dc, {thumb + inset, y + inset, thumb + height - inset, y + height - inset},
                      0, resources, thumbColor, thumbColor, true);
    RECT label = bounds; label.left = s(51); label.right -= s(3);
    SelectObject(dc, reinterpret_cast<HFONT>(SendMessageW(window, WM_GETFONT, 0, 0)));
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, resources.color(enabled ? ui::Color::text : ui::Color::disabledText));
    RECT measured = label;
    DrawTextW(dc, text.c_str(), length, &measured, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
    label.top = std::max(0L, (bounds.bottom - measured.bottom + measured.top) / 2);
    DrawTextW(dc, text.c_str(), length, &label, DT_WORDBREAK | DT_NOPREFIX);
    if (GetFocus() == window && !(SendMessageW(window, WM_QUERYUISTATE, 0, 0) & UISF_HIDEFOCUS)) {
        RECT focus = bounds; InflateRect(&focus, -s(1), -s(1));
        SetTextColor(dc, resources.color(ui::Color::focus));
        DrawFocusRect(dc, &focus);
    }
    RestoreDC(dc, saved);
}
struct ToggleSwitch { const UiResources* resources; bool checked = false; };
LRESULT accessibleResult(HWND window, WPARAM w, LPARAM l, bool toggle);
LRESULT CALLBACK toggleProcedure(HWND window, UINT message, WPARAM w, LPARAM l,
                                 UINT_PTR id, DWORD_PTR data) {
    auto* toggle = reinterpret_cast<ToggleSwitch*>(data);
    const auto& resources = *toggle->resources;
    if (message == BM_CLICK && !IsWindowEnabled(window)) return 0;
    if (message == BM_GETCHECK) return toggle->checked ? BST_CHECKED : BST_UNCHECKED;
    if (message == BM_SETCHECK) {
        const bool checked = w == BST_CHECKED;
        if (toggle->checked != checked) {
            toggle->checked = checked;
            InvalidateRect(window, nullptr, FALSE);
            NotifyWinEvent(EVENT_OBJECT_STATECHANGE, window, OBJID_CLIENT, CHILDID_SELF);
        }
        return 0;
    }
    if (message == WM_GETOBJECT && static_cast<LONG>(l) == OBJID_CLIENT)
        return accessibleResult(window, w, l, true);
    if (message == WM_GETDLGCODE)
        return (DefSubclassProc(window, message, w, l) & ~(DLGC_DEFPUSHBUTTON | DLGC_UNDEFPUSHBUTTON)) | DLGC_BUTTON;
    if (message == BM_SETSTYLE) w = (w & ~BS_TYPEMASK) | BS_OWNERDRAW;
    if (message == WM_PAINT) {
        PAINTSTRUCT paint{}; const auto dc = BeginPaint(window, &paint);
        paintToggle(window, dc, resources); EndPaint(window, &paint); return 0;
    }
    if (message == WM_PRINTCLIENT) { paintToggle(window, reinterpret_cast<HDC>(w), resources); return 0; }
    if (message == WM_ERASEBKGND) return 1;
    if (message == WM_NCDESTROY) { RemoveWindowSubclass(window, toggleProcedure, id); delete toggle; }
    const auto result = DefSubclassProc(window, message, w, l);
    switch (message) {
    case BM_SETCHECK: case BM_SETSTATE: case WM_ENABLE: case WM_SETFOCUS: case WM_KILLFOCUS:
    case WM_UPDATEUISTATE: case WM_SETTEXT: case WM_SETFONT: case WM_THEMECHANGED: case WM_SIZE:
    case WM_MOUSEMOVE: case WM_MOUSELEAVE: case WM_LBUTTONDOWN: case WM_LBUTTONUP:
    case WM_CAPTURECHANGED: case WM_CANCELMODE: case BM_SETSTYLE:
        if (IsWindow(window)) InvalidateRect(window, nullptr, FALSE); break;
    default: break;
    }
    return result;
}
LRESULT CALLBACK toggleParentProcedure(HWND window, UINT message, WPARAM w, LPARAM l,
                                      UINT_PTR id, DWORD_PTR) {
    DWORD_PTR data = 0;
    if (message == WM_COMMAND && l && HIWORD(w) == BN_CLICKED &&
        GetWindowSubclass(reinterpret_cast<HWND>(l), toggleProcedure, 0, &data)) {
        if (!IsWindowEnabled(reinterpret_cast<HWND>(l))) return 0;
        // BUTTON owns click qualification, capture and Space/BM_CLICK activation.
        // Commit exactly once, before the application's BN_CLICKED handler reads it.
        auto* toggle = reinterpret_cast<ToggleSwitch*>(data);
        SendMessageW(reinterpret_cast<HWND>(l), BM_SETCHECK, toggle->checked ? BST_UNCHECKED : BST_CHECKED, 0);
    }
    if (message == WM_DRAWITEM && l) {
        const auto* draw = reinterpret_cast<const DRAWITEMSTRUCT*>(l);
        if (draw->CtlType == ODT_BUTTON && GetWindowSubclass(draw->hwndItem, toggleProcedure, 0, &data)) {
            InvalidateRect(draw->hwndItem, nullptr, FALSE); UpdateWindow(draw->hwndItem); return TRUE;
        }
    }
    if (message == WM_NCDESTROY) RemoveWindowSubclass(window, toggleParentProcedure, id);
    return DefSubclassProc(window, message, w, l);
}
constexpr UINT_PTR modernButtonId = 0x50425634;
struct ModernButton {
    const UiResources* resources;
    ButtonRole role;
    ui::Color background;
    bool hover = false;
    bool isDefault = false;
};
// Owner-draw BUTTON retains its standard MSAA provider, except Windows derives
// STATE_SYSTEM_DEFAULT from BS_DEFPUSHBUTTON. Forward everything to that provider
// and supply the semantic default bit kept by our dialog-code adapter.
class ButtonAccessible final : public IAccessible {
public:
    ButtonAccessible(HWND window, IAccessible* native, bool toggle = false) : window_(window), native_(native), toggle_(toggle) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (iid != IID_IUnknown && iid != IID_IDispatch && iid != IID_IAccessible) return E_NOINTERFACE;
        *out = static_cast<IAccessible*>(this); AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&references_); }
    ULONG STDMETHODCALLTYPE Release() override {
        const auto count = InterlockedDecrement(&references_);
        if (!count) delete this;
        return count;
    }
    HRESULT STDMETHODCALLTYPE GetTypeInfoCount(UINT* p) override { return native_->GetTypeInfoCount(p); }
    HRESULT STDMETHODCALLTYPE GetTypeInfo(UINT i, LCID l, ITypeInfo** p) override { return native_->GetTypeInfo(i,l,p); }
    HRESULT STDMETHODCALLTYPE GetIDsOfNames(REFIID r, LPOLESTR* n, UINT c, LCID l, DISPID* p) override { return native_->GetIDsOfNames(r,n,c,l,p); }
    HRESULT STDMETHODCALLTYPE Invoke(DISPID id, REFIID r, LCID l, WORD flags, DISPPARAMS* args,
        VARIANT* result, EXCEPINFO* exception, UINT* error) override {
        if ((id == DISPID_ACC_STATE || id == DISPID_ACC_ROLE || id == DISPID_ACC_DEFAULTACTION) &&
            (flags & DISPATCH_PROPERTYGET) && args && args->cArgs <= 1 && result) {
            VARIANT self{}; self.vt = VT_I4; self.lVal = CHILDID_SELF;
            const auto child = args->cArgs ? args->rgvarg[0] : self;
            if (id == DISPID_ACC_STATE) return get_accState(child, result);
            if (id == DISPID_ACC_ROLE) return get_accRole(child, result);
            VariantInit(result); result->vt = VT_BSTR; return get_accDefaultAction(child, &result->bstrVal);
        }
        return native_->Invoke(id,r,l,flags,args,result,exception,error);
    }
    HRESULT STDMETHODCALLTYPE get_accParent(IDispatch** p) override { return native_->get_accParent(p); }
    HRESULT STDMETHODCALLTYPE get_accChildCount(long* p) override { return native_->get_accChildCount(p); }
    HRESULT STDMETHODCALLTYPE get_accChild(VARIANT c, IDispatch** p) override { return native_->get_accChild(c,p); }
    HRESULT STDMETHODCALLTYPE get_accName(VARIANT c, BSTR* p) override { return native_->get_accName(c,p); }
    HRESULT STDMETHODCALLTYPE get_accValue(VARIANT c, BSTR* p) override { return native_->get_accValue(c,p); }
    HRESULT STDMETHODCALLTYPE get_accDescription(VARIANT c, BSTR* p) override { return native_->get_accDescription(c,p); }
    HRESULT STDMETHODCALLTYPE get_accRole(VARIANT c, VARIANT* p) override {
        const auto result = native_->get_accRole(c,p);
        if (toggle_ && SUCCEEDED(result) && p && p->vt == VT_I4 && c.vt == VT_I4 && c.lVal == CHILDID_SELF)
            p->lVal = ROLE_SYSTEM_CHECKBUTTON;
        return result;
    }
    HRESULT STDMETHODCALLTYPE get_accState(VARIANT c, VARIANT* p) override {
        const auto result = native_->get_accState(c,p);
        if (SUCCEEDED(result) && p && p->vt == VT_I4 && c.vt == VT_I4 && c.lVal == CHILDID_SELF) {
            p->lVal &= ~STATE_SYSTEM_DEFAULT;
            if (toggle_) {
                p->lVal &= ~STATE_SYSTEM_CHECKED;
                if (IsWindow(window_) && SendMessageW(window_, BM_GETCHECK, 0, 0) == BST_CHECKED) p->lVal |= STATE_SYSTEM_CHECKED;
            } else if (IsWindow(window_) && (SendMessageW(window_, WM_GETDLGCODE, 0, 0) & DLGC_DEFPUSHBUTTON))
                p->lVal |= STATE_SYSTEM_DEFAULT;
        }
        return result;
    }
    HRESULT STDMETHODCALLTYPE get_accHelp(VARIANT c, BSTR* p) override { return native_->get_accHelp(c,p); }
    HRESULT STDMETHODCALLTYPE get_accHelpTopic(BSTR* p, VARIANT c, long* t) override { return native_->get_accHelpTopic(p,c,t); }
    HRESULT STDMETHODCALLTYPE get_accKeyboardShortcut(VARIANT c, BSTR* p) override { return native_->get_accKeyboardShortcut(c,p); }
    HRESULT STDMETHODCALLTYPE get_accFocus(VARIANT* p) override { return native_->get_accFocus(p); }
    HRESULT STDMETHODCALLTYPE get_accSelection(VARIANT* p) override { return native_->get_accSelection(p); }
    HRESULT STDMETHODCALLTYPE get_accDefaultAction(VARIANT c, BSTR* p) override {
        if (toggle_ && c.vt == VT_I4 && c.lVal == CHILDID_SELF) {
            if (!p) return E_POINTER;
            *p = SysAllocString(SendMessageW(window_, BM_GETCHECK, 0, 0) == BST_CHECKED ? L"Uncheck" : L"Check");
            return *p ? S_OK : E_OUTOFMEMORY;
        }
        return native_->get_accDefaultAction(c,p);
    }
    HRESULT STDMETHODCALLTYPE accSelect(long f, VARIANT c) override { return native_->accSelect(f,c); }
    HRESULT STDMETHODCALLTYPE accLocation(long* x,long* y,long* w,long* h,VARIANT c) override { return native_->accLocation(x,y,w,h,c); }
    HRESULT STDMETHODCALLTYPE accNavigate(long d,VARIANT c,VARIANT* p) override { return native_->accNavigate(d,c,p); }
    HRESULT STDMETHODCALLTYPE accHitTest(long x,long y,VARIANT* p) override { return native_->accHitTest(x,y,p); }
    HRESULT STDMETHODCALLTYPE accDoDefaultAction(VARIANT c) override { return native_->accDoDefaultAction(c); }
    HRESULT STDMETHODCALLTYPE put_accName(VARIANT c,BSTR p) override { return native_->put_accName(c,p); }
    HRESULT STDMETHODCALLTYPE put_accValue(VARIANT c,BSTR p) override { return native_->put_accValue(c,p); }
private:
    ~ButtonAccessible() { native_->Release(); }
    LONG references_ = 1;
    HWND window_;
    IAccessible* native_;
    bool toggle_;
};
LRESULT accessibleResult(HWND window, WPARAM w, LPARAM l, bool toggle) {
    IAccessible* native = nullptr;
    if (SUCCEEDED(CreateStdAccessibleObject(window, OBJID_CLIENT, IID_IAccessible, reinterpret_cast<void**>(&native)))) {
        auto* accessible = new (std::nothrow) ButtonAccessible(window, native, toggle);
        if (accessible) {
            const auto result = LresultFromObject(IID_IAccessible, w, accessible);
            accessible->Release(); return result;
        }
        native->Release();
    }
    return DefSubclassProc(window, WM_GETOBJECT, w, l);
}
void paintModernButton(HWND window, HDC dc, const ModernButton& button) {
    const auto& resources = *button.resources;
    PaintBuffer buffer(dc); dc = buffer.dc();
    const int saved = SaveDC(dc);
    RECT bounds{}; GetClientRect(window, &bounds);
    FillRect(dc, &bounds, resources.brush(button.background));
    const auto s = [&](int value) { return scaleForDpi(value, resources.dpi()); };
    const bool enabled = IsWindowEnabled(window) != FALSE;
    const auto state = SendMessageW(window, BM_GETSTATE, 0, 0);
    const bool pressed = (state & BST_PUSHED) != 0;
    const bool primary = button.role == ButtonRole::primary, reset = button.role == ButtonRole::reset;
    const auto fill = !enabled ? (primary ? ui::Color::buttonPrimaryDisabled : reset ?
        ui::Color::buttonResetDisabled : ui::Color::disabledBackground) : primary ?
        (pressed ? ui::Color::primaryPressed : button.hover ? ui::Color::primaryHover : ui::Color::primary) : reset ?
        (pressed ? ui::Color::buttonResetPressed : button.hover ? ui::Color::buttonResetHover : ui::Color::buttonReset) :
        (pressed ? ui::Color::buttonNeutralPressed : button.hover ? ui::Color::buttonNeutralHover : ui::Color::buttonNeutral);
    const auto textColor = !enabled ? ui::Color::disabledText : primary || reset ? ui::Color::primaryText : ui::Color::text;
    // A single contiguous contour: focus changes its thickness, default only
    // its shade. Neither state adds a detached ring or changes the outer size.
    const bool isDefault = button.isDefault;
    const bool focused = enabled && GetFocus() == window &&
        !(SendMessageW(window, WM_QUERYUISTATE, 0, 0) & UISF_HIDEFOCUS);
    const int inset = s(2);
    const auto contour = primary ? ui::Color::primaryPressed : reset ?
        ui::Color::buttonResetPressed : ui::Color::buttonNeutralFocus;
    const auto border = enabled && (isDefault || focused) ? contour :
        primary || reset ? fill : ui::Color::border;
    paintRoundedShape(dc, {inset, inset, bounds.right - inset, bounds.bottom - inset}, s(12), resources,
        focused ? border : fill, border);
    if (focused) {
        const int inner = inset + s(3);
        paintRoundedShape(dc, {inner, inner, bounds.right - inner, bounds.bottom - inner}, s(6), resources,
            fill, fill);
    }
    const int length = GetWindowTextLengthW(window);
    std::wstring text(static_cast<std::size_t>(length) + 1, L'\0');
    text.resize(GetWindowTextW(window, text.data(), length + 1));
    SelectObject(dc, resources.font(ui::Font::label));
    SetBkMode(dc, TRANSPARENT); SetTextColor(dc, resources.color(textColor));
    RECT label = bounds; InflateRect(&label, -s(6), 0);
    DrawTextW(dc, text.c_str(), static_cast<int>(text.size()), &label,
        DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    RestoreDC(dc, saved);
}
LRESULT CALLBACK modernButtonProcedure(HWND window, UINT message, WPARAM w, LPARAM l,
                                       UINT_PTR id, DWORD_PTR data) {
    auto* button = reinterpret_cast<ModernButton*>(data);
    if (message == WM_GETOBJECT && static_cast<LONG>(l) == OBJID_CLIENT) {
        IAccessible* native = nullptr;
        if (SUCCEEDED(CreateStdAccessibleObject(window, OBJID_CLIENT, IID_IAccessible,
            reinterpret_cast<void**>(&native)))) {
            auto* accessible = new (std::nothrow) ButtonAccessible(window, native);
            if (accessible) {
                const auto result = LresultFromObject(IID_IAccessible, w, accessible);
                accessible->Release(); return result;
            }
            native->Release();
        }
        return DefSubclassProc(window, message, w, l);
    }
    if (message == WM_GETDLGCODE) {
        const auto native = DefSubclassProc(window, message, w, l);
        return (native & ~(DLGC_DEFPUSHBUTTON | DLGC_UNDEFPUSHBUTTON)) |
            DLGC_BUTTON | (button->isDefault ? DLGC_DEFPUSHBUTTON : DLGC_UNDEFPUSHBUTTON);
    }
    if (message == BM_SETSTYLE) {
        // Dialog navigation promotes/demotes buttons through BM_SETSTYLE.
        // Keep that semantic state without ever restoring a system paint style.
        const auto type = w & BS_TYPEMASK;
        if (type == BS_PUSHBUTTON || type == BS_DEFPUSHBUTTON) {
            button->isDefault = type == BS_DEFPUSHBUTTON;
            NotifyWinEvent(EVENT_OBJECT_STATECHANGE, window, OBJID_CLIENT, CHILDID_SELF);
        }
        const auto result = DefSubclassProc(window, message, (w & ~BS_TYPEMASK) | BS_OWNERDRAW, l);
        InvalidateRect(window, nullptr, FALSE);
        return result;
    }
    if (message == WM_NCDESTROY) {
        RemoveWindowSubclass(window, modernButtonProcedure, id);
        delete button;
        return DefSubclassProc(window, message, w, l);
    }
    if (message == WM_PAINT) {
        PAINTSTRUCT paint{}; const auto dc = BeginPaint(window, &paint);
        paintModernButton(window, dc, *button); EndPaint(window, &paint); return 0;
    }
    if (message == WM_PRINTCLIENT) { paintModernButton(window, reinterpret_cast<HDC>(w), *button); return 0; }
    if (message == WM_ERASEBKGND) return 1;
    if (message == WM_MOUSEMOVE) {
        // Captured mouse moves also arrive with negative/out-of-client coordinates.
        RECT bounds{}; GetClientRect(window, &bounds);
        const POINT point{static_cast<short>(LOWORD(l)), static_cast<short>(HIWORD(l))};
        const bool hover = IsWindowEnabled(window) && PtInRect(&bounds, point);
        if (hover != button->hover) {
            button->hover = hover;
            if (hover) {
                TRACKMOUSEEVENT tracking{sizeof(tracking), TME_LEAVE, window, 0}; TrackMouseEvent(&tracking);
            }
            InvalidateRect(window, nullptr, FALSE);
        }
    }
    if (message == WM_MOUSELEAVE || (message == WM_ENABLE && !w)) {
        button->hover = false; InvalidateRect(window, nullptr, FALSE);
    }
    if (message == WM_CAPTURECHANGED || message == WM_CANCELMODE) {
        POINT point{}; RECT bounds{}; GetClientRect(window, &bounds);
        button->hover = IsWindowEnabled(window) && GetCursorPos(&point) && ScreenToClient(window, &point) &&
            PtInRect(&bounds, point);
    }
    const auto result = DefSubclassProc(window, message, w, l);
    switch (message) {
    case BM_SETSTATE: case BM_SETSTYLE: case WM_STYLECHANGED: case WM_ENABLE:
    case WM_SETFOCUS: case WM_KILLFOCUS: case WM_UPDATEUISTATE: case WM_SETTEXT:
    case WM_SETFONT: case WM_THEMECHANGED: case WM_SIZE: case WM_CAPTURECHANGED:
    case WM_CANCELMODE: case WM_MOUSEMOVE: case WM_MOUSELEAVE: case WM_LBUTTONDOWN: case WM_LBUTTONUP:
        if (IsWindow(window)) InvalidateRect(window, nullptr, FALSE); break;
    default: break;
    }
    return result;
}
// Native BUTTON can draw synchronously during state changes, outside WM_PAINT.
// Intercept that path too, including row buttons whose parent is the table pane.
LRESULT CALLBACK modernButtonParentProcedure(HWND window, UINT message, WPARAM w, LPARAM l,
                                             UINT_PTR id, DWORD_PTR) {
    if (message == WM_DRAWITEM && l) {
        const auto* draw = reinterpret_cast<const DRAWITEMSTRUCT*>(l);
        DWORD_PTR data = 0;
        if (draw->CtlType == ODT_BUTTON && GetWindowSubclass(draw->hwndItem,
            modernButtonProcedure, modernButtonId, &data)) {
            // Native owner-draw state notifications never invoke the system
            // renderer. Repaint the entire client with an unshifted paint DC.
            InvalidateRect(draw->hwndItem, nullptr, FALSE);
            UpdateWindow(draw->hwndItem);
            return TRUE;
        }
    }
    if (message == WM_NOTIFY && l) {
        const auto* draw = reinterpret_cast<const NMCUSTOMDRAW*>(l);
        DWORD_PTR data = 0;
        if (draw->hdr.code == NM_CUSTOMDRAW && GetWindowSubclass(draw->hdr.hwndFrom,
            modernButtonProcedure, modernButtonId, &data)) {
            if (draw->dwDrawStage == CDDS_PREPAINT) {
                // The native pressed-state DC can have a shifted origin and a
                // partial clip. Never paint our client geometry through that DC.
                // WM_PAINT owns the complete skin, including the corner margins.
                InvalidateRect(draw->hdr.hwndFrom, nullptr, FALSE);
                return CDRF_SKIPDEFAULT;
            }
            return CDRF_DODEFAULT;
        }
    }
    if (message == WM_NCDESTROY) RemoveWindowSubclass(window, modernButtonParentProcedure, id);
    return DefSubclassProc(window, message, w, l);
}
}
PaintBuffer::PaintBuffer(HDC target) : target_(target) {
    if (GetClipBox(target, &bounds_) == ERROR) return;
    const int width = bounds_.right - bounds_.left, height = bounds_.bottom - bounds_.top;
    if (width <= 0 || height <= 0) return;
    memory_ = CreateCompatibleDC(target);
    bitmap_ = CreateCompatibleBitmap(target, width, height);
    if (!memory_ || !bitmap_) { finish(); return; }
    previous_ = SelectObject(memory_, bitmap_);
    if (!previous_ || previous_ == HGDI_ERROR) { previous_ = nullptr; finish(); return; }
    SetViewportOrgEx(memory_, -bounds_.left, -bounds_.top, nullptr);
    IntersectClipRect(memory_, bounds_.left, bounds_.top, bounds_.right, bounds_.bottom);
    if (!BitBlt(memory_, bounds_.left, bounds_.top, width, height, target, bounds_.left, bounds_.top, SRCCOPY)) {
        SelectObject(memory_, previous_); previous_ = nullptr; finish(); return;
    }
    SelectObject(memory_, GetCurrentObject(target, OBJ_FONT));
    SetTextColor(memory_, GetTextColor(target));
    SetBkColor(memory_, GetBkColor(target));
    SetBkMode(memory_, GetBkMode(target));
}
PaintBuffer::~PaintBuffer() { finish(); }
void PaintBuffer::finish() {
    if (memory_ && previous_) {
        BitBlt(target_, bounds_.left, bounds_.top, bounds_.right - bounds_.left,
               bounds_.bottom - bounds_.top, memory_, bounds_.left, bounds_.top, SRCCOPY);
        SelectObject(memory_, previous_);
    }
    if (bitmap_) DeleteObject(bitmap_);
    if (memory_) DeleteDC(memory_);
    bitmap_ = nullptr; memory_ = nullptr; previous_ = nullptr;
}
void paintRoundedShape(HDC dc, Rect rect, int diameter, const UiResources& resources,
                       ui::Color fill, ui::Color border, bool ellipse) {
    if (smoothShape(dc, rect, diameter, resources, fill, border, ellipse)) return;
    const auto brush = SelectObject(dc, resources.brush(fill));
    const auto pen = SelectObject(dc, resources.pen(border));
    if (ellipse) Ellipse(dc, rect.left, rect.top, rect.right, rect.bottom);
    else RoundRect(dc, rect.left, rect.top, rect.right, rect.bottom, diameter, diameter);
    SelectObject(dc, pen); SelectObject(dc, brush);
}
bool attachToggleSwitch(HWND window, const UiResources& resources) {
    if (!window || (GetWindowLongPtrW(window, GWL_STYLE) & BS_TYPEMASK) != BS_OWNERDRAW) return false;
    DWORD_PTR existing = 0;
    if (GetWindowSubclass(window, toggleProcedure, 0, &existing)) return true;
    auto* toggle = new (std::nothrow) ToggleSwitch{&resources};
    if (!toggle) return false;
    if (!SetWindowSubclass(GetParent(window), toggleParentProcedure, 0, 0) ||
        !SetWindowSubclass(window, toggleProcedure, 0, reinterpret_cast<DWORD_PTR>(toggle))) {
        delete toggle; return false;
    }
    InvalidateRect(window, nullptr, FALSE); return true;
}
bool attachModernButton(HWND window, const UiResources& resources, ButtonRole role, ui::Color background) {
    if (!window || (GetWindowLongPtrW(window, GWL_STYLE) & BS_TYPEMASK) != BS_OWNERDRAW) return false;
    DWORD_PTR existing = 0;
    if (GetWindowSubclass(window, modernButtonProcedure, modernButtonId, &existing)) return true;
    auto* button = new (std::nothrow) ModernButton{&resources, role, background, false, role == ButtonRole::primary};
    if (!button) return false;
    if (!SetWindowSubclass(GetParent(window), modernButtonParentProcedure, modernButtonId, 0) ||
        !SetWindowSubclass(window, modernButtonProcedure, modernButtonId, reinterpret_cast<DWORD_PTR>(button))) {
        delete button; return false;
    }
    InvalidateRect(window, nullptr, FALSE);
    return true;
}
thread_local LayoutFrame* LayoutFrame::active_ = nullptr;
bool attachModernTextBox(HWND window, const UiResources& resources, ui::Color background) {
    if (!window) return false;
    const auto style = GetWindowLongPtrW(window, GWL_STYLE);
    wchar_t name[16]{}; GetClassNameW(window, name, 16);
    if (std::wstring_view(name) != L"Edit" && std::wstring_view(name) != L"EDIT") return false;
    if ((style & (WS_VISIBLE | WS_BORDER | ES_MULTILINE | ES_READONLY)) ||
        (GetWindowLongPtrW(window, GWL_EXSTYLE) & WS_EX_CLIENTEDGE)) return false;
    auto* box = new (std::nothrow) ModernTextBox{&resources, background};
    if (!box) return false;
    if (!SetWindowSubclass(GetParent(window), textBoxParentProcedure, textBoxId, 0) ||
        !SetWindowSubclass(window, textBoxProcedure, textBoxId, reinterpret_cast<DWORD_PTR>(box))) {
        delete box; return false;
    }
    textBoxFrame(window);
    return true;
}
void setTextBoxInvalid(HWND window, bool invalid) {
    DWORD_PTR data = 0;
    if (!window || !GetWindowSubclass(window, textBoxProcedure, textBoxId, &data)) return;
    auto& box = *reinterpret_cast<ModernTextBox*>(data);
    if (box.invalid == invalid) return;
    box.invalid = invalid; repaintTextBox(window);
}
LayoutFrame::LayoutFrame(HWND window) : root_(GetAncestor(window, GA_ROOT)), previous_(active_) {
    for (auto* frame = previous_; frame; frame = frame->previous_)
        if (frame->root_ == root_) outer_ = false;
    active_ = this;
    // WS_EX_COMPOSITED presents the descendant tree when its last DC is
    // released. Keep this lease only for the synchronous layout, never across
    // a message loop or timer. Do not use WM_SETREDRAW: it changes visibility
    // and would break responsive layout decisions and native focus handling.
    if (outer_ && IsWindowVisible(root_)) dc_ = GetDC(root_);
}
LayoutFrame::~LayoutFrame() {
    active_ = previous_;
    if (outer_ && IsWindow(root_))
        RedrawWindow(root_, nullptr, nullptr,
            RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN | RDW_UPDATENOW);
    // Native non-client/child painting can invalidate the parent again (for
    // example when a scrollbar disappears). Drain that paint before releasing
    // the composed frame. UpdateWindow is a no-op when nothing remains dirty.
    if (outer_ && IsWindow(root_)) UpdateWindow(root_);
    if (dc_) ReleaseDC(root_, dc_);

}
bool positionControls(HWND parent, std::span<const ControlPlacement> children, const WindowPositionApi& api) {
    LayoutFrame frame(parent);
    if (children.empty()) return true;
    if (children.size() > static_cast<std::size_t>((std::numeric_limits<int>::max)())) return false;
    constexpr UINT flags = SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOREDRAW | SWP_NOCOPYBITS;
    HDWP batch = api.begin(static_cast<int>(children.size()));
    for (const auto& child : children) {
        if (batch && child.window) batch = api.defer(batch, child.window, nullptr, child.rect.left, child.rect.top,
            child.rect.right - child.rect.left, child.rect.bottom - child.rect.top, flags);
    }
    if (batch && api.end(batch)) return true;
    bool moved = true;
    for (const auto& child : children) if (child.window) {
        if (!api.set(child.window, nullptr, child.rect.left, child.rect.top,
            child.rect.right - child.rect.left, child.rect.bottom - child.rect.top, flags)) moved = false;
    }
    return moved;
}
Size measureText(HDC dc, HFONT font, std::wstring_view text, int availableWidth, bool wrap) {
    if (!dc || !font || text.size() > static_cast<std::size_t>((std::numeric_limits<int>::max)())) return {};
    const auto old = SelectObject(dc, font);
    RECT bounds{0, 0, std::max(1, availableWidth), 0};
    DrawTextW(dc, text.data(), static_cast<int>(text.size()), &bounds,
              DT_CALCRECT | DT_NOPREFIX | (wrap ? DT_WORDBREAK : DT_SINGLELINE));
    SelectObject(dc, old);
    return {bounds.right - bounds.left, bounds.bottom - bounds.top};
}
int measuredButtonWidth(HDC dc, HFONT font, std::wstring_view text, int dpi) {
    return std::max(scaleForDpi(ui::buttonMinWidth, dpi),
        measureText(dc, font, text, 1, false).width + 2 * scaleForDpi(ui::buttonPadding, dpi));
}
void paintCard(HDC dc, Rect rect, const UiResources& resources, ui::Color fill, ui::Color border) {
    const int diameter = scaleForDpi(2 * ui::cardRadius, resources.dpi());
    paintRoundedShape(dc, rect, diameter, resources, fill, border);
}
void invalidateChanged(HWND window, Rect region, bool changed) {
    if (!changed || region.right <= region.left || region.bottom <= region.top) return;
    const RECT native{region.left, region.top, region.right, region.bottom};
    InvalidateRect(window, &native, FALSE);
}
} // namespace phpvpn::tray
