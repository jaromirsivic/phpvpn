#pragma once
#include "layout.h"
#include <algorithm>
#include <array>

namespace phpvpn::tray {
struct ServerInfoLayout {
    Rect heading, tabs, content, pager, notice, refresh, autoRefresh, ok;
    int pagerButtonWidth = 0;
};

// Text heights are measured with the native fonts, in physical pixels.
inline ServerInfoLayout computeServerInfoLayout(Size client, int dpi, int headingHeight, int noticeHeight,
                                                int pagerButtons) {
    const auto p = [dpi](int value) { return scaleForDpi(value, dpi); };
    const std::array buttons{FooterButtonSpec{p(280), FooterSide::leading, true},
                             FooterButtonSpec{p(88), FooterSide::trailing, true}};
    const auto footer = computeButtonFooterLayout(client, dpi, buttons);
    const int left = footer.margin, right = client.width - left;
    ServerInfoLayout result;
    result.refresh = footer.buttons[0];
    result.refresh.right = result.refresh.left + p(104);
    result.autoRefresh = {result.refresh.right + p(8), result.refresh.top,
        footer.buttons[0].right, result.refresh.bottom};
    result.ok = footer.buttons[1];
    result.heading = {left, left, right, left + std::max(p(40), headingHeight)};
    const int tabsTop = result.heading.bottom + p(8);
    result.tabs = {left, tabsTop, right, tabsTop + p(36)};
    const int noticeBottom = footer.bounds.top;
    result.notice = {left, noticeBottom - noticeHeight, right, noticeBottom};
    const int pagerBottom = result.notice.top - p(4);
    result.pager = {left, pagerBottom - (pagerButtons ? p(32) : 0), right, pagerBottom};
    result.content = {left, result.tabs.bottom + p(8), right, result.pager.top - p(8)};
    result.content.bottom = std::max(result.content.top + 1, result.content.bottom);
    result.pagerButtonWidth = pagerButtons ? std::min(p(38), (right - left) / pagerButtons) : 0;
    return result;
}
} // namespace phpvpn::tray
