#pragma once
#include <span>
#include <string_view>
#include "ui_resources.h"

namespace phpvpn::tray {
struct ControlPlacement { HWND window; Rect rect; };
// Optional injected native calls permit deterministic failure-path tests.
struct WindowPositionApi {
    decltype(&BeginDeferWindowPos) begin = BeginDeferWindowPos;
    decltype(&DeferWindowPos) defer = DeferWindowPos;
    decltype(&EndDeferWindowPos) end = EndDeferWindowPos;
    decltype(&SetWindowPos) set = SetWindowPos;
};
// False means the individual fallback also failed. Every child is retried when
// any stage of the deferred batch fails. No recreation, focus or text changes.
bool positionControls(HWND parent, std::span<const ControlPlacement> children,
                      const WindowPositionApi& api = {});
Size measureText(HDC dc, HFONT font, std::wstring_view text, int availableWidth, bool wrap);
int measuredButtonWidth(HDC dc, HFONT font, std::wstring_view text, int dpi);
void paintCard(HDC dc, Rect rect, const UiResources& resources,
               ui::Color fill = ui::Color::surface, ui::Color border = ui::Color::border);
void invalidateChanged(HWND window, Rect region, bool changed);
} // namespace phpvpn::tray
