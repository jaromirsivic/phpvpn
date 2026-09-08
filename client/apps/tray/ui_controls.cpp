#include "ui_controls.h"
#include <algorithm>
#include <limits>
#include <commctrl.h>
#include <string>

namespace phpvpn::tray {
namespace {
void paintToggle(HWND window, HDC dc, const UiResources& resources) {
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
    SelectObject(dc, resources.brush(fill)); SelectObject(dc, resources.pen(fill));
    RoundRect(dc, x, y, x + width, y + height, height, height);
    const auto thumbColor = enabled ? ui::Color::primaryText : ui::Color::disabledBackground;
    SelectObject(dc, resources.brush(thumbColor)); SelectObject(dc, resources.pen(thumbColor));
    const int thumb = x + (checked ? width - height : 0);
    Ellipse(dc, thumb + inset, y + inset, thumb + height - inset, y + height - inset);
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
    const auto oldBrush = SelectObject(dc, resources.brush(fill));
    const auto oldPen = SelectObject(dc, resources.pen(border));
    const int diameter = scaleForDpi(2 * ui::cardRadius, resources.dpi());
    RoundRect(dc, rect.left, rect.top, rect.right, rect.bottom, diameter, diameter);
    SelectObject(dc, oldPen);
    SelectObject(dc, oldBrush);
}
void invalidateChanged(HWND window, Rect region, bool changed) {
    if (!changed || region.right <= region.left || region.bottom <= region.top) return;
    const RECT native{region.left, region.top, region.right, region.bottom};
    InvalidateRect(window, &native, FALSE);
}
} // namespace phpvpn::tray
