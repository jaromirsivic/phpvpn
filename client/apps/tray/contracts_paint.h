#pragma once
#include "ui_controls.h"
#include <commctrl.h>
namespace phpvpn::tray {
inline LRESULT paintContractsPrimary(const NMCUSTOMDRAW& paint, const UiResources& resources) {
    if (paint.dwDrawStage != CDDS_PREPAINT) return CDRF_DODEFAULT;
    const bool enabled = IsWindowEnabled(paint.hdr.hwndFrom) != FALSE;
    FillRect(paint.hdc, &paint.rc, resources.brush(enabled ? ui::Color::primary : ui::Color::disabledBackground));
    SetBkMode(paint.hdc, TRANSPARENT);
    SetTextColor(paint.hdc, resources.color(enabled ? ui::Color::primaryText : ui::Color::disabledText));
    const auto old = SelectObject(paint.hdc, resources.font(ui::Font::label));
    auto rect = paint.rc; DrawTextW(paint.hdc,L"OK",2,&rect,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
    SelectObject(paint.hdc,old);
    if (paint.uItemState & CDIS_FOCUS) { InflateRect(&rect,-3,-3); DrawFocusRect(paint.hdc,&rect); }
    return CDRF_SKIPDEFAULT;
}
}
