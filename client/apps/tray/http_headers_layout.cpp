#include "http_headers_layout.h"
#include <array>

namespace phpvpn::tray {
HttpHeadersLayout computeHttpHeadersLayout(Size client, int dpi) {
    const auto p = [dpi](int n) { return scaleForDpi(n, dpi); };
    const std::array specs{FooterButtonSpec{p(88), FooterSide::leading, true},
        FooterButtonSpec{p(88), FooterSide::trailing, true}, FooterButtonSpec{p(88), FooterSide::trailing, true}};
    const auto footer = computeButtonFooterLayout(client, dpi, specs);
    return {{footer.margin, footer.margin, client.width - footer.margin, footer.bounds.top - p(12)},
        footer.buttons[0], footer.buttons[1], footer.buttons[2]};
}
}
