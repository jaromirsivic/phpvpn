#include "wizard_dialog_text.h"

#include "core/base/utf8.h"
#include "core/config/config.h"
#include "core/protocol/http_headers.h"

namespace phpvpn::tray {
namespace {
std::string description(const char* title, const BoundedHttpMessage& message, bool escaped) {
    auto out = std::string(title) + " (Original size: " + diagnosticSizeKiB(message.originalSizeBytes) + " KiB)";
    if (message.truncated())
        out += "\nTruncated: first 63 KiB and last 1 KiB, " + std::to_string(message.originalSizeBytes - 65536) +
               " bytes omitted. The four dotted lines are a display separator.";
    if (escaped) out += "\nInvalid or non-displayable bytes are shown as \\xHH.";
    return out;
}
std::string lineFeeds(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\r' && i + 1 < text.size() && text[i + 1] == '\n') continue;
        out += text[i];
    }
    return out;
}
}  // namespace

std::optional<std::array<HttpDetailPage, 2>> httpErrorDetailPages(const HttpDiagnostic& d) {
    if (!validHttpMessage(d.request) || !validHttpMessage(d.response)) return std::nullopt;
    const auto request = diagnosticDisplayText(d.request), response = diagnosticDisplayText(d.response);
    // The limit counts UTF-16 units on Windows; UTF-32 code points never exceed them.
    if (request.text.size() > kHttpDiagnosticDisplayLimit || response.text.size() > kHttpDiagnosticDisplayLimit)
        return std::nullopt;
    auto requestText = toUtf8(request.text);
    auto responseText = toUtf8(response.text);
    if (!requestText || !responseText) return std::nullopt;
    std::array<HttpDetailPage, 2> pages;
    pages[0].note = description("Request", d.request, request.escaped);
    pages[0].note += "\nApplication reconstruction.";
    if (d.request.prefix.find(" HTTP/?\r\n") != std::string::npos) pages[0].note += " HTTP version unavailable (HTTP/?).";
    if (d.requestHeadersSource == RequestHeadersSource::application)
        pages[0].note += "\nSystem request headers unavailable. Showing application headers.";
    else if (d.requestHeadersSource == RequestHeadersSource::merged)
        pages[0].note += "\nSystem headers merged with original application headers.";
    pages[1].note = description("Response", d.response, response.escaped);
    if (!d.responseComplete) pages[1].note += "\nIncomplete response: size refers to received content.";
    if (d.responseHeadersSource != ResponseHeadersSource::winhttp)
        pages[1].note += "\nComplete system response headers unavailable. Showing available information.";
    pages[0].text = std::move(requestText).value();
    pages[1].text = std::move(responseText).value();
    return pages;
}

std::optional<std::string> httpHeadersDisplayText(std::string_view value) {
    // Normalize only the display copy. Cancel preserves the original raw field.
    auto normalized = normalizeHttpHeaders(value);
    if (!normalized) return std::nullopt;
    return lineFeeds(normalized.value());
}

std::string defaultHttpHeadersDisplayText() { return lineFeeds(kDefaultHttpHeaders); }

std::string httpHeadersErrorText(const Status& result) {
    if (result) return {};
    if (result.code() == ErrorCode::config_too_large)
        return "The HTTP headers cannot fit in the configuration file (512 KiB limit).";
    if (result.code() == ErrorCode::config_encoding_error) return "The text contains an invalid Unicode character.";
    return httpHeadersErrorDescription(result.error());
}
}  // namespace phpvpn::tray
