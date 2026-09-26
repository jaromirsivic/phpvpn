#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include <utility>

namespace phpvpn {
enum class HttpAction;
inline constexpr std::size_t kHttpDiagnosticLimit = 65536;
inline constexpr std::size_t kHttpDiagnosticPrefix = 64512;
inline constexpr std::size_t kHttpDiagnosticTail = 1024;
inline constexpr std::string_view kHttpDiagnosticSeparator = "\r\n...\r\n...\r\n...\r\n...\r\n";
inline constexpr std::size_t kHttpDiagnosticDisplayLimit = 262166;
std::optional<std::uint64_t> checkedDiagnosticSize(std::uint64_t current, std::uint64_t added);
struct BoundedHttpMessage {
    std::uint64_t originalSizeBytes = 0;
    std::string prefix, suffix;
    bool truncated() const { return originalSizeBytes > kHttpDiagnosticLimit; }
};
// `winhttp` means captured by the platform transport (WinHTTP or libcurl). The
// name follows the fixed IPC v1 wire value.
enum class RequestHeadersSource { winhttp, merged, application };
enum class ResponseHeadersSource { winhttp, available, unavailable };
struct DiagnosticRequestHeaders {
    std::string version = "HTTP/?", block;
    RequestHeadersSource source = RequestHeadersSource::application;
};
struct DiagnosticResponseHeaders {
    std::string block;
    ResponseHeadersSource source = ResponseHeadersSource::unavailable;
};
DiagnosticRequestHeaders reconstructDiagnosticRequestHeaders(std::optional<std::string> raw, std::string_view application);
DiagnosticResponseHeaders reconstructDiagnosticResponseHeaders(std::optional<std::string> raw, std::optional<unsigned> status,
    const std::vector<std::pair<std::string, std::string>>& available);
struct HttpDiagnostic {
    HttpAction action{};
    bool responseComplete = false;
    RequestHeadersSource requestHeadersSource = RequestHeadersSource::application;
    ResponseHeadersSource responseHeadersSource = ResponseHeadersSource::unavailable;
    BoundedHttpMessage request, response;
};
class BoundedHttpCapture {
public:
    bool append(std::string_view bytes);
    BoundedHttpMessage finish() const;
private:
    std::uint64_t size_ = 0;
    std::string prefix_, tail_;
};
// Prepend headers without materializing another complete response body.
std::optional<BoundedHttpMessage> prependHttpHeaders(std::string_view headers, const BoundedHttpMessage& body);
bool validHttpMessage(const BoundedHttpMessage& message);
std::string encodeDiagnosticOctets(std::string_view bytes);
std::optional<std::string> decodeDiagnosticOctets(std::string_view text, std::size_t expected);
struct DiagnosticDisplay { std::wstring text; bool escaped = false; };
DiagnosticDisplay diagnosticDisplayText(const BoundedHttpMessage& message);
std::string diagnosticSizeKiB(std::uint64_t bytes);
}
