#pragma once
#include "layout.h"
#include <array>
namespace phpvpn::tray {
struct WizardLayout {
    Rect steps, content;
    // Cancel, Next/Finish, Back, Reset in visual order from the right.
    std::array<Rect, 4> buttons;
};
WizardLayout computeWizardLayout(Size client, int dpi);
} // namespace phpvpn::tray
