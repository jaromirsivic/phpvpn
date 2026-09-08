#include "ui_controls.h"
#include <algorithm>
#include <limits>

namespace phpvpn::tray {
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
