#include "harness.h"
#include "../../apps/tray/layout.h"
#include <array>
#include <limits>

namespace {
using namespace phpvpn::tray;
constexpr std::array monitors{
    MonitorGeometry{1, {0, 0, 1920, 1080}, {0, 0, 1920, 1040}, 96, true},
    MonitorGeometry{2, {-1600, 0, 0, 1200}, {-1600, 0, 0, 1160}, 144, false},
    MonitorGeometry{3, {0, -1200, 1920, 0}, {0, -1200, 1920, -40}, 192, false}};
}
PHPVPN_TEST(TrayGeometry, ParentMenuForegroundCursorPrimaryOrder) {
    MonitorPreference p{2, Point{200, -500}, 1, Point{-500, 300}};
    PHPVPN_CHECK_EQ(selectMonitor(monitors, p), std::optional<std::size_t>(1));
    p.parent = 999; // destroyed/unplugged parent cannot steal the menu fallback
    PHPVPN_CHECK_EQ(selectMonitor(monitors, p), std::optional<std::size_t>(2));
    p.menuPoint.reset();
    PHPVPN_CHECK_EQ(selectMonitor(monitors, p), std::optional<std::size_t>(0));
    p.foreground = 999;
    PHPVPN_CHECK_EQ(selectMonitor(monitors, p), std::optional<std::size_t>(1));
    p.cursor.reset();
    PHPVPN_CHECK_EQ(selectMonitor(monitors, p), std::optional<std::size_t>(0));
    PHPVPN_CHECK(!selectMonitor({}, p));
}
PHPVPN_TEST(TrayGeometry, SavedMenuSurvivesCursorMovementAndUnplug) {
    MonitorPreference p{{}, Point{-1300, 800}, 1, Point{100, 100}};
    PHPVPN_CHECK_EQ(selectMonitor(monitors, p), std::optional<std::size_t>(1));
    const std::array remaining{monitors[0], monitors[2]};
    PHPVPN_CHECK_EQ(selectMonitor(remaining, p), std::optional<std::size_t>(0));
    p.menuPoint = Point{300, -900};
    PHPVPN_CHECK_EQ(selectMonitor(remaining, p), std::optional<std::size_t>(1));
}
PHPVPN_TEST(TrayGeometry, InitialClientUsesTargetDpiFrameAndSignedWorkArea) {
    const auto placed = initialWindowRect({600, 400}, monitors[1], {24, 58});
    PHPVPN_CHECK_EQ(placed.right - placed.left, 924);
    PHPVPN_CHECK_EQ(placed.bottom - placed.top, 658);
    PHPVPN_CHECK_EQ(placed.left, -1262);
    PHPVPN_CHECK_EQ(placed.top, 251);
    const auto above = initialWindowRect({500, 300}, monitors[2], {32, 74});
    PHPVPN_CHECK(above.top < 0 && above.bottom <= -40);
    PHPVPN_CHECK_EQ(above.right - above.left, 1032);
    PHPVPN_CHECK_EQ(above.bottom - above.top, 674);
    PHPVPN_CHECK_EQ(initialWindowRect({2000, 2000}, monitors[2], {32, 74}), monitors[2].work);
}
PHPVPN_TEST(TrayGeometry, ReactivationPreservesExactGeometryOnAnyMonitor) {
    for (const Rect current : {Rect{113, 119, 999, 721}, Rect{-1500, 31, -57, 1131}, Rect{11, -1100, 799, -80}})
        PHPVPN_CHECK_EQ(usableWindowRect(current, monitors), current);
    auto changedDpi = monitors;
    changedDpi[1].dpi = 192;
    const Rect current{-1400, 20, -200, 900};
    PHPVPN_CHECK_EQ(usableWindowRect(current, changedDpi), current);
}
PHPVPN_TEST(TrayGeometry, UnplugMovesOnlyNecessaryDistanceWithoutResizing) {
    const Rect current{-1500, 200, -500, 900};
    const std::array remaining{monitors[0], monitors[2]};
    PHPVPN_CHECK_EQ(usableWindowRect(current, remaining), (Rect{0, 200, 1000, 900}));
    // Plugging the second monitor back in does not move an already usable rect.
    const Rect moved{0, 200, 1000, 900};
    PHPVPN_CHECK_EQ(usableWindowRect(moved, monitors), moved);
}
PHPVPN_TEST(TrayGeometry, ResolutionWorkAreaAndDpiRecommendationMinimalClamp) {
    auto changed = monitors;
    changed[0].bounds = {0, 0, 1280, 800};
    changed[0].work = {0, 0, 1280, 760};
    PHPVPN_CHECK_EQ(usableWindowRect({600, 300, 1600, 900}, changed), (Rect{280, 160, 1280, 760}));
    PHPVPN_CHECK_EQ(usableWindowRect({-10, 20, 2000, 1000}, changed), (Rect{0, 0, 1280, 760}));
    // A two-pixel work-area loss must not center or otherwise resize the window.
    PHPVPN_CHECK_EQ(usableWindowRect({100, 100, 800, 762}, changed), (Rect{100, 98, 800, 760}));
    const Rect recommended{-1800, 60, -300, 1100};
    PHPVPN_CHECK_EQ(usableWindowRect(recommended, monitors), (Rect{-1600, 60, -100, 1100}));
}
PHPVPN_TEST(TrayGeometry, StraddlingWindowUsesLargestIntersection) {
    PHPVPN_CHECK_EQ(usableWindowRect({-900, 100, 100, 800}, monitors), (Rect{-1000, 100, 0, 800}));
}
PHPVPN_TEST(TrayGeometry, InvalidAreasAndExtremeSizesStayBounded) {
    const std::array invalid{MonitorGeometry{1, {}, {}, 96, true}, monitors[1]};
    PHPVPN_CHECK_EQ(selectMonitor(invalid, {}), std::optional<std::size_t>(1));
    const auto maximum = (std::numeric_limits<int>::max)();
    PHPVPN_CHECK_EQ(initialWindowRect({maximum, maximum}, monitors[1], {maximum, maximum}), monitors[1].work);
    const Rect current{-100, -100, 200, 200};
    PHPVPN_CHECK_EQ(usableWindowRect(current, {}), current);
}
PHPVPN_TEST(TrayGeometry, FixedFooterAndFocusRevealShareScrollBounds) {
    for (const auto size : {Size{1180, 680}, Size{480, 320}, Size{200, 90}, Size{0, 0}}) {
        const auto layout = computeContentLayout(size, 100, 70, 12096);
        PHPVPN_CHECK_EQ(layout.footer.bottom, size.height);
        PHPVPN_CHECK_EQ(layout.viewport.bottom, layout.footer.top);
        PHPVPN_CHECK_EQ(layout.header.bottom, layout.viewport.top);
        PHPVPN_CHECK(layout.header.bottom >= 0 && layout.viewport.bottom >= layout.viewport.top);
        const int page = layout.viewport.bottom - layout.viewport.top;
        const int last = revealScrollPosition(0, page, 12096, {0, 12048, 100, 12096});
        if (page >= 48) PHPVPN_CHECK_EQ(last, layout.scrollMax);
        else PHPVPN_CHECK_EQ(last, 12048); // oversized target aligns its top
        PHPVPN_CHECK_EQ(revealScrollPosition(last, page, 12096, {0, 0, 100, 48}), 0);
    }
}
