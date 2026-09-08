#include "ui_controls.h"
#include <algorithm>
#include <limits>
#include <commctrl.h>
#include <string>
#include <objidl.h>
#include <gdiplus.h>
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
                 ui::Color fill, ui::Color border, bool ellipse) {
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
    const float stroke = static_cast<float>(std::max(1, scaleForDpi(1, resources.dpi())));
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
LRESULT CALLBACK toggleProcedure(HWND window, UINT message, WPARAM w, LPARAM l,
                                 UINT_PTR id, DWORD_PTR data) {
    const auto& resources = *reinterpret_cast<const UiResources*>(data);
    if (message == WM_PAINT) {
        PAINTSTRUCT paint{}; const auto dc = BeginPaint(window, &paint);
        paintToggle(window, dc, resources); EndPaint(window, &paint); return 0;
    }
    if (message == WM_PRINTCLIENT) { paintToggle(window, reinterpret_cast<HDC>(w), resources); return 0; }
    if (message == WM_ERASEBKGND) return 1;
    if (message == WM_NCDESTROY) RemoveWindowSubclass(window, toggleProcedure, id);
    const auto result = DefSubclassProc(window, message, w, l);
    switch (message) {
    case BM_SETCHECK: case BM_SETSTATE: case WM_ENABLE: case WM_SETFOCUS: case WM_KILLFOCUS:
    case WM_UPDATEUISTATE: case WM_SETTEXT: case WM_SETFONT: case WM_THEMECHANGED: case WM_SIZE:
        InvalidateRect(window, nullptr, FALSE); break;
    default: break;
    }
    return result;
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
    return window && SetWindowSubclass(window, toggleProcedure, 0,
        reinterpret_cast<DWORD_PTR>(&resources));
}
bool positionControls(HWND parent, std::span<const ControlPlacement> children, const WindowPositionApi& api) {
    if (children.empty()) return true;
    if (children.size() > static_cast<std::size_t>((std::numeric_limits<int>::max)())) return false;
    constexpr UINT flags = SWP_NOZORDER | SWP_NOACTIVATE;
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
    InvalidateRect(parent, nullptr, TRUE);
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
