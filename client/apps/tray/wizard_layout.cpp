#include "wizard_layout.h"
#include <algorithm>
namespace phpvpn::tray {
WizardLayout computeWizardLayout(Size client, int dpi, int primaryWidth) {
    const int width = std::max(1, client.width), height = std::max(1, client.height);
    const int margin = std::min(width / 8, scaleForDpi(16, dpi)), gap = scaleForDpi(8, dpi);
    const int buttonHeight = scaleForDpi(36, dpi), small = scaleForDpi(88, dpi);
    primaryWidth = std::max(scaleForDpi(200, dpi), primaryWidth);
    const bool wrap = width < 2 * margin + 3 * small + primaryWidth + 3 * gap;
    const int footer = std::min(height, 2 * margin + buttonHeight * (wrap ? 2 : 1) + (wrap ? gap : 0));
    WizardLayout result;
    result.compact = width < scaleForDpi(760, dpi);
    const int left = result.compact ? 0 : scaleForDpi(230, dpi);
    const int top = result.compact ? std::min(scaleForDpi(96, dpi), std::max(1, height - footer)) : 0;
    result.steps = {margin, margin, result.compact ? width - margin : left - margin,
        result.compact ? top - margin : std::max(margin + 1, height - footer - margin)};
    result.content = {left, top, width, std::max(top + 1, height - footer)};
    const int y = std::max(0, height - footer + margin);
    const int available = std::max(1, width - 2 * margin);
    if (wrap) {
        const int secondary = std::min(small, std::max(1, (available - 2 * gap) / 3));
        result.buttons[0] = {margin, y, margin + secondary, y + buttonHeight};
        int x = width - margin - 2 * secondary - gap;
        result.buttons[1] = {x, y, x + secondary, y + buttonHeight};
        x += secondary + gap;
        result.buttons[2] = {x, y, x + secondary, y + buttonHeight};
        const int w = std::min(primaryWidth, available), bottom = y + buttonHeight + gap;
        result.buttons[3] = {width - margin - w, bottom, width - margin, bottom + buttonHeight};
    } else {
        result.buttons[0] = {margin, y, margin + small, y + buttonHeight};
        int x = width - margin - primaryWidth - 2 * small - 2 * gap;
        for (std::size_t i = 1; i < 4; ++i) {
            const int w = i == 3 ? primaryWidth : small;
            result.buttons[i] = {x, y, x + w, y + buttonHeight}; x += w + gap;
        }
    }
    return result;
}
} // namespace phpvpn::tray
