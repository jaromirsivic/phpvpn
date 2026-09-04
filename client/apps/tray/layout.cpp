#include "layout.h"
#include <algorithm>
#include <cstdint>
#include <limits>

namespace phpvpn::tray {
int scaleForDpi(int pixels, int dpi) {
    const auto scaled = (static_cast<std::int64_t>(pixels) * std::max(dpi, 1) + 48) / 96;
    return static_cast<int>(std::clamp<std::int64_t>(scaled, 0, std::numeric_limits<int>::max()));
}
Size defaultEditorClientSize(int dpi) { return {scaleForDpi(1080, dpi), scaleForDpi(560, dpi)}; }
Size clampToWorkArea(Size desired, Rect workArea, int /*dpi*/) {
    return {std::clamp(desired.width, 0, std::max(0, workArea.right - workArea.left)),
            std::clamp(desired.height, 0, std::max(0, workArea.bottom - workArea.top))};
}
Rect centerInWorkArea(Size size, Rect workArea) {
    size = clampToWorkArea(size, workArea, 96);
    const int x = workArea.left + (workArea.right - workArea.left - size.width) / 2;
    const int y = workArea.top + (workArea.bottom - workArea.top - size.height) / 2;
    return {x, y, x + size.width, y + size.height};
}
EditorLayout computeEditorLayout(Size client, int dpi, bool editable) {
    const int width = std::max(0, client.width);
    const int height = std::max(0, client.height);
    const int margin = std::min({scaleForDpi(12, dpi), width / 8, height / 8});
    const int gap = std::min(scaleForDpi(8, dpi), width / 8);
    const int buttonWidth = std::min(scaleForDpi(92, dpi), std::max(0, (width - 2 * margin - gap) / 2));
    const int buttonHeight = std::min(scaleForDpi(32, dpi), std::max(0, height - 2 * margin));
    EditorLayout layout;
    int textBottom = height - margin;
    if (editable) {
        layout.cancelButton = {width - margin - buttonWidth, height - margin - buttonHeight,
                               width - margin, height - margin};
        layout.saveButton = {layout.cancelButton.left - gap - buttonWidth, layout.cancelButton.top,
                             layout.cancelButton.left - gap, layout.cancelButton.bottom};
        textBottom = std::max(margin, layout.cancelButton.top - gap);
    }
    layout.textArea = {margin, margin, std::max(margin, width - margin), std::max(margin, textBottom)};
    return layout;
}
Rect exposedParentRegion(Size /*oldClient*/, Size newClient,
                         const EditorLayout& /*oldLayout*/, const EditorLayout& /*newLayout*/) {
    // Conservative bounding rectangle of every exposed parent pixel. Together
    // with WS_CLIPCHILDREN this repaints all gaps without painting over controls.
    return {0, 0, std::max(0, newClient.width), std::max(0, newClient.height)};
}
}  // namespace phpvpn::tray
