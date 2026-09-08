#pragma once
#include <span>
#include <string_view>
#include "ui_resources.h"

namespace phpvpn::tray {
// Pixel-coordinate back buffer. Allocation failure leaves the original DC usable.
// Finish before EndPaint. Only the clipped dirty area is allocated, even for a
// very tall scrolled Contracts child. ClearType is drawn at final resolution.
class PaintBuffer {
public:
    explicit PaintBuffer(HDC target);
    ~PaintBuffer();
    PaintBuffer(const PaintBuffer&) = delete;
    PaintBuffer& operator=(const PaintBuffer&) = delete;
    HDC dc() const { return memory_ ? memory_ : target_; }
    void finish();
private:
    HDC target_ = nullptr, memory_ = nullptr;
    HBITMAP bitmap_ = nullptr;
    HGDIOBJ previous_ = nullptr;
    RECT bounds_{};
};
// GDI+ coverage antialiasing for curves, GDI ClearType for text and integer
// FillRect/DrawFocusRect for sharp axial lines. No scaling of raster assets.
void paintRoundedShape(HDC dc, Rect rect, int diameter, const UiResources& resources,
                       ui::Color fill, ui::Color border, bool ellipse = false);
struct ControlPlacement { HWND window; Rect rect; };
// One completed layout frame for a composited top-level window, including all
// descendants. Nested layout/scroll calls publish only at the outer boundary.
class LayoutFrame {
public:
    explicit LayoutFrame(HWND window);
    ~LayoutFrame();
    LayoutFrame(const LayoutFrame&) = delete;
    LayoutFrame& operator=(const LayoutFrame&) = delete;
private:
    HWND root_ = nullptr;
    HDC dc_ = nullptr;
    LayoutFrame* previous_ = nullptr;
    bool outer_ = true;
    static thread_local LayoutFrame* active_;
};
// Optional injected native calls permit deterministic failure-path tests.
struct WindowPositionApi {
    decltype(&BeginDeferWindowPos) begin = BeginDeferWindowPos;
    decltype(&DeferWindowPos) defer = DeferWindowPos;
    decltype(&EndDeferWindowPos) end = EndDeferWindowPos;
    decltype(&SetWindowPos) set = SetWindowPos;
};
// False means the individual fallback also failed. Every child is retried when
// any stage of the deferred batch fails. No recreation, focus or text changes.
// Suppresses intermediate painting/copying. LayoutFrame publishes the result.
bool positionControls(HWND parent, std::span<const ControlPlacement> children,
                      const WindowPositionApi& api = {});
Size measureText(HDC dc, HFONT font, std::wstring_view text, int availableWidth, bool wrap);
int measuredButtonWidth(HDC dc, HFONT font, std::wstring_view text, int dpi);
void paintCard(HDC dc, Rect rect, const UiResources& resources,
               ui::Color fill = ui::Color::surface, ui::Color border = ui::Color::border);
void invalidateChanged(HWND window, Rect region, bool changed);
// Create BUTTON with BS_OWNERDRAW. Native BUTTON qualifies activation, while
// the shared adapter maintains two-state BM_GETCHECK/BM_SETCHECK semantics and
// exposes the checkbox role/state through the standard accessibility provider.
// Resources must outlive the control, as they do for the containing window.
bool attachToggleSwitch(HWND window, const UiResources& resources);
enum class ButtonRole { neutral, primary, reset };
// Only the presentation is replaced. BUTTON keeps its native input, dialog
// input and accessibility provider. Create with BS_OWNERDRAW before attaching,
// while the containing window is hidden. Primary starts as the default action.
// Dialog promotion/demotion is retained without replacing the owner-draw style.
bool attachModernButton(HWND window, const UiResources& resources, ButtonRole role = ButtonRole::neutral,
                        ui::Color background = ui::Color::background);
} // namespace phpvpn::tray
