// GTK-free texts of the two windows owned by the Configuration Wizard (WP10a):
// the HTTP error detail notes and pages (apps/tray/http_error_detail_window.cpp)
// and the HTTP headers dialog display and error texts (http_headers_window.cpp).
#pragma once
#include <array>
#include <optional>
#include <string>
#include <string_view>

#include "core/base/result.h"
#include "core/wizard/http_diagnostic.h"

namespace phpvpn::tray {
struct HttpDetailPage {
    // Explanation shown above the raw text (size, truncation, escaping, sources).
    std::string note;
    // Raw HTTP message as UTF-8, invalid or non-displayable bytes as \xHH.
    std::string text;
};
// Request and Response pages. nullopt when a message is malformed or its display
// exceeds kHttpDiagnosticDisplayLimit (the Windows window refuses to open then).
std::optional<std::array<HttpDetailPage, 2>> httpErrorDetailPages(const HttpDiagnostic& diagnostic);

// Display copy of the headers for the dialog: canonical form with LF line ends.
// nullopt when the stored value cannot be normalized (the dialog does not open).
std::optional<std::string> httpHeadersDisplayText(std::string_view value);
// The default headers of the dialog's Reset, in display form.
std::string defaultHttpHeadersDisplayText();
// Error text of a refused OK, as the Windows dialog shows it.
std::string httpHeadersErrorText(const Status& result);
}  // namespace phpvpn::tray
