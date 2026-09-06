#include "harness.h"
#include "../../apps/tray/wizard_layout.h"
#include "../../apps/tray/menu_model.h"
#include "../../apps/tray/editor_document.h"

namespace phpvpn::tray {
PHPVPN_TEST(WizardLayout, FooterRemainsAccessibleAcrossDpiAndSmallWorkAreas) {
    for (int dpi : {96, 120, 144, 192}) for (const auto logical : {Size{1000, 680}, Size{600, 440}, Size{480, 320}}) {
        const Size client{scaleForDpi(logical.width, dpi), scaleForDpi(logical.height, dpi)};
        const auto layout = computeWizardLayout(client, dpi);
        PHPVPN_CHECK(layout.content.bottom <= layout.buttons[0].top);
        PHPVPN_CHECK(layout.content.left >= layout.steps.right);
        PHPVPN_CHECK(layout.content.right <= client.width);
        for (std::size_t i = 0; i < layout.buttons.size(); ++i) {
            const auto& button = layout.buttons[i];
            PHPVPN_CHECK(button.left >= 0 && button.right <= client.width && button.top >= 0 && button.bottom <= client.height);
            PHPVPN_CHECK(button.right - button.left >= scaleForDpi(100, dpi));
            if (i) PHPVPN_CHECK(button.right < layout.buttons[i - 1].left);
        }
    }
}
PHPVPN_TEST(TrayMenu, ExactOrderAndOnlyChosenStateActions) {
    constexpr MenuAction expected[]{MenuAction::toggle, MenuAction::separator, MenuAction::wizard, MenuAction::contracts,
        MenuAction::edit, MenuAction::separator, MenuAction::info, MenuAction::statistics, MenuAction::separator, MenuAction::exit};
    for (std::size_t i = 0; i < configurationMenu.size(); ++i) {
        PHPVPN_CHECK(configurationMenu[i].action == expected[i]);
        PHPVPN_CHECK(requiresConfigState(expected[i]) == (i == 3 || i == 7));
    }
}
PHPVPN_TEST(EditorDocument, ExactUnmodifiedBytesAndUnambiguousRawNulRepair) {
    EditorDocument model;
    const std::string original = "{\r\n\"a\": 1\r}\n";
    model.load(original);
    PHPVPN_CHECK(!model.literal());
    auto unchanged = model.edited("normalized display", false);
    PHPVPN_REQUIRE_OK(unchanged);
    PHPVPN_CHECK_EQ(*unchanged, original);
    const std::string nul("{\"a\":\"x\0y\"}", 11);
    model.load(nul);
    PHPVPN_CHECK(model.literal());
    const auto normalized = normalizeConfigText(nul);
    PHPVPN_REQUIRE_OK(normalized);
    PHPVPN_CHECK_EQ(*normalized, nul);
    auto roundTrip = model.edited(model.presentation(), true);
    PHPVPN_REQUIRE_OK(roundTrip);
    PHPVPN_CHECK_EQ(*roundTrip, nul);
    PHPVPN_CHECK(!model.edited("\"incomplete", true));
    auto repaired = model.edited(Json::escapeString("{\"a\":\"xy\"}"), true);
    PHPVPN_REQUIRE_OK(repaired);
    PHPVPN_CHECK_EQ(*repaired, std::string("{\"a\":\"xy\"}"));
}
} // namespace phpvpn::tray
