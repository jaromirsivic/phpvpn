#include "core/protocol/http_headers.h"
#include <unordered_set>
#include <charconv>
#include <algorithm>
#include <limits>

namespace phpvpn {
namespace {
bool token(unsigned char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') || std::string_view("!#$%&'*+-.^_`|~").find(static_cast<char>(c)) != std::string_view::npos;
}
Error invalid(std::size_t line, const char* reason) {
    return Error(ErrorCode::config_invalid_value,
        "masterSetup.httpHeaders line " + std::to_string(line) + ": " + reason);
}
}
bool isHttpHeadersErrorDescription(std::string_view description) {
    if (description == "masterSetup.httpHeaders is required" || description == "masterSetup.httpHeaders must be a string") return true;
    constexpr std::string_view prefix = "masterSetup.httpHeaders line ";
    if (!description.starts_with(prefix)) return false;
    description.remove_prefix(prefix.size());
    const auto end = description.find(": ");
    if (end == std::string_view::npos || end == 0) return false;
    std::size_t line = 0;
    const auto number = std::from_chars(description.data(), description.data() + end, line);
    if (number.ec != std::errc{} || number.ptr != description.data() + end || line == 0) return false;
    description.remove_prefix(end + 2);
    for (const auto reason : {"header block too large", "at least one header is required", "empty line or missing colon",
         "bare CR is forbidden", "invalid field name", "forbidden control octet"})
        if (description == reason) return true;
    for (std::string_view suffix : {" is managed by the application or transport", " is a duplicate header name"}) {
        if (!description.ends_with(suffix)) continue;
        const auto name = description.substr(0, description.size() - suffix.size());
        return !name.empty() && std::all_of(name.begin(), name.end(), [](unsigned char c) { return token(c); });
    }
    return false;
}
std::string httpHeadersErrorDescription(const Error& error) {
    if (error.code == ErrorCode::config_invalid_value) {
        if (error.detail == "missing httpHeaders in masterSetup") return "masterSetup.httpHeaders is required";
        if (error.detail == "httpHeaders must be a string in masterSetup") return "masterSetup.httpHeaders must be a string";
        if (isHttpHeadersErrorDescription(error.detail)) return error.detail;
    }
    return errorCodeDescription(error.code);
}
Result<std::string> normalizeHttpHeaders(std::string_view input) {
    // UTF-8 conversion uses an int length. Reserve the longest application
    // Content-Type prefix as well, before LF expansion can reach that limit.
    constexpr auto prefixSize = sizeof("Content-Type: application/x-ndjson; charset=utf-8\r\n") - 1;
    constexpr auto limit = static_cast<std::size_t>((std::numeric_limits<int>::max)()) - prefixSize;
    if (input.size() > limit) return invalid(1, "header block too large");
    if (input.empty()) return invalid(1, "at least one header is required");
    std::string output;
    output.reserve(input.size());
    std::size_t line = 1, nameSize = 0, nameStart = 0;
    bool value = false;
    std::unordered_set<std::string> names;
    for (std::size_t i = 0; i < input.size(); ++i) {
        const auto c = static_cast<unsigned char>(input[i]);
        if (c == '\r' || c == '\n') {
            if (!value) return invalid(line, "empty line or missing colon");
            if (c == '\r' && (i + 1 == input.size() || input[++i] != '\n'))
                return invalid(line, "bare CR is forbidden");
            if (i + 1 == input.size()) return output;
            if (output.size() > limit - 2) return invalid(line, "header block too large");
            output += "\r\n";
            ++line;
            nameSize = 0;
            nameStart = i + 1;
            value = false;
            continue;
        }
        if (!value) {
            if (c == ':' && nameSize != 0) {
                const auto name = input.substr(nameStart, nameSize);
                for (const auto owned : {std::string_view("Content-Type"), std::string_view("Content-Length"),
                     std::string_view("Connection"), std::string_view("Host"), std::string_view("Transfer-Encoding")}) {
                    if (name.size() != owned.size()) continue;
                    bool equal = true;
                    for (std::size_t n = 0; n < name.size(); ++n) {
                        const auto lower = [](char ch) { return ch >= 'A' && ch <= 'Z' ? ch + ('a' - 'A') : ch; };
                        if (lower(name[n]) != lower(owned[n])) { equal = false; break; }
                    }
                    if (equal) return Error(ErrorCode::config_invalid_value,
                        "masterSetup.httpHeaders line " + std::to_string(line) + ": " + std::string(name) +
                        " is managed by the application or transport");
                }
                std::string identity(name);
                for (char& ch : identity) if (ch >= 'A' && ch <= 'Z') ch += 'a' - 'A';
                if (!names.insert(std::move(identity)).second) return Error(ErrorCode::config_invalid_value,
                    "masterSetup.httpHeaders line " + std::to_string(line) + ": " + std::string(name) +
                    " is a duplicate header name");
                value = true;
            }
            else if (!token(c)) return invalid(line, "invalid field name");
            else ++nameSize;
        } else if ((c < 0x20 && c != '\t') || c == 0x7f) {
            return invalid(line, "forbidden control octet");
        }
        if (output.size() == limit) return invalid(line, "header block too large");
        output += static_cast<char>(c);
    }
    if (!value) return invalid(line, "empty line or missing colon");
    return output;
}
std::string buildApplicationRequestHeaders(std::string_view contentType, std::string_view normalizedHeaders) {
    std::string result = "Content-Type: ";
    result += contentType;
    result += "\r\n";
    result += normalizedHeaders;
    return result;
}
}
