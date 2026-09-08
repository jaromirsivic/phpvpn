#pragma once
#include "layout.h"
#include <algorithm>
#include <array>

namespace phpvpn::tray {
struct StatisticsRowLayout {
    Rect label, key, value;
};
struct StatisticsLayout {
    Rect heading, description, freshness;
    std::array<Rect, 3> cards, titles;
    std::array<StatisticsRowLayout, 9> rows;
    int height = 0;
};
struct StatisticsMetrics {
    int descriptionHeight = 24, freshnessHeight = 0;
    std::array<int, 9> valueWidths{}, keyWidths{}, labelWidths{};
};
inline StatisticsLayout computeStatisticsLayout(int width, int dpi, const StatisticsMetrics &metrics) {
    const auto p = [dpi](int n) { return scaleForDpi(n, dpi); };
    const bool narrow = width < p(760);
    const int m = std::min(p(narrow ? 16 : 32), width / 8), pad = p(20), gap = p(24), right = width - m;
    StatisticsLayout l;
    int y = m;
    l.heading = {m, y, right, y + p(36)};
    y += p(44);
    l.description = {m, y, right, y + metrics.descriptionHeight};
    y += metrics.descriptionHeight + p(24);
    if (metrics.freshnessHeight) {
        l.freshness = {m, y, right, y + metrics.freshnessHeight};
        y += metrics.freshnessHeight + p(12);
    }
    const int half = narrow ? right - m : (right - m - gap) / 2;
    auto row = [&](std::size_t i, int left, int top, int available) {
        const bool stacked =
            metrics.valueWidths[i] + std::max(metrics.keyWidths[i], metrics.labelWidths[i]) + p(16) > available;
        auto &r = l.rows[i];
        const int valueWidth = std::min(available, metrics.valueWidths[i] + p(8));
        r.label = {left, top, left + available - (stacked ? 0 : valueWidth + p(12)), top + p(24)};
        // Technical keys remain readable/selectable via their native read-only field.
        r.key = {left, top + p(24), r.label.right, top + p(44)};
        const int valueTop = stacked ? top + p(48) : top + p(6);
        r.value = {stacked ? left : left + available - valueWidth, valueTop, left + available, valueTop + p(36)};
        return stacked ? p(104) : p(60);
    };
    auto card = [&](std::size_t index, int left, int top, int cardWidth, std::size_t first, std::size_t count) {
        l.titles[index] = {left + pad, top + pad, left + cardWidth - pad, top + pad + p(28)};
        int next = top + pad + p(44);
        for (std::size_t i = first; i < first + count; ++i)
            next += row(i, left + pad, next, std::max(1, cardWidth - 2 * pad));
        l.cards[index] = {left, top, left + cardWidth, next + pad};
        return next + pad;
    };
    const int first = card(0, m, y, half, 0, 3);
    const int second = card(1, narrow ? m : m + half + gap, narrow ? first + gap : y, half, 3, 2);
    if (!narrow) {
        const int bottom = std::max(first, second);
        l.cards[0].bottom = l.cards[1].bottom = bottom;
    }
    y = std::max(first, second) + gap;
    l.titles[2] = {m + pad, y + pad, right - pad, y + pad + p(28)};
    int next = y + pad + p(44), end = next;
    if (narrow) {
        for (std::size_t i = 5; i < 9; ++i)
            end += row(i, m + pad, end, std::max(1, right - m - 2 * pad));
    } else {
        const int available = (right - m - 2 * pad - gap) / 2;
        int leftEnd = next, rightEnd = next;
        for (std::size_t i = 5; i < 7; ++i)
            leftEnd += row(i, m + pad, leftEnd, available);
        for (std::size_t i = 7; i < 9; ++i)
            rightEnd += row(i, m + pad + available + gap, rightEnd, available);
        end = std::max(leftEnd, rightEnd);
    }
    l.cards[2] = {m, y, right, end + pad};
    l.height = end + pad + m;
    return l;
}
} // namespace phpvpn::tray
