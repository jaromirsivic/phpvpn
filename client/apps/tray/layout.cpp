#include "layout.h"
#include <array>
#include <algorithm>
#include <cstdint>
#include <limits>

namespace phpvpn::tray {
namespace {
bool usable(Rect r) { return r.right > r.left && r.bottom > r.top; }
bool contains(Rect a, Rect b) {
    return usable(a) && usable(b) && b.left >= a.left && b.top >= a.top && b.right <= a.right && b.bottom <= a.bottom;
}
bool contains(Rect r, Point p) { return p.x >= r.left && p.x < r.right && p.y >= r.top && p.y < r.bottom; }
long double distance(Rect a, Rect b) {
    const auto dx = std::max({0.0L, static_cast<long double>(a.left) - b.right, static_cast<long double>(b.left) - a.right});
    const auto dy = std::max({0.0L, static_cast<long double>(a.top) - b.bottom, static_cast<long double>(b.top) - a.bottom});
    return dx * dx + dy * dy;
}
int extent(int low, int high) {
    return static_cast<int>(std::clamp<std::int64_t>(static_cast<std::int64_t>(high) - low, 0, (std::numeric_limits<int>::max)()));
}
std::optional<std::size_t> nearest(std::span<const MonitorGeometry> monitors, Rect rect) {
    std::optional<std::size_t> best;
    long double bestDistance = (std::numeric_limits<long double>::max)();
    long double bestOverlap = -1;
    for (std::size_t i = 0; i < monitors.size(); ++i) {
        if (!usable(monitors[i].work)) continue;
        const auto d = distance(rect, monitors[i].bounds);
        const auto bounds = monitors[i].bounds;
        const auto overlap = static_cast<long double>(extent(std::max(rect.left, bounds.left), std::min(rect.right, bounds.right))) *
            extent(std::max(rect.top, bounds.top), std::min(rect.bottom, bounds.bottom));
        if (!best || overlap > bestOverlap || (overlap == bestOverlap && (d < bestDistance ||
            (d == bestDistance && monitors[i].primary && !monitors[*best].primary)))) {
            best = i; bestDistance = d; bestOverlap = overlap;
        }
    }
    return best;
}
}
std::optional<std::size_t> selectMonitor(std::span<const MonitorGeometry> monitors, const MonitorPreference& p) {
    const auto byId = [&](std::optional<std::uintptr_t> id) -> std::optional<std::size_t> {
        if (id) for (std::size_t i = 0; i < monitors.size(); ++i)
            if (monitors[i].id == *id && usable(monitors[i].work)) return i;
        return {};
    };
    const auto byPoint = [&](std::optional<Point> point) -> std::optional<std::size_t> {
        if (!point) return {};
        for (std::size_t i = 0; i < monitors.size(); ++i)
            if (usable(monitors[i].work) && contains(monitors[i].bounds, *point)) return i;
        // A saved menu point can outlive an unplug. It remains authoritative.
        return nearest(monitors, {point->x, point->y, point->x, point->y});
    };
    if (auto i = byId(p.parent)) return i;
    if (auto i = byPoint(p.menuPoint)) return i;
    if (auto i = byId(p.foreground)) return i;
    if (auto i = byPoint(p.cursor)) return i;
    for (std::size_t i = 0; i < monitors.size(); ++i)
        if (monitors[i].primary && usable(monitors[i].work)) return i;
    return nearest(monitors, {});
}
Rect clampWindowRect(Rect current, Rect work) {
    if (!usable(work)) return current;
    const int width = std::min(extent(current.left, current.right), extent(work.left, work.right));
    const int height = std::min(extent(current.top, current.bottom), extent(work.top, work.bottom));
    const int x = std::clamp(current.left, work.left, work.right - width);
    const int y = std::clamp(current.top, work.top, work.bottom - height);
    return {x, y, x + width, y + height};
}
Rect usableWindowRect(Rect current, std::span<const MonitorGeometry> monitors) {
    for (const auto& monitor : monitors) if (contains(monitor.work, current)) return current;
    const auto i = nearest(monitors, current);
    return i ? clampWindowRect(current, monitors[*i].work) : current;
}
Rect initialWindowRect(Size dip, const MonitorGeometry& monitor, Size frame) {
    const auto add = [](int a, int b) { return static_cast<int>(std::clamp<std::int64_t>(
        static_cast<std::int64_t>(a) + b, 0, (std::numeric_limits<int>::max)())); };
    return centerInWorkArea({add(scaleForDpi(dip.width, monitor.dpi), frame.width),
                             add(scaleForDpi(dip.height, monitor.dpi), frame.height)}, monitor.work);
}
ContentLayout computeContentLayout(Size client, int headerHeight, int footerHeight, int contentHeight) {
    const int w = std::max(0, client.width), h = std::max(0, client.height);
    const int footer = std::clamp(footerHeight, 0, h);
    const int header = std::clamp(headerHeight, 0, h - footer);
    return {{0, 0, w, header}, {0, header, w, h - footer}, {0, h - footer, w, h},
        std::max(0, std::max(0, contentHeight) - (h - footer - header))};
}
int revealScrollPosition(int position, int viewportHeight, int contentHeight, Rect target) {
    const int page = std::max(0, viewportHeight), maximum = std::max(0, std::max(0, contentHeight) - page);
    int next = std::clamp(position, 0, maximum);
    if (target.top < next || extent(target.top, target.bottom) > page) next = target.top;
    else if (target.bottom > static_cast<std::int64_t>(next) + page) next = target.bottom - page;
    return std::clamp(next, 0, maximum);
}
int scaleForDpi(int pixels, int dpi) {
    const auto scaled = (static_cast<std::int64_t>(pixels) * std::max(dpi, 1) + 48) / 96;
    return static_cast<int>(std::clamp<std::int64_t>(scaled, 0, std::numeric_limits<int>::max()));
}
int buttonFooterMargin(int width, int dpi) {
    return std::min(std::max(0, width) / 8, scaleForDpi(16, dpi));
}
ButtonFooterLayout computeButtonFooterLayout(
    Size client, int dpi, std::span<const FooterButtonSpec> buttons) {
    const int width = std::max(0, client.width);
    const int height = std::max(0, client.height);
    ButtonFooterLayout result;
    result.buttons.resize(buttons.size());
    result.margin = buttonFooterMargin(width, dpi);
    result.gap = scaleForDpi(8, dpi);
    result.buttonHeight = scaleForDpi(36, dpi);

    std::vector<std::size_t> leading;
    std::vector<std::size_t> trailing;
    std::int64_t preferredWidth = 0;
    for (std::size_t i = 0; i < buttons.size(); ++i) {
        if (!buttons[i].visible) continue;
        (buttons[i].side == FooterSide::leading ? leading : trailing).push_back(i);
        preferredWidth += std::max(1, buttons[i].preferredWidth);
    }
    const auto visibleCount = leading.size() + trailing.size();
    const int available = std::max(1, width - 2 * result.margin);
    if (visibleCount > 1) preferredWidth += static_cast<std::int64_t>(visibleCount - 1) * result.gap;
    const bool wraps = !leading.empty() && !trailing.empty() && preferredWidth > available;
    result.rows = visibleCount == 0 ? 0 : (wraps ? 2 : 1);
    const std::int64_t footerHeight = 2LL * result.margin +
        static_cast<std::int64_t>(result.rows) * result.buttonHeight +
        static_cast<std::int64_t>(std::max(0, result.rows - 1)) * result.gap;
    const int boundedFooterHeight = static_cast<int>(std::min<std::int64_t>(height, footerHeight));
    result.bounds = {0, height - boundedFooterHeight, width, height};

    const auto layoutGroup = [&](const std::vector<std::size_t>& indices, int row, FooterSide side) {
        if (indices.empty()) return;
        const int count = static_cast<int>(indices.size());
        const int actualGap = count > 1
            ? std::min(result.gap, std::max(0, (available - count) / (count - 1)))
            : 0;
        const int widthBudget = std::max(0, available - actualGap * (count - 1));
        const int minimumWidth = widthBudget >= count ? 1 : 0;
        std::vector<int> widths(indices.size(), minimumWidth);
        int remaining = widthBudget - minimumWidth * count;
        const auto allocate = [&](std::size_t position) {
            const int desired = std::max(minimumWidth, buttons[indices[position]].preferredWidth);
            const int extra = std::min(remaining, desired - minimumWidth);
            widths[position] += extra;
            remaining -= extra;
        };
        if (side == FooterSide::leading) {
            for (std::size_t i = 0; i < indices.size(); ++i) allocate(i);
        } else {
            for (std::size_t i = indices.size(); i-- > 0;) allocate(i);
        }
        int groupWidth = actualGap * (count - 1);
        for (int buttonWidth : widths) groupWidth += buttonWidth;
        int x = side == FooterSide::leading ? result.margin : width - result.margin - groupWidth;
        const int y = std::max(0, result.bounds.top + result.margin) +
            row * (result.buttonHeight + result.gap);
        for (std::size_t i = 0; i < indices.size(); ++i) {
            result.buttons[indices[i]] = {x, y, x + widths[i], y + result.buttonHeight};
            x += widths[i] + actualGap;
        }
    };

    layoutGroup(leading, 0, FooterSide::leading);
    layoutGroup(trailing, wraps ? 1 : 0, FooterSide::trailing);
    return result;
}
Size defaultEditorClientSize(int dpi) { return {scaleForDpi(1080, dpi), scaleForDpi(560, dpi)}; }
Size clampToWorkArea(Size desired, Rect workArea, int /*dpi*/) {
    return {std::clamp(desired.width, 0, extent(workArea.left, workArea.right)),
            std::clamp(desired.height, 0, extent(workArea.top, workArea.bottom))};
}
Rect centerInWorkArea(Size size, Rect workArea) {
    size = clampToWorkArea(size, workArea, 96);
    const int x = workArea.left + (extent(workArea.left, workArea.right) - size.width) / 2;
    const int y = workArea.top + (extent(workArea.top, workArea.bottom) - size.height) / 2;
    return {x, y, x + size.width, y + size.height};
}
EditorLayout computeEditorLayout(Size client, int dpi, bool editable) {
    const int width = std::max(0, client.width);
    const int height = std::max(0, client.height);
    const int margin = std::min({scaleForDpi(width < scaleForDpi(700, dpi) ? 16 : 32, dpi), width / 8, height / 8});
    const std::array specs{
        FooterButtonSpec{scaleForDpi(104, dpi), FooterSide::leading, editable},
        FooterButtonSpec{scaleForDpi(104, dpi), FooterSide::trailing, editable},
        FooterButtonSpec{scaleForDpi(104, dpi), FooterSide::trailing, true}};
    const auto footer = computeButtonFooterLayout(client, dpi, specs);
    EditorLayout layout;
    layout.resetButton = footer.buttons[0];
    layout.cancelButton = footer.buttons[1];
    layout.saveButton = footer.buttons[2];
    // WM_SIZE can transiently report less than the supported minimum height.
    // Keep the shared footer's rows, but clip their bounds to that client.
    for (auto* button : {&layout.resetButton, &layout.cancelButton, &layout.saveButton}) {
        button->top = std::min(button->top, height);
        button->bottom = std::min(button->bottom, height);
    }
    layout.footer = footer.bounds;
    const int textBottom = std::max(0, layout.footer.top - margin / 2);
    const int headerTop = std::min({scaleForDpi(16, dpi), margin, textBottom});
    const int headerSpace = std::min(scaleForDpi(editable ? 72 : 44, dpi),
        std::max(0, textBottom - headerTop));
    layout.heading = {margin, headerTop, width - margin,
        headerTop + std::min(scaleForDpi(36, dpi), headerSpace)};
    if (editable) {
        const int pathTop = std::min(headerTop+scaleForDpi(40,dpi),headerTop+headerSpace);
        layout.path = {margin,pathTop,width-margin,std::min(pathTop+scaleForDpi(24,dpi),headerTop+headerSpace)};
    }
    const int textTop = headerTop + headerSpace;
    layout.textArea = {margin, textTop, std::max(margin, width - margin), std::max(textTop, textBottom)};
    if (!editable) layout.tabs = layout.textArea;
    return layout;
}
Rect exposedParentRegion(Size /*oldClient*/, Size newClient,
                         const EditorLayout& /*oldLayout*/, const EditorLayout& /*newLayout*/) {
    // Conservative bounding rectangle of every exposed parent pixel. Together
    // with WS_CLIPCHILDREN this repaints all gaps without painting over controls.
    return {0, 0, std::max(0, newClient.width), std::max(0, newClient.height)};
}
}  // namespace phpvpn::tray
