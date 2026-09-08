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
PHPVPN_TEST(TrayLayout, ConnectCardMatchesReferenceAndKeepsContentAlignedAtSupportedDpi) {
    // N09b: reference client x=1..1462, card x=79, target width 940 DIP.
    constexpr double referenceLeft = (79.0 - 1.0) * 940.0 / 1461.0;
    for (const int dpi : {96, 120, 144, 192}) {
        const auto p = [dpi](int n) { return scaleForDpi(n, dpi); };
        for (const bool success : {false, true}) {
            const auto l = computeProgressLayout(p(940), dpi, p(42), p(24), p(24), success, p(28));
            PHPVPN_CHECK(std::abs(l.card.left * 96.0 / dpi - referenceLeft) <= 6.0);
            PHPVPN_CHECK_EQ(l.heading.left, l.card.left);
            PHPVPN_CHECK_EQ(l.panel.left, l.card.left);
            PHPVPN_CHECK_EQ(l.heading.right, l.card.right);
            PHPVPN_CHECK_EQ(l.detailsHeading.left - l.card.left, p(24));
            PHPVPN_CHECK(l.values[0].right > l.values[0].left);
            PHPVPN_CHECK(l.panelHeading.right > l.panelHeading.left);
            PHPVPN_CHECK(l.panelAction.bottom <= l.panel.bottom);
            PHPVPN_CHECK_EQ(l.eyebrow.top, p(32));
            PHPVPN_CHECK(l.card.top > l.description.bottom);
        }
        const auto narrow = computeProgressLayout(p(320), dpi, p(84), p(48), p(72), true, p(56));
        PHPVPN_CHECK_EQ(narrow.card.left, p(16));
        PHPVPN_CHECK(narrow.values[0].top >= narrow.labels[0].bottom);
        PHPVPN_CHECK(narrow.countdown.top >= narrow.panelAction.bottom);
    }
}
PHPVPN_TEST(TrayLayout, ConnectAllFourEdgesMatchBothClientReferences) {
    // Entire reference client: (1,79), 1461x995 pixels mapped to 940x680 DIP.
    const std::array<std::array<double, 4>, 2> references{{
        {{(79.0 - 1) * 940 / 1461, (310.0 - 79) * 680 / 995,
          (1364.0 - 1) * 940 / 1461, (828.0 - 79) * 680 / 995}},
        {{(78.0 - 1) * 940 / 1461, (310.0 - 79) * 680 / 995,
          (1360.0 - 1) * 940 / 1461, (827.0 - 79) * 680 / 995}}}};
    for (const int dpi : {96, 120, 144, 192}) {
        const auto p = [dpi](int n) { return scaleForDpi(n, dpi); };
        for (const int heading : {42, 46}) {
            for (const bool success : {false, true}) {
                const auto l = computeProgressLayout(p(940), dpi, p(heading), p(24), p(24), success, p(28));
                const std::array<int, 4> actual{l.card.left, l.card.top, l.card.right, l.card.bottom};
                for (const auto &reference : references)
                    for (std::size_t edge = 0; edge < actual.size(); ++edge)
                        PHPVPN_CHECK(std::abs(actual[edge] * 96.0 / dpi - reference[edge]) <= 6.0);
                PHPVPN_CHECK(l.description.bottom < l.card.top);
                PHPVPN_CHECK(l.detailsHeading.bottom < l.labels[0].top);
                PHPVPN_CHECK(l.values[1].bottom < l.separator.top);
                PHPVPN_CHECK(l.checksHeading.bottom < l.checks[0].top);
                PHPVPN_CHECK(l.checks[2].bottom < l.card.bottom);
                PHPVPN_CHECK(l.card.bottom < l.panel.top);
                PHPVPN_CHECK(l.height <= p(680));
            }
        }
    }
}
PHPVPN_TEST(TrayLayout, ConnectResponsiveContentStaysInsideCardsAcrossBreakpoints) {
    for (const int dpi : {96, 120, 144, 192}) {
        const auto p = [dpi](int n) { return scaleForDpi(n, dpi); };
        for (const int width : {320, 399, 400, 599, 600, 640, 759, 760, 940}) {
            for (const bool success : {false, true}) {
                const auto l = computeProgressLayout(p(width), dpi, p(84), p(48), p(72), success, p(56));
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
                PHPVPN_CHECK(l.description.bottom < l.card.top && l.card.bottom < l.panel.top);
                if (success) {
                    PHPVPN_CHECK(contained(l.countdown, l.panel));
                    PHPVPN_CHECK(l.countdown.left >= l.panelAction.right || l.countdown.top >= l.panelAction.bottom);
                }
                PHPVPN_CHECK_EQ(l.card.left, p(width < 600 ? 16 : 48));
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
        PHPVPN_CHECK_EQ(l.textArea.bottom, scaleForDpi(460, dpi));
        PHPVPN_CHECK_EQ(l.saveButton.bottom, scaleForDpi(528, dpi));
        const auto info = computeEditorLayout(size, dpi, false);
        PHPVPN_CHECK_EQ(info.textArea.top, scaleForDpi(32, dpi));
        PHPVPN_CHECK_EQ(info.textArea.bottom, scaleForDpi(528, dpi));
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
            PHPVPN_CHECK(layout.resetButton.right < layout.cancelButton.left);
            PHPVPN_CHECK(layout.cancelButton.right < layout.saveButton.left);
            PHPVPN_CHECK(layout.resetButton.right > layout.resetButton.left);
            PHPVPN_CHECK(layout.resetButton.bottom > layout.resetButton.top);
            PHPVPN_CHECK(layout.saveButton.bottom > layout.saveButton.top);
            PHPVPN_CHECK(layout.textArea.bottom <= layout.saveButton.top);
            const auto readOnly = computeEditorLayout(size, dpi, false);
            PHPVPN_CHECK_EQ(readOnly.saveButton.right, 0);
            PHPVPN_CHECK_EQ(readOnly.resetButton.right, 0);
            PHPVPN_CHECK_EQ(readOnly.cancelButton.right, 0);
            PHPVPN_CHECK(inside(readOnly.textArea, size));
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
