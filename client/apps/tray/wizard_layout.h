#pragma once
#include "layout.h"
#include <array>
namespace phpvpn::tray {
struct WizardLayout {
    Rect steps, content;
    bool compact = false;
    // Reset, Back, Error detail, primary, in reading and keyboard order.
    std::array<Rect, 4> buttons;
};
WizardLayout computeWizardLayout(Size client, int dpi, int primaryWidth = 0, bool detailVisible = false, int detailWidth = 0);
} // namespace phpvpn::tray
