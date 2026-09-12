#include "wizard_layout.h"
#include <algorithm>
#include <array>
namespace phpvpn::tray {
WizardLayout computeWizardLayout(Size client, int dpi, int primaryWidth) {
    const int width = std::max(1, client.width), height = std::max(1, client.height);
    primaryWidth = std::max(scaleForDpi(200, dpi), primaryWidth);
    const std::array specs{
        FooterButtonSpec{scaleForDpi(88, dpi), FooterSide::leading, true},
        FooterButtonSpec{scaleForDpi(88, dpi), FooterSide::trailing, true},
        FooterButtonSpec{primaryWidth, FooterSide::trailing, true},
    };
    const auto footer = computeButtonFooterLayout({width, height}, dpi, specs);
    const int margin = footer.margin;
    WizardLayout result;
    result.compact = width < scaleForDpi(760, dpi);
    const int left = result.compact ? 0 : scaleForDpi(230, dpi);
    const int top = result.compact ? std::min(scaleForDpi(64, dpi), std::max(1, footer.bounds.top)) : 0;
    result.steps = {margin, margin, result.compact ? width - margin : left - margin,
        result.compact ? top - margin : std::max(margin + 1, footer.bounds.top - margin)};
    result.content = {left, top, width, std::max(top + 1, footer.bounds.top)};
    std::copy(footer.buttons.begin(), footer.buttons.end(), result.buttons.begin());
    return result;
}
} // namespace phpvpn::tray
