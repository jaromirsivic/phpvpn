#include "core/wizard/http_diagnostic.h"
#include <algorithm>
#include <limits>
#include <map>
#include <set>

namespace phpvpn {
namespace {
void stripFraming(std::string& block) {
    while (block.ends_with("\r\n")) block.resize(block.size() - 2);
}
struct HeaderLine {
    std::string_view text;
    std::string name;
    std::string_view value;
};
std::vector<HeaderLine> headerLines(std::string_view block) {
    std::vector<HeaderLine> lines;
    for (std::size_t start = 0; start < block.size();) {
        const auto end = block.find("\r\n", start);
        const auto line = block.substr(start, end == std::string_view::npos ? block.size() - start : end - start);
        const auto colon = line.find(':');
        if (colon != std::string_view::npos) {
            std::string name(line.substr(0, colon));
            for (char& c : name) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
            auto value = line.substr(colon + 1);
            while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) value.remove_prefix(1);
            while (!value.empty() && (value.back() == ' ' || value.back() == '\t')) value.remove_suffix(1);
            lines.push_back({line, std::move(name), value});
        }
        if (end == std::string_view::npos) break;
        start = end + 2;
    }
    return lines;
}
}
DiagnosticRequestHeaders reconstructDiagnosticRequestHeaders(std::optional<std::string> raw, std::string_view application) {
    DiagnosticRequestHeaders out;
    if (raw) {
        out.block = std::move(*raw); out.source = RequestHeadersSource::winhttp;
        if (out.block.starts_with("POST ")) {
            const auto end = out.block.find("\r\n");
            const auto position = out.block.substr(0, end).rfind(" HTTP/");
            if (position != std::string::npos) out.version = out.block.substr(position + 1, end - position - 1);
            out.block.erase(0, end == std::string::npos ? out.block.size() : end + 2);
        }
        stripFraming(out.block);
        const auto rawLines = headerLines(out.block);
        const auto applicationLines = headerLines(application);
        std::map<std::string, std::vector<std::string_view>> actual, expected;
        for (const auto& line : rawLines) actual[line.name].push_back(line.value);
        for (const auto& line : applicationLines) expected[line.name].push_back(line.value);
        std::set<std::string> incomplete;
        for (const auto& [name, values] : expected)
            if (actual[name] != values) incomplete.insert(name);
        if (!incomplete.empty()) {
            std::string merged;
            const auto append = [&](std::string_view line) {
                if (!merged.empty()) merged += "\r\n";
                merged += line;
            };
            // Reconstruct entire affected groups. Never guess which duplicate
            // was omitted, or invent a system value for a colliding name.
            for (const auto& line : rawLines) if (!incomplete.contains(line.name)) append(line.text);
            for (const auto& line : applicationLines) if (incomplete.contains(line.name)) append(line.text);
            out.block = std::move(merged);
            out.source = RequestHeadersSource::merged;
        }
    } else out.block = application;
    stripFraming(out.block);
    return out;
}
DiagnosticResponseHeaders reconstructDiagnosticResponseHeaders(std::optional<std::string> raw, std::optional<unsigned> status,
    const std::vector<std::pair<std::string, std::string>>& available) {
    DiagnosticResponseHeaders out;
    if (raw && raw->starts_with("HTTP/")) {
        out.block = std::move(*raw); out.source = ResponseHeadersSource::winhttp;
    } else {
        out.block = "HTTP/? " + (status ? std::to_string(*status) : "[status unavailable]");
        if (raw) stripFraming(*raw);
        if (raw && !raw->empty()) {
            out.block += "\r\n" + *raw; out.source = ResponseHeadersSource::available;
        } else for (const auto& [name, value] : available) {
            out.block += "\r\n" + name + ": " + value; out.source = ResponseHeadersSource::available;
        }
    }
    stripFraming(out.block); out.block += "\r\n\r\n";
    return out;
}
std::optional<std::uint64_t> checkedDiagnosticSize(std::uint64_t current, std::uint64_t added) {
    if (added > (std::numeric_limits<std::uint64_t>::max)() - current) return {};
    return current + added;
}
bool BoundedHttpCapture::append(std::string_view bytes) {
    const auto next = checkedDiagnosticSize(size_, bytes.size());
    if (!next) return false;
    prefix_.append(bytes.substr(0, kHttpDiagnosticLimit - prefix_.size()));
    if (bytes.size() >= kHttpDiagnosticTail) tail_.assign(bytes.substr(bytes.size() - kHttpDiagnosticTail));
    else {
        tail_.append(bytes);
        if (tail_.size() > kHttpDiagnosticTail) tail_.erase(0, tail_.size() - kHttpDiagnosticTail);
    }
    size_ = *next;
    return true;
}
BoundedHttpMessage BoundedHttpCapture::finish() const {
    return {size_, size_ <= kHttpDiagnosticLimit ? prefix_ : prefix_.substr(0, kHttpDiagnosticPrefix),
        size_ <= kHttpDiagnosticLimit ? std::string{} : tail_};
}
bool validHttpMessage(const BoundedHttpMessage& m) {
    return m.originalSizeBytes && (m.truncated()
        ? m.prefix.size() == kHttpDiagnosticPrefix && m.suffix.size() == kHttpDiagnosticTail
        : m.prefix.size() == m.originalSizeBytes && m.suffix.empty());
}
std::optional<BoundedHttpMessage> prependHttpHeaders(std::string_view h, const BoundedHttpMessage& b) {
    const auto next = checkedDiagnosticSize(b.originalSizeBytes, h.size());
    if (!next) return {};
    BoundedHttpMessage out;
    out.originalSizeBytes = *next;
    const auto count = out.truncated() ? kHttpDiagnosticPrefix : static_cast<std::size_t>(out.originalSizeBytes);
    out.prefix.assign(h.substr(0, count));
    out.prefix.append(b.prefix.substr(0, count - out.prefix.size()));
    if (out.truncated()) {
        const auto& last = b.truncated() ? b.suffix : b.prefix;
        if (last.size() >= kHttpDiagnosticTail) out.suffix = last.substr(last.size() - kHttpDiagnosticTail);
        else { out.suffix = std::string(h.substr(h.size() - (kHttpDiagnosticTail - last.size()))); out.suffix += last; }
    }
    return out;
}
std::string encodeDiagnosticOctets(std::string_view bytes) {
    std::string out;
    out.reserve(bytes.size() * 2);
    for (unsigned char b : bytes) {
        if (b < 128) out.push_back(static_cast<char>(b));
        else { out.push_back(static_cast<char>(0xc0 | (b >> 6))); out.push_back(static_cast<char>(0x80 | (b & 63))); }
    }
    return out;
}
std::optional<std::string> decodeDiagnosticOctets(std::string_view text, std::size_t expected) {
    if (expected > kHttpDiagnosticLimit || text.size() > expected * 2) return {};
    std::string out;
    for (std::size_t i = 0; i < text.size();) {
        const auto b = static_cast<unsigned char>(text[i++]);
        if (b < 128) out.push_back(static_cast<char>(b));
        else {
            if ((b != 0xc2 && b != 0xc3) || i == text.size()) return {};
            const auto c = static_cast<unsigned char>(text[i++]);
            if ((c & 0xc0) != 0x80) return {};
            out.push_back(static_cast<char>(((b & 3) << 6) | (c & 63)));
        }
        if (out.size() > expected) return {};
    }
    if (out.size() != expected) return {};
    return out;
}
namespace {
void displaySegment(std::string_view bytes, DiagnosticDisplay& out) {
    constexpr wchar_t hex[] = L"0123456789ABCDEF";
    for (std::size_t i = 0; i < bytes.size();) {
        const auto b = static_cast<unsigned char>(bytes[i]);
        std::uint32_t cp = b;
        std::size_t n = b < 128 ? 1 : b >= 0xc2 && b <= 0xdf ? 2 : b >= 0xe0 && b <= 0xef ? 3 : b >= 0xf0 && b <= 0xf4 ? 4 : 0;
        bool valid = n && n <= bytes.size() - i;
        if (valid && n > 1) {
            cp = b & (0x7f >> n);
            for (std::size_t j = 1; j < n; ++j) {
                const auto c = static_cast<unsigned char>(bytes[i + j]);
                if ((c & 0xc0) != 0x80) valid = false;
                cp = (cp << 6) | (c & 63);
            }
            if ((n == 2 && cp < 0x80) || (n == 3 && cp < 0x800) || (n == 4 && cp < 0x10000) ||
                cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) valid = false;
        }
        if (!valid || (b < 32 && b != '\t' && b != '\r' && b != '\n') || b == 127) {
            out.text += L"\\x"; out.text += hex[b >> 4]; out.text += hex[b & 15]; out.escaped = true; ++i;
        } else {
            if (cp <= 0xffff) out.text.push_back(static_cast<wchar_t>(cp));
            else { cp -= 0x10000; out.text.push_back(static_cast<wchar_t>(0xd800 + (cp >> 10))); out.text.push_back(static_cast<wchar_t>(0xdc00 + (cp & 1023))); }
            i += n;
        }
    }
}
}
DiagnosticDisplay diagnosticDisplayText(const BoundedHttpMessage& m) {
    DiagnosticDisplay out;
    if (!validHttpMessage(m)) return out;
    displaySegment(m.prefix, out);
    if (m.truncated()) { out.text.append(kHttpDiagnosticSeparator.begin(), kHttpDiagnosticSeparator.end()); displaySegment(m.suffix, out); }
    return out;
}
std::string diagnosticSizeKiB(std::uint64_t bytes) {
    auto whole = bytes / 1024;
    auto hundredths = (100 * (bytes % 1024) + 512) / 1024;
    if (hundredths == 100) { ++whole; hundredths = 0; }
    return std::to_string(whole) + "." + (hundredths < 10 ? "0" : "") + std::to_string(hundredths);
}
}
