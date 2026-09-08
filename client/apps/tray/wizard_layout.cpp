#include "wizard_layout.h"
#include <algorithm>
namespace phpvpn::tray {
WizardLayout computeWizardLayout(Size client, int dpi, int primaryWidth) {
    const int width = std::max(1, client.width), height = std::max(1, client.height);
    const int margin = std::min(width / 8, scaleForDpi(16, dpi)), gap = scaleForDpi(8, dpi);
    const int buttonHeight = scaleForDpi(36, dpi), small = scaleForDpi(88, dpi);
    primaryWidth = std::max(scaleForDpi(200, dpi), primaryWidth);
    const bool wrap = width < 2 * margin + 2 * small + primaryWidth + 2 * gap;
    const int footer = std::min(height, 2 * margin + buttonHeight * (wrap ? 2 : 1) + (wrap ? gap : 0));
    WizardLayout result;
    result.compact = width < scaleForDpi(760, dpi);
    const int left = result.compact ? 0 : scaleForDpi(230, dpi);
    const int top = result.compact ? std::min(scaleForDpi(64, dpi), std::max(1, height - footer)) : 0;
    result.steps = {margin, margin, result.compact ? width - margin : left - margin,
        result.compact ? top - margin : std::max(margin + 1, height - footer - margin)};
    result.content = {left, top, width, std::max(top + 1, height - footer)};
    const int y = std::max(0, height - footer + margin);
    const int available = std::max(1, width - 2 * margin);
    if (wrap) {
        const int resetWidth = std::min(small, available);
        result.buttons[0] = {margin, y, margin + resetWidth, y + buttonHeight};
        const int primary = std::min(primaryWidth, std::max(1, available - gap - 1));
        const int back = std::min(small, std::max(1, available - gap - primary));
        const int bottom = y + buttonHeight + gap;
        const int primaryLeft = width - margin - primary;
        result.buttons[1] = {primaryLeft - gap - back, bottom, primaryLeft - gap, bottom + buttonHeight};
        result.buttons[2] = {primaryLeft, bottom, width - margin, bottom + buttonHeight};
    } else {
        result.buttons[0] = {margin, y, margin + small, y + buttonHeight};
        const int primaryLeft = width - margin - primaryWidth;
        result.buttons[1] = {primaryLeft - gap - small, y, primaryLeft - gap, y + buttonHeight};
        result.buttons[2] = {primaryLeft, y, width - margin, y + buttonHeight};
    }
    return result;
}
} // namespace phpvpn::tray
