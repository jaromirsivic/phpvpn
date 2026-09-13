#include "harness.h"
#include "core/wizard/http_diagnostic.h"
#include <limits>

namespace phpvpn {
PHPVPN_TEST(http_diagnostic, header_reconstruction_preserves_duplicates_and_explicit_fallbacks) {
    const std::string application = "Content-Type: application/json\r\nX-App: original\r\n";
    const std::string raw = "POST /relay?action=health HTTP/1.1\r\nHost: relay.example\r\nX-Dup: one\r\nX-Dup: two\r\ncontent-type: application/json\r\nX-App: original\r\n\r\n";
    const auto complete = reconstructDiagnosticRequestHeaders(raw, application);
    PHPVPN_CHECK_EQ(complete.source, RequestHeadersSource::winhttp);
    PHPVPN_CHECK_EQ(complete.version, "HTTP/1.1");
    PHPVPN_CHECK_EQ(complete.block, raw.substr(raw.find("\r\n") + 2, raw.size() - raw.find("\r\n") - 6));
    const auto merged = reconstructDiagnosticRequestHeaders("X-Dup: one\r\nX-Dup: two\r\n", application);
    PHPVPN_CHECK_EQ(merged.source, RequestHeadersSource::merged);
    PHPVPN_CHECK_EQ(merged.version, "HTTP/?");
    PHPVPN_CHECK_EQ(merged.block, "X-Dup: one\r\nX-Dup: two\r\nContent-Type: application/json\r\nX-App: original");
    const auto fallback = reconstructDiagnosticRequestHeaders({}, application);
    PHPVPN_CHECK_EQ(fallback.source, RequestHeadersSource::application);
    PHPVPN_CHECK_EQ(fallback.version, "HTTP/?");
    PHPVPN_CHECK_EQ(fallback.block, application.substr(0, application.size() - 2));
    PHPVPN_CHECK(fallback.block.find("Host:") == std::string::npos);
    const auto absent = reconstructDiagnosticResponseHeaders({}, {}, {});
    PHPVPN_CHECK_EQ(absent.source, ResponseHeadersSource::unavailable);
    PHPVPN_CHECK_EQ(absent.block, "HTTP/? [status unavailable]\r\n\r\n");
    const auto numeric = reconstructDiagnosticResponseHeaders({}, 503, {{"Content-Type", "text/html"}, {"X-Dup", "one"}, {"X-Dup", "two"}});
    PHPVPN_CHECK_EQ(numeric.source, ResponseHeadersSource::available);
    PHPVPN_CHECK_EQ(numeric.block, "HTTP/? 503\r\nContent-Type: text/html\r\nX-Dup: one\r\nX-Dup: two\r\n\r\n");
    const auto withoutStatusLine = reconstructDiagnosticResponseHeaders("X-Dup: one\r\nX-Dup: two\r\n\r\n", 204, {});
    PHPVPN_CHECK_EQ(withoutStatusLine.block, "HTTP/? 204\r\nX-Dup: one\r\nX-Dup: two\r\n\r\n");
    const std::string response = "HTTP/2 200\r\nX-Dup: one\r\nX-Dup: two\r\n\r\n";
    const auto system = reconstructDiagnosticResponseHeaders(response, {}, {});
    PHPVPN_CHECK_EQ(system.source, ResponseHeadersSource::winhttp);
    PHPVPN_CHECK_EQ(system.block, response);
}
PHPVPN_TEST(http_diagnostic, exact_streaming_slices_and_header_composition) {
    for (std::size_t n : {0u, 1u, 64511u, 64512u, 65535u, 65536u, 65537u, 66560u, 2000000u}) {
        std::string bytes(n, ' ');
        for (std::size_t i = 0; i < n; ++i) bytes[i] = static_cast<char>(i % 251);
        for (std::size_t block : {1u, 997u, 65536u}) {
            BoundedHttpCapture capture;
            for (std::size_t i = 0; i < n; i += block) PHPVPN_CHECK(capture.append(std::string_view(bytes).substr(i, block)));
            const auto result = capture.finish();
            PHPVPN_CHECK_EQ(result.originalSizeBytes, n);
            PHPVPN_CHECK_EQ(result.prefix, bytes.substr(0, n <= 65536 ? n : 64512));
            PHPVPN_CHECK_EQ(result.suffix, n <= 65536 ? std::string{} : bytes.substr(n - 1024));
            for (std::size_t hn : {0u, 39u, 65537u}) {
                const std::string headers(hn, 'H');
                const auto joined = prependHttpHeaders(headers, result);
                PHPVPN_CHECK(joined.has_value());
                const auto full = headers + bytes;
                PHPVPN_CHECK_EQ(joined->prefix, full.substr(0, full.size() <= 65536 ? full.size() : 64512));
                PHPVPN_CHECK_EQ(joined->suffix, full.size() <= 65536 ? std::string{} : full.substr(full.size() - 1024));
            }
        }
    }
    PHPVPN_CHECK_EQ(kHttpDiagnosticSeparator, "\r\n...\r\n...\r\n...\r\n...\r\n");
    PHPVPN_CHECK_EQ(kHttpDiagnosticSeparator.size(), 22u);
    BoundedHttpMessage maximum{(std::numeric_limits<std::uint64_t>::max)(), std::string(64512, 'a'), std::string(1024, 'b')};
    PHPVPN_CHECK(!prependHttpHeaders("x", maximum));
    PHPVPN_CHECK(!checkedDiagnosticSize(maximum.originalSizeBytes, 1));
    const auto largest = checkedDiagnosticSize(maximum.originalSizeBytes - 1, 1);
    const auto zero = checkedDiagnosticSize(0, 0);
    PHPVPN_CHECK(largest && *largest == maximum.originalSizeBytes);
    PHPVPN_CHECK(zero && *zero == 0);
}
PHPVPN_TEST(http_diagnostic, octets_and_safe_unicode_are_deterministic) {
    std::string all;
    for (int i = 0; i < 256; ++i) all += static_cast<char>(i);
    const auto decoded = decodeDiagnosticOctets(encodeDiagnosticOctets(all), 256);
    PHPVPN_CHECK(decoded && *decoded == all);
    PHPVPN_CHECK(!decodeDiagnosticOctets("\xc4\x80", 1));
    PHPVPN_CHECK(!decodeDiagnosticOctets("\xc0\x80", 1));
    const std::string bytes = std::string("\0\x7f\x01", 3) + "\t\r\n\xc4\x8d\xf0\x9f\x98\x80\xc0\xaf\xed\xa0\x80\xe2\x82";
    auto display = diagnosticDisplayText({bytes.size(), bytes, {}});
    PHPVPN_CHECK_EQ(display.text, L"\\x00\\x7F\\x01\t\r\n\u010d\xd83d\xde00\\xC0\\xAF\\xED\\xA0\\x80\\xE2\\x82");
    PHPVPN_CHECK(display.escaped);
    for (const std::string text : {std::string("\xc4\x8d"), std::string("\xe2\x82\xac"), std::string("\xf0\x9f\x98\x80")}) {
        for (std::size_t cut = 1; cut < text.size(); ++cut) {
            BoundedHttpMessage m{65537, std::string(64512 - cut, 'a') + text.substr(0, cut), text.substr(cut) + std::string(1024 - text.size() + cut, 'b')};
            auto projected = diagnosticDisplayText(m);
            PHPVPN_CHECK(projected.escaped);
            PHPVPN_CHECK(projected.text.find(L"\r\n...\r\n...\r\n...\r\n...\r\n") != std::wstring::npos);
        }
    }
    BoundedHttpMessage max{65537, std::string(64512, '\0'), std::string(1024, '\0')};
    PHPVPN_CHECK_EQ(diagnosticDisplayText(max).text.size(), kHttpDiagnosticDisplayLimit);
    PHPVPN_CHECK_EQ(diagnosticSizeKiB(0), "0.00");
    PHPVPN_CHECK_EQ(diagnosticSizeKiB(65536), "64.00");
    PHPVPN_CHECK_EQ(diagnosticSizeKiB(65537), "64.00");
    PHPVPN_CHECK_EQ(diagnosticSizeKiB((std::numeric_limits<std::uint64_t>::max)()), "18014398509481984.00");
}
}
