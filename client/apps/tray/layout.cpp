#include "layout.h"
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
    const int gap = std::min(scaleForDpi(8, dpi), width / 8);
    const int buttonWidth = std::min(scaleForDpi(104, dpi), std::max(0, (width - 2 * margin - 2 * gap) / 3));
    const int buttonHeight = std::min(scaleForDpi(36, dpi), std::max(0, height - 2 * margin));
    EditorLayout layout;
    layout.saveButton = {width - margin - buttonWidth, height - margin - buttonHeight,
                         width - margin, height - margin};
    if (editable) {
        layout.cancelButton = {layout.saveButton.left - gap - buttonWidth, layout.saveButton.top,
                             layout.saveButton.left - gap, layout.saveButton.bottom};
        layout.resetButton = {margin, layout.saveButton.top, margin + buttonWidth, layout.saveButton.bottom};
    }
    layout.footer = {0, std::max(0, layout.saveButton.top - margin / 2), width, height};
    const int textBottom = std::max(margin, layout.footer.top - margin / 2);
    const int headerTop = std::min(scaleForDpi(16, dpi), margin);
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
    return layout;
}
Rect exposedParentRegion(Size /*oldClient*/, Size newClient,
                         const EditorLayout& /*oldLayout*/, const EditorLayout& /*newLayout*/) {
    // Conservative bounding rectangle of every exposed parent pixel. Together
    // with WS_CLIPCHILDREN this repaints all gaps without painting over controls.
    return {0, 0, std::max(0, newClient.width), std::max(0, newClient.height)};
}
}  // namespace phpvpn::tray
