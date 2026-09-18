#include "harness.h"
#include <windows.h>
#include <algorithm>

// Compile the production cache with process-local read-only OS inputs. Never
// change the user's High Contrast setting or system palette for a unit test.
// Rename the class so the ordinary production instance remains linked unchanged.
#define UiResources PaletteTestResources
#include "../../apps/tray/ui_resources.h"
namespace {
bool testHighContrast = false;
COLORREF testWindowColor = RGB(10, 20, 30);
BOOL paletteSystemParametersInfo(UINT action, UINT, PVOID value, UINT) {
    if (action != SPI_GETHIGHCONTRAST) return FALSE;
    static_cast<HIGHCONTRASTW*>(value)->dwFlags = testHighContrast ? HCF_HIGHCONTRASTON : 0;
    return TRUE;
}
COLORREF paletteGetSysColor(int role) {
    return role == COLOR_WINDOW ? testWindowColor : RGB(240, 230, 220);
}
}
#define SystemParametersInfoW paletteSystemParametersInfo
#define GetSysColor paletteGetSysColor
#include "../../apps/tray/ui_resources.cpp"
#undef GetSysColor
#undef SystemParametersInfoW
#undef UiResources

PHPVPN_TEST(TrayResources, HighContrastAndSystemPaletteKeepFontsAndIconsAtSameDpi) {
    using namespace phpvpn::tray;
    PaletteTestResources resources;
    testHighContrast = false;
    testWindowColor = RGB(10, 20, 30);
    const auto instance = GetModuleHandleW(nullptr);
    PHPVPN_CHECK(resources.update(instance, 96));
    const auto font = resources.font(ui::Font::label);
    const auto icon = resources.icon(0);
    const auto normalColor = resources.color(ui::Color::surface);
    PHPVPN_CHECK_EQ(resources.color(ui::Color::serverInfoValid), RGB(221,242,226));
    PHPVPN_CHECK_EQ(resources.color(ui::Color::successBackground), RGB(240,250,242));
    PHPVPN_CHECK_EQ(resources.color(ui::Color::contractsChanged), RGB(255,242,179));
    auto previousPen = resources.pen(ui::Color::surface);
    for (int transition = 0; transition < 3; ++transition) {
        testHighContrast = transition != 2;
        if (transition == 1) testWindowColor = RGB(70, 80, 90);
        PHPVPN_CHECK(resources.update(instance, 96));
        PHPVPN_CHECK_EQ(resources.font(ui::Font::label), font);
        PHPVPN_CHECK_EQ(resources.icon(0), icon);
        PHPVPN_CHECK_EQ(resources.dpi(), 96);
        PHPVPN_CHECK(resources.pen(ui::Color::surface) != previousPen);
        LOGPEN pen{};
        PHPVPN_CHECK(GetObjectW(resources.pen(ui::Color::surface), sizeof(pen), &pen) != 0);
        PHPVPN_CHECK_EQ(pen.lopnColor, testHighContrast ? testWindowColor : normalColor);
        PHPVPN_CHECK_EQ(resources.color(ui::Color::surface), pen.lopnColor);
        if(testHighContrast) PHPVPN_CHECK_EQ(resources.color(ui::Color::serverInfoValid), testWindowColor);
        previousPen = resources.pen(ui::Color::surface);
        PHPVPN_CHECK(resources.update(instance, 96));
        PHPVPN_CHECK_EQ(resources.pen(ui::Color::surface), previousPen);
        PHPVPN_CHECK_EQ(resources.font(ui::Font::label), font);
        PHPVPN_CHECK_EQ(resources.icon(0), icon);
    }
}
