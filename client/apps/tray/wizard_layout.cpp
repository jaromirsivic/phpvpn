#include "wizard_layout.h"
#include <algorithm>
namespace phpvpn::tray {
WizardLayout computeWizardLayout(Size client, int dpi) {
    const int width = std::max(1, client.width), height = std::max(1, client.height);
    const int margin = scaleForDpi(16, dpi), footer = std::min(height, scaleForDpi(62, dpi));
    const int left = std::min(scaleForDpi(230, dpi), width / 3);
    WizardLayout result;
    result.steps = {margin, margin, std::max(margin + 1, left - margin), std::max(margin + 1, height - footer - margin)};
    result.content = {left, 0, width, std::max(1, height - footer)};
    const int gap = scaleForDpi(8, dpi), buttonHeight = std::min(footer, scaleForDpi(34, dpi));
    const int buttonWidth = std::min(scaleForDpi(112, dpi), std::max(1, (width - 2 * margin - 3 * gap) / 4));
    int x = width - margin - buttonWidth;
    const int y = std::max(0, height - footer + (footer - buttonHeight) / 2);
    for (auto& button : result.buttons) {
        button = {x, y, x + buttonWidth, y + buttonHeight}; x -= buttonWidth + gap;
    }
    return result;
}
} // namespace phpvpn::tray
