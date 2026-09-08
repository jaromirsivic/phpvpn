#pragma once
#include "layout.h"
#include <array>
namespace phpvpn::tray {
struct WizardLayout {
    Rect steps, content;
    bool compact = false;
    // Reset, Back, Cancel, primary, in reading and keyboard order.
    std::array<Rect, 4> buttons;
};
WizardLayout computeWizardLayout(Size client, int dpi, int primaryWidth = 0);
} // namespace phpvpn::tray
