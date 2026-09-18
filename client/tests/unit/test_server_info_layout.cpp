#include "../../apps/tray/server_info_layout.h"
#include "harness.h"
namespace phpvpn {
PHPVPN_TEST(ServerInfo, layout_dpi_small_screen_and_footer) {
    using namespace tray;
    for (const int dpi : {96, 120, 144, 192}) {
        const auto p = [dpi](int value) { return scaleForDpi(value, dpi); };
        for (const int width : {320, 640, 1080}) {
            for (const int count : {0, 7, 13}) {
                auto layout = computeServerInfoLayout({p(width), p(360)}, dpi, p(64), p(40), count);
                PHPVPN_CHECK(layout.heading.right <= p(width));
                PHPVPN_CHECK(layout.heading.bottom <= layout.tabs.top);
                PHPVPN_CHECK(layout.tabs.bottom <= layout.content.top);
                PHPVPN_CHECK(layout.content.bottom > layout.content.top);
                PHPVPN_CHECK(layout.content.bottom <= layout.pager.top);
                PHPVPN_CHECK(layout.pager.bottom <= layout.notice.top);
                PHPVPN_CHECK(layout.notice.bottom <= layout.refresh.top);
                PHPVPN_CHECK(layout.refresh.right < layout.autoRefresh.left);
                PHPVPN_CHECK_EQ(layout.refresh.top, layout.autoRefresh.top);
                PHPVPN_CHECK(layout.autoRefresh.right <= p(width - 16));
                PHPVPN_CHECK(layout.autoRefresh.right < layout.ok.left || layout.autoRefresh.bottom < layout.ok.top);
                PHPVPN_CHECK(layout.ok.bottom <= p(360));
                PHPVPN_CHECK(layout.pagerButtonWidth * count <= layout.pager.right - layout.pager.left);
            }
        }
    }
}
} // namespace phpvpn
