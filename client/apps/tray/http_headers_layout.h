#pragma once
#include "layout.h"

namespace phpvpn::tray {
struct HttpHeadersLayout { Rect text, reset, cancel, ok; };
HttpHeadersLayout computeHttpHeadersLayout(Size client, int dpi);
}
