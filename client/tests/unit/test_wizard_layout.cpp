#include "harness.h"
#include "../../apps/tray/wizard_layout.h"
#include "../../apps/tray/menu_model.h"
#include "../../apps/tray/editor_document.h"
#include <array>

namespace phpvpn::tray {
PHPVPN_TEST(ButtonFooterLayout, MatchesWizardReferenceAtSupportedDpi) {
    for (int dpi : {96, 120, 144, 192}) {
        const auto p = [dpi](int value) { return scaleForDpi(value, dpi); };
        for (int logicalWidth : {320, 480, 600, 1000}) {
            const Size client{p(logicalWidth), p(680)};
            const std::array specs{
                FooterButtonSpec{p(88), FooterSide::leading, true},
                FooterButtonSpec{p(88), FooterSide::trailing, true},
                FooterButtonSpec{p(200), FooterSide::trailing, true},
            };
            const auto footer = computeButtonFooterLayout(client, dpi, specs);

            PHPVPN_CHECK_EQ(footer.margin, std::min(client.width / 8, p(16)));
            PHPVPN_CHECK_EQ(footer.gap, p(8));
            PHPVPN_CHECK_EQ(footer.buttonHeight, p(36));
            PHPVPN_CHECK_EQ(footer.buttons.size(), specs.size());
            for (std::size_t i = 0; i < footer.buttons.size(); ++i) {
                const auto button = footer.buttons[i];
                PHPVPN_CHECK(button.left >= 0 && button.right <= client.width);
                PHPVPN_CHECK(button.top >= 0 && button.bottom <= client.height);
                PHPVPN_CHECK_EQ(button.bottom - button.top, p(36));
                for (std::size_t j = 0; j < i; ++j) {
                    const auto previous = footer.buttons[j];
                    PHPVPN_CHECK(button.right <= previous.left || button.left >= previous.right ||
                                 button.bottom <= previous.top || button.top >= previous.bottom);
                }
            }
            PHPVPN_CHECK(footer.buttons[1].right <= footer.buttons[2].left);
        }
    }
}
PHPVPN_TEST(ButtonFooterLayout, PasswordAndContractsBreakpointsPreserveSharedGeometry) {
    const auto empty = [](Rect value) { return value == Rect{}; };
    for (int dpi : {96, 120, 144, 192}) {
        const auto p = [dpi](int value) { return scaleForDpi(value, dpi); };
        const auto verify = [&](int width, std::span<const FooterButtonSpec> specs) {
            const auto footer = computeButtonFooterLayout({width, p(600)}, dpi, specs);
            PHPVPN_CHECK_EQ(footer.margin, p(16));
            PHPVPN_CHECK_EQ(footer.gap, p(8));
            PHPVPN_CHECK_EQ(footer.buttonHeight, p(36));
            for (std::size_t i = 0; i < specs.size(); ++i) {
                const auto button = footer.buttons[i];
                if (!specs[i].visible) {
                    PHPVPN_CHECK(empty(button));
                    continue;
                }
                PHPVPN_CHECK(button.left >= footer.margin && button.right <= width - footer.margin);
                PHPVPN_CHECK_EQ(button.bottom - button.top, p(36));
                PHPVPN_CHECK(button.top >= footer.bounds.top && button.bottom <= footer.bounds.bottom);
                for (std::size_t j = 0; j < i; ++j) if (specs[j].visible) {
                    const auto previous = footer.buttons[j];
                    PHPVPN_CHECK(button.right <= previous.left || button.left >= previous.right ||
                                 button.bottom <= previous.top || button.top >= previous.bottom);
                }
            }
            return footer;
        };

        const std::array password{
            FooterButtonSpec{p(104), FooterSide::leading, true},
            FooterButtonSpec{p(104), FooterSide::leading, true},
            FooterButtonSpec{p(104), FooterSide::trailing, true}};
        const int passwordBreakpoint = 2 * p(16) + 3 * p(104) + 2 * p(8);
        const auto passwordWide = verify(passwordBreakpoint, password);
        const auto passwordNarrow = verify(passwordBreakpoint - 1, password);
        PHPVPN_CHECK_EQ(passwordWide.rows, 1);
        PHPVPN_CHECK_EQ(passwordNarrow.rows, 2);
        PHPVPN_CHECK_EQ(passwordWide.buttons[0].left, p(16));
        PHPVPN_CHECK_EQ(passwordWide.buttons[1].left - passwordWide.buttons[0].right, p(8));
        PHPVPN_CHECK_EQ(passwordWide.buttons[2].right, passwordBreakpoint - p(16));

        for (const auto visibility : {std::array{false, false, true, true}, std::array{true, false, true, false},
                                      std::array{true, true, true, false}, std::array{true, true, true, true}}) {
            const std::array contracts{
                FooterButtonSpec{p(236), FooterSide::leading, visibility[0]},
                FooterButtonSpec{p(164), FooterSide::leading, visibility[1]},
                FooterButtonSpec{p(104), FooterSide::trailing, visibility[2]},
                FooterButtonSpec{p(180), FooterSide::trailing, visibility[3]}};
            int preferred = 2 * p(16), visible = 0;
            for (const auto& spec : contracts) if (spec.visible) { preferred += spec.preferredWidth; ++visible; }
            preferred += std::max(0, visible - 1) * p(8);
            const auto wide = verify(preferred, contracts);
            const auto narrow = verify(preferred - 1, contracts);
            const bool splitSides = visibility[0] || visibility[1];
            PHPVPN_CHECK_EQ(wide.rows, 1);
            PHPVPN_CHECK_EQ(narrow.rows, splitSides ? 2 : 1);
        }
    }
}
PHPVPN_TEST(WizardLayout, FooterRemainsAccessibleAcrossDpiAndSmallWorkAreas) {
    bool sawOneRow = false;
    bool sawTwoRows = false;
    for (int dpi : {96, 120, 144, 192}) for (const auto logical : {Size{1000, 680}, Size{600, 440}, Size{480, 320}, Size{320, 320}}) {
        const Size client{scaleForDpi(logical.width, dpi), scaleForDpi(logical.height, dpi)};
        const auto layout = computeWizardLayout(client, dpi);
        const std::array specs{
            FooterButtonSpec{scaleForDpi(88, dpi), FooterSide::leading, true},
            FooterButtonSpec{scaleForDpi(88, dpi), FooterSide::trailing, true},
            FooterButtonSpec{scaleForDpi(108, dpi), FooterSide::trailing, false},
            FooterButtonSpec{scaleForDpi(200, dpi), FooterSide::trailing, true},
        };
        const auto footer = computeButtonFooterLayout(client, dpi, specs);
        sawOneRow = sawOneRow || footer.rows == 1;
        sawTwoRows = sawTwoRows || footer.rows == 2;
        PHPVPN_CHECK_EQ(footer.buttons.size(), layout.buttons.size());
        for (std::size_t i = 0; i < layout.buttons.size(); ++i)
            PHPVPN_CHECK_EQ(layout.buttons[i], footer.buttons[i]);
        PHPVPN_CHECK(layout.content.bottom <= layout.buttons[0].top);
        PHPVPN_CHECK(layout.compact ? layout.content.top >= layout.steps.bottom : layout.content.left >= layout.steps.right);
        PHPVPN_CHECK(layout.content.right <= client.width);
        for (std::size_t i = 0; i < layout.buttons.size(); ++i) {
            if (i == 2) { PHPVPN_CHECK_EQ(layout.buttons[i], Rect{}); continue; }
            const auto& button = layout.buttons[i];
            PHPVPN_CHECK(button.left >= 0 && button.right <= client.width && button.top >= 0 && button.bottom <= client.height);
            PHPVPN_CHECK(button.right > button.left);
            if (i) PHPVPN_CHECK(button.left > layout.buttons[i == 3 ? 1 : i - 1].right || button.top > layout.buttons[i == 3 ? 1 : i - 1].bottom);
        }
    }
    PHPVPN_CHECK(sawOneRow);
    PHPVPN_CHECK(sawTwoRows);
}
PHPVPN_TEST(WizardLayout, ErrorDetailHasStandardGapAndNoCollision) {
    for (int dpi : {96, 144, 192}) for (int width : {320, 480, 600, 1000}) {
        const auto s = [dpi](int n) { return scaleForDpi(n, dpi); };
        const auto layout = computeWizardLayout({s(width), s(680)}, dpi, s(220), true, s(120));
        for (std::size_t i = 0; i < 4; ++i) {
            const auto& a = layout.buttons[i];
            PHPVPN_CHECK(a.right > a.left && a.left >= 0 && a.right <= s(width));
            for (std::size_t j = 0; j < i; ++j) {
                const auto& b = layout.buttons[j];
                PHPVPN_CHECK(a.left >= b.right || a.right <= b.left || a.top >= b.bottom || a.bottom <= b.top);
            }
        }
        if (width >= 600) {
            PHPVPN_CHECK_EQ(layout.buttons[2].left - layout.buttons[1].right, s(8));
            PHPVPN_CHECK_EQ(layout.buttons[3].left - layout.buttons[2].right, s(8));
        }
    }
}
PHPVPN_TEST(TrayMenu, ExactOrderAndOnlyChosenStateActions) {
    constexpr MenuAction expected[]{MenuAction::toggle, MenuAction::separator, MenuAction::wizard, MenuAction::contracts,
        MenuAction::edit, MenuAction::separator, MenuAction::info, MenuAction::statistics, MenuAction::about,
        MenuAction::separator, MenuAction::exit};
    static_assert(std::size(expected) == configurationMenu.size());
    for (std::size_t i = 0; i < configurationMenu.size(); ++i) {
        PHPVPN_CHECK(configurationMenu[i].action == expected[i]);
        // Contracts loads one document directly. Only Statistics needs state.
        PHPVPN_CHECK(requiresConfigState(expected[i]) == (i == 7));
    }
    PHPVPN_CHECK(configurationMenu[8].title == L"About");
}
PHPVPN_TEST(TrayMenu, AboutNamesBuildVersionLicenceAndCopyright) {
    PHPVPN_CHECK(aboutTitle == L"About PHPVPN");
    PHPVPN_CHECK(aboutHeading == L"PHPVPN version " PHPVPN_CLIENT_VERSION);
    PHPVPN_CHECK(aboutText == L"PHPVPN is freeware and open source software.\nLicense: MIT\n"
                              L"Copyright © www.pilandrome.com");
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
