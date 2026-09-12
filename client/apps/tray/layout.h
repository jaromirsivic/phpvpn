#pragma once
#include <cstdint>
#include <optional>
#include <span>
#include <vector>
namespace phpvpn::tray {
struct Size { int width = 0; int height = 0; bool operator==(const Size&) const = default; };
struct Rect { int left = 0; int top = 0; int right = 0; int bottom = 0; bool operator==(const Rect&) const = default; };
struct Point { int x = 0; int y = 0; bool operator==(const Point&) const = default; };
enum class FooterSide { leading, trailing };
struct FooterButtonSpec {
    int preferredWidth = 0;
    FooterSide side = FooterSide::trailing;
    bool visible = true;
};
struct ButtonFooterLayout {
    Rect bounds;
    std::vector<Rect> buttons;
    int margin = 0;
    int gap = 0;
    int buttonHeight = 0;
    int rows = 0;
};
// Native adapter supplies monitor identity, full bounds, work area and target DPI.
struct MonitorGeometry {
    std::uintptr_t id = 0;
    Rect bounds, work;
    int dpi = 96;
    bool primary = false;
};
struct MonitorPreference {
    std::optional<std::uintptr_t> parent;
    std::optional<Point> menuPoint;
    std::optional<std::uintptr_t> foreground;
    std::optional<Point> cursor;
};
std::optional<std::size_t> selectMonitor(std::span<const MonitorGeometry> monitors,
                                       const MonitorPreference& preference);
// Preserve the exact rectangle if contained in any work area. Otherwise use
// the nearest full monitor and the smallest translation/shrink that fits.
Rect usableWindowRect(Rect current, std::span<const MonitorGeometry> monitors);
Rect clampWindowRect(Rect current, Rect work);
// Frame insets are physical pixels returned by the native DPI-aware adapter.
Rect initialWindowRect(Size clientDip, const MonitorGeometry& monitor, Size frame);
struct ContentLayout { Rect header, viewport, footer; int scrollMax = 0; };
ContentLayout computeContentLayout(Size client, int headerHeight, int footerHeight, int contentHeight);
int revealScrollPosition(int position, int viewportHeight, int contentHeight, Rect target);
struct EditorLayout { Rect textArea; Rect saveButton; Rect resetButton; Rect cancelButton; Rect heading, path, tabs, footer; };
int scaleForDpi(int pixels, int dpi);
int buttonFooterMargin(int width, int dpi);
ButtonFooterLayout computeButtonFooterLayout(
    Size client, int dpi, std::span<const FooterButtonSpec> buttons);
Size defaultEditorClientSize(int dpi);
Size clampToWorkArea(Size desired, Rect workArea, int dpi);
Rect centerInWorkArea(Size size, Rect workArea);
EditorLayout computeEditorLayout(Size client, int dpi, bool editable);
Rect exposedParentRegion(Size oldClient, Size newClient,
                         const EditorLayout& oldLayout, const EditorLayout& newLayout);
}  // namespace phpvpn::tray
