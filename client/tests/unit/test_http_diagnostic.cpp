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
    // \U0001F600 is the surrogate pair D83D DE00 with a UTF-16 wchar_t and one
    // code unit with a UTF-32 wchar_t.
    PHPVPN_CHECK_EQ(display.text, L"\\x00\\x7F\\x01\t\r\n\u010d\U0001F600\\xC0\\xAF\\xED\\xA0\\x80\\xE2\\x82");
    PHPVPN_CHECK(display.escaped);
    for (const std::string& text : {std::string("\xc4\x8d"), std::string("\xe2\x82\xac"), std::string("\xf0\x9f\x98\x80")}) {
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
PHPVPN_TEST(http_diagnostic, display_code_units_follow_the_wchar_width) {
    // U+10000, U+1F600 and U+10FFFF, the first, a typical and the last
    // supplementary code point, around one BMP character.
    const std::string bytes = "\xf0\x90\x80\x80" "A" "\xf0\x9f\x98\x80" "\xf4\x8f\xbf\xbf";
    const auto display = diagnosticDisplayText({bytes.size(), bytes, {}});
    PHPVPN_CHECK(!display.escaped);
    std::wstring expected;
    if constexpr (sizeof(wchar_t) == 2) {
        for (const unsigned unit : {0xd800u, 0xdc00u, 0x41u, 0xd83du, 0xde00u, 0xdbffu, 0xdfffu})
            expected.push_back(static_cast<wchar_t>(unit));
    } else {
        for (const unsigned point : {0x10000u, 0x41u, 0x1f600u, 0x10ffffu})
            expected.push_back(static_cast<wchar_t>(point));
    }
    PHPVPN_CHECK_EQ(display.text, expected);
    // The display limit is the UTF-16 worst case, all escapes, so the shorter
    // UTF-32 projection of supplementary characters always stays below it.
    std::string astral;
    while (astral.size() + 4 <= 64512) astral += "\xf0\x9f\x98\x80";
    BoundedHttpMessage wide{65537, astral + std::string(64512 - astral.size(), 'a'), std::string(1024, 'b')};
    PHPVPN_CHECK(diagnosticDisplayText(wide).text.size() < kHttpDiagnosticDisplayLimit);
}
}

namespace phpvpn {
PHPVPN_TEST(http_diagnostic, reconstructs_entire_incomplete_duplicate_groups_without_claiming_raw) {
    const std::string application = "Content-Type: application/json\r\nX: first\r\nx: second\r\nX: first\r\nY: \t own \t";
    for (const std::string raw : {"Host: relay\r\nX: first", "Host: relay\r\nX: first\r\nx: second",
         "Host: relay\r\nX: first, second, first", "Host: relay\r\nX: changed"}) {
        const auto result = reconstructDiagnosticRequestHeaders(raw, application);
        PHPVPN_CHECK_EQ(result.source, RequestHeadersSource::merged);
        PHPVPN_CHECK_EQ(result.block, "Host: relay\r\n" + application);
        PHPVPN_CHECK_EQ(result.version, "HTTP/?");
    }
    const auto identical = reconstructDiagnosticRequestHeaders("X: same", "X: same\r\nX: same");
    PHPVPN_CHECK_EQ(identical.block, "X: same\r\nX: same");
    PHPVPN_CHECK_EQ(identical.source, RequestHeadersSource::merged);
    const std::string complete = "POST / HTTP/2\r\nHost: relay\r\ncontent-type:\tapplication/json \r\nx: first\r\nX: second\r\nx: first\r\ny: own\r\n\r\n";
    const auto result = reconstructDiagnosticRequestHeaders(complete, application);
    PHPVPN_CHECK_EQ(result.source, RequestHeadersSource::winhttp);
    PHPVPN_CHECK_EQ(result.version, "HTTP/2");
    PHPVPN_CHECK_EQ(result.block, complete.substr(15, complete.size() - 19));
    const auto absent = reconstructDiagnosticRequestHeaders({}, application);
    PHPVPN_CHECK_EQ(absent.source, RequestHeadersSource::application);
    PHPVPN_CHECK_EQ(absent.block, application);
    PHPVPN_CHECK(absent.block.find("PHPVPN/1.0") == std::string::npos);
}
}
