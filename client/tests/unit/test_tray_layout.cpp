#include "harness.h"
#include "../../apps/tray/layout.h"
#include <array>

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
            PHPVPN_CHECK(layout.saveButton.right < layout.resetButton.left);
            PHPVPN_CHECK(layout.resetButton.right < layout.cancelButton.left);
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
