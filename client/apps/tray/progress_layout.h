#pragma once
#include "layout.h"
#include <algorithm>
#include <array>

namespace phpvpn::tray {
struct ProgressLayout {
    Rect eyebrow, card, detailsHeading, checksHeading, separator, panel;
    std::array<Rect, 2> labels, values;
    std::array<Rect, 3> checks, checkIcons, checkLabels;
    Rect panelHeading, panelAction, countdown;
    int height = 0;
};
// Text measurement and placement must use the same available width.
inline int progressHorizontalMargin(int width, int dpi) {
    return buttonFooterMargin(width, dpi);
}
inline int progressContentRight(int width, int dpi) {
    const int margin = progressHorizontalMargin(width, dpi);
    return std::max(margin, width - margin);
}
// Heights are measured with the actual native fonts before layout, in pixels.
inline ProgressLayout computeProgressLayout(int width, int dpi, int actionHeight, bool success,
                                            bool disconnect, int panelHeadingHeight = 28) {
    const auto p = [dpi](int n) { return scaleForDpi(n, dpi); };
    const int m = progressHorizontalMargin(width, dpi), pad = p(24);
    const int verticalMargin = std::min(p(width < p(600) ? 16 : 32), width / 8);
    const int right = progressContentRight(width, dpi), innerRight = std::max(m + pad, right - pad);
    ProgressLayout l;
    int y = verticalMargin;
    l.eyebrow = {m, y, right, y + p(24)};
    y += p(32);
    if (!disconnect) {
        const int top = y;
        y += pad;
        l.detailsHeading = {m + pad, y, innerRight, y + p(28)};
        y += p(44);
        for (int i = 0; i < 2; ++i) {
            const bool narrow = width < p(600);
            l.labels[i] = {m + pad, y, narrow ? innerRight : m + pad + p(160), y + p(24)};
            if (narrow)
                y += p(28);
            l.values[i] = {narrow ? m + pad : m + pad + p(176), y, innerRight, y + p(32)};
            y += p(40);
        }
        y += p(8);
        l.separator = {m + pad, y, innerRight, y + std::max(1, p(1))};
        y += p(20);
        l.checksHeading = {m + pad, y, innerRight, y + p(28)};
        y += p(44);
        for (std::size_t i = 0; i < 3; ++i) {
            const bool stacked = width < p(400);
            l.checkIcons[i] = {m + pad, y, m + pad + p(28), y + p(32)};
            l.checkLabels[i] = {m + pad + p(40), y, m + pad + p(176), y + p(32)};
            l.checks[i] = {m + pad + p(stacked ? 40 : 176), y + p(stacked ? 28 : 0),
                           innerRight, y + p(stacked ? 56 : 32)};
            y += p(stacked ? 60 : 40);
        }
        y += p(16);
        l.card = {m, top, right, y};
        y += p(16);
    }
    const int panelTop = y;
    y += pad;
    const bool beside = success && width >= p(760);
    const int panelRight = beside ? innerRight - p(208) : innerRight;
    l.panelHeading = {m + pad, y, panelRight, y + panelHeadingHeight};
    y += panelHeadingHeight + p(4);
    l.panelAction = {m + pad, y, panelRight, y + actionHeight};
    y += actionHeight;
    if (beside)
        l.countdown = {innerRight - p(196), panelTop + pad + p(16), innerRight, panelTop + pad + p(40)};
    else if (success) {
        y += p(12);
        l.countdown = {m + pad, y, innerRight, y + p(24)};
        y += p(24);
    }
    y += pad;
    l.panel = {m, panelTop, right, y};
    l.height = y + verticalMargin;
    return l;
}
} // namespace phpvpn::tray
