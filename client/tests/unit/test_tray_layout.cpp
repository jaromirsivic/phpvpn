#include "harness.h"
#include "../../apps/tray/layout.h"
#include "../../apps/tray/progress_layout.h"
#include <array>
#include <cmath>

namespace {
using namespace phpvpn::tray;
bool contains(Rect rect, int x, int y) { return x >= rect.left && x < rect.right && y >= rect.top && y < rect.bottom; }
bool inside(Rect rect, Size size) {
    return rect.left >= 0 && rect.top >= 0 && rect.right >= rect.left && rect.bottom >= rect.top &&
           rect.right <= size.width && rect.bottom <= size.height;
}
}
PHPVPN_TEST(TrayLayout, DefaultClientSizeScalesAtEverySupportedDpi) {
    for (const int dpi : {96, 120, 144, 192}) {
        const auto size = defaultEditorClientSize(dpi);
        PHPVPN_CHECK_EQ(size.width, 1080 * dpi / 96);
        PHPVPN_CHECK_EQ(size.height, 560 * dpi / 96);
    }
}
PHPVPN_TEST(TrayLayout, ProgressUsesSharedMarginsAndStaticContextAtSupportedDpi) {
    for (const int dpi : {96, 120, 144, 192}) {
        const auto p = [dpi](int n) { return scaleForDpi(n, dpi); };
        for (const int width : {320, 599, 600, 759, 760, 940}) {
            for (const bool success : {false, true}) for (const bool disconnect : {false, true}) {
                const auto l = computeProgressLayout(p(width), dpi, p(72), success, disconnect, p(56));
                const int margin = buttonFooterMargin(p(width), dpi);
                PHPVPN_CHECK_EQ(l.eyebrow.left, margin);
                PHPVPN_CHECK_EQ(p(width) - l.eyebrow.right, margin);
                PHPVPN_CHECK_EQ(l.panel.left, margin);
                PHPVPN_CHECK_EQ(p(width) - l.panel.right, margin);
                PHPVPN_CHECK(l.panel.top >= l.eyebrow.bottom + p(8));
                PHPVPN_CHECK(disconnect ? l.card == Rect{} : l.card.bottom < l.panel.top);
                PHPVPN_CHECK(disconnect ? l.separator == Rect{} : l.card.top >= l.eyebrow.bottom + p(8));
                PHPVPN_CHECK_EQ(l.height - l.panel.bottom, buttonFooterMargin(p(width), dpi));
            }
        }
        const auto narrow = computeProgressLayout(p(320), dpi, p(72), true, false, p(56));
        PHPVPN_CHECK(narrow.values[0].top >= narrow.labels[0].bottom);
        PHPVPN_CHECK(narrow.countdown.top >= narrow.panelAction.bottom);
    }
}
PHPVPN_TEST(TrayLayout, ProgressCardsAndPanelsRemainSeparatedAtSupportedDpi) {
    for (const int dpi : {96, 120, 144, 192}) {
        const auto p = [dpi](int n) { return scaleForDpi(n, dpi); };
        for (const bool success : {false, true}) {
            const auto l = computeProgressLayout(p(940), dpi, p(24), success, false, p(28));
            PHPVPN_CHECK(l.detailsHeading.bottom < l.labels[0].top);
            PHPVPN_CHECK(l.values[1].bottom < l.separator.top);
            PHPVPN_CHECK(l.checksHeading.bottom < l.checks[0].top);
            PHPVPN_CHECK(l.checks[2].bottom < l.card.bottom);
            PHPVPN_CHECK(l.card.bottom < l.panel.top);
            PHPVPN_CHECK(l.panelHeading.bottom <= l.panelAction.top);
            PHPVPN_CHECK(l.panelAction.bottom <= l.panel.bottom);
            PHPVPN_CHECK(l.height <= p(680));
        }
    }
}
PHPVPN_TEST(TrayLayout, ConnectResponsiveContentStaysInsideCardsAcrossBreakpoints) {
    for (const int dpi : {96, 120, 144, 192}) {
        const auto p = [dpi](int n) { return scaleForDpi(n, dpi); };
        for (const int width : {320, 399, 400, 599, 600, 640, 759, 760, 940}) {
            for (const bool success : {false, true}) {
                const auto l = computeProgressLayout(p(width), dpi, p(72), success, false, p(56));
                const auto contained = [](Rect child, Rect parent) {
                    return child.left >= parent.left && child.right <= parent.right &&
                           child.top >= parent.top && child.bottom <= parent.bottom &&
                           child.left < child.right && child.top < child.bottom;
                };
                for (const auto r : l.values) PHPVPN_CHECK(contained(r, l.card));
                for (const auto r : l.labels) PHPVPN_CHECK(contained(r, l.card));
                for (const auto r : l.checks) PHPVPN_CHECK(contained(r, l.card));
                for (const auto r : l.checkLabels) PHPVPN_CHECK(contained(r, l.card));
                PHPVPN_CHECK(contained(l.panelHeading, l.panel));
                PHPVPN_CHECK(contained(l.panelAction, l.panel));
                PHPVPN_CHECK(l.card.top >= l.eyebrow.bottom + p(8) && l.card.bottom < l.panel.top);
                if (success) {
                    PHPVPN_CHECK(contained(l.countdown, l.panel));
                    PHPVPN_CHECK(l.countdown.left >= l.panelAction.right || l.countdown.top >= l.panelAction.bottom);
                }
                PHPVPN_CHECK_EQ(l.card.left, buttonFooterMargin(p(width), dpi));
            }
        }
    }
}
PHPVPN_TEST(TrayLayout, EditorReferenceTopPreservesHeaderAndFooterAtSupportedDpi) {
    // N09b: reference client y=55..1004, text top y=202, target height 560 DIP.
    constexpr double referenceTop = (202.0 - 55.0) * 560.0 / 949.0;
    for (const int dpi : {96, 120, 144, 192}) {
        const auto size = defaultEditorClientSize(dpi);
        const auto l = computeEditorLayout(size, dpi, true);
        PHPVPN_CHECK(std::abs(l.textArea.top * 96.0 / dpi - referenceTop) <= 6.0);
        PHPVPN_CHECK(l.heading.bottom <= l.path.top);
        PHPVPN_CHECK(l.path.bottom < l.textArea.top);
        PHPVPN_CHECK(inside(l.heading, size) && inside(l.path, size));
        PHPVPN_CHECK(l.textArea.bottom <= l.footer.top);
        PHPVPN_CHECK_EQ(l.saveButton.bottom, scaleForDpi(544, dpi));
        const auto info = computeEditorLayout(size, dpi, false);
        PHPVPN_CHECK_EQ(info.textArea.top, scaleForDpi(60, dpi));
        PHPVPN_CHECK(info.textArea.bottom <= info.footer.top);
        PHPVPN_CHECK_EQ(info.saveButton.bottom, scaleForDpi(544, dpi));
        PHPVPN_CHECK_EQ(l.tabs, Rect{});
        PHPVPN_CHECK(info.heading.bottom <= info.tabs.top);
        PHPVPN_CHECK(info.tabs.bottom <= info.footer.top);
        PHPVPN_CHECK(inside(info.tabs, size) && info.tabs.bottom > info.tabs.top);
        PHPVPN_CHECK(inside(info.heading, size));
    }
}
PHPVPN_TEST(TrayLayout, EditorSharedFooterAndTabs) {
    for (int dpi : {96, 120, 144, 192}) for (int width : {320, 359, 360, 480, 640, 1080}) {
        const Size size{scaleForDpi(width, dpi), scaleForDpi(560, dpi)};
        const auto p = [dpi](int v) { return scaleForDpi(v, dpi); };
        for (bool editable : {false, true}) {
            const std::array specs{
                FooterButtonSpec{p(104), FooterSide::leading, editable},
                FooterButtonSpec{p(104), FooterSide::trailing, editable},
                FooterButtonSpec{p(104), FooterSide::trailing, true}};
            const auto expected = computeButtonFooterLayout(size, dpi, specs);
            const auto actual = computeEditorLayout(size, dpi, editable);
            PHPVPN_CHECK_EQ(actual.footer, expected.bounds);
            PHPVPN_CHECK_EQ(actual.resetButton, expected.buttons[0]);
            PHPVPN_CHECK_EQ(actual.cancelButton, expected.buttons[1]);
            PHPVPN_CHECK_EQ(actual.saveButton, expected.buttons[2]);
            PHPVPN_CHECK(actual.textArea.bottom <= actual.footer.top);
            PHPVPN_CHECK(editable ? actual.tabs == Rect{} :
                inside(actual.tabs, size) && actual.tabs.bottom <= actual.footer.top);
        }
    }
}
PHPVPN_TEST(TrayLayout, WorkAreaClampingAndCenteringSupportsSecondaryMonitorCoordinates) {
    const Rect work{-1280, 40, 0, 720};
    const auto size = clampToWorkArea(defaultEditorClientSize(192), work, 192);
    PHPVPN_CHECK_EQ(size.width, 1280);
    PHPVPN_CHECK_EQ(size.height, 680);
    const auto rect = centerInWorkArea(size, work);
    PHPVPN_CHECK_EQ(rect.left, -1280);
    PHPVPN_CHECK_EQ(rect.top, 40);
    PHPVPN_CHECK_EQ(rect.right, 0);
    PHPVPN_CHECK_EQ(rect.bottom, 720);
    const auto centered = centerInWorkArea({600, 400}, work);
    PHPVPN_CHECK_EQ(centered.left, -940);
    PHPVPN_CHECK_EQ(centered.top, 180);
}
PHPVPN_TEST(TrayLayout, RepeatedEdgeAndCornerResizesKeepControlsSeparateAndVisible) {
    for (const int dpi : {96, 120, 144, 192}) {
        for (const Size size : {Size{320, 160}, Size{800, 450}, Size{1080, 560}, Size{1640, 560},
                               Size{1640, 980}, Size{1080, 980}, Size{450, 280}}) {
            const auto layout = computeEditorLayout(size, dpi, true);
            PHPVPN_CHECK(inside(layout.textArea, size));
            PHPVPN_CHECK(inside(layout.saveButton, size));
            PHPVPN_CHECK(inside(layout.resetButton, size));
            PHPVPN_CHECK(inside(layout.cancelButton, size));
            PHPVPN_CHECK(layout.resetButton.right < layout.cancelButton.left ||
                         layout.resetButton.bottom <= layout.cancelButton.top);
            PHPVPN_CHECK(layout.cancelButton.right < layout.saveButton.left);
            PHPVPN_CHECK(layout.resetButton.right > layout.resetButton.left);
            PHPVPN_CHECK(layout.resetButton.bottom > layout.resetButton.top);
            PHPVPN_CHECK(layout.saveButton.bottom > layout.saveButton.top);
            PHPVPN_CHECK(layout.textArea.bottom <= layout.saveButton.top);
            const auto readOnly = computeEditorLayout(size, dpi, false);
            PHPVPN_CHECK(readOnly.saveButton.right > readOnly.saveButton.left);
            PHPVPN_CHECK_EQ(readOnly.resetButton.right, 0);
            PHPVPN_CHECK_EQ(readOnly.cancelButton.right, 0);
            PHPVPN_CHECK(inside(readOnly.textArea, size));
            PHPVPN_CHECK(inside(readOnly.heading, size));
            PHPVPN_CHECK(readOnly.textArea.bottom <= readOnly.saveButton.top);
        }
    }
}
PHPVPN_TEST(TrayLayout, CancelVerticalArtifactRegressionLeavesNoUnpaintedPixel) {
    Size oldSize{1080, 560};
    auto old = computeEditorLayout(oldSize, 96, true);
    // Horizontal stretch reproduces the documented strip beside old Cancel.
    for (const Size nextSize : {Size{1550, 560}, Size{1550, 780}, Size{890, 780}, Size{1080, 560}}) {
        const auto next = computeEditorLayout(nextSize, 96, true);
        const auto exposed = exposedParentRegion(oldSize, nextSize, old, next);
        bool covered = true;
        for (int y = 0; y < nextSize.height; ++y) {
            for (int x = 0; x < nextSize.width; ++x) {
                if (!contains(exposed, x, y) && !contains(next.textArea, x, y) &&
                    !contains(next.saveButton, x, y) && !contains(next.resetButton, x, y) &&
                    !contains(next.cancelButton, x, y)) covered = false;
            }
        }
        PHPVPN_CHECK(covered);
        oldSize = nextSize;
        old = next;
    }
}
