#include "core/base/utf8.h"

#include <cstdint>

namespace phpvpn {
namespace {

Error invalidUtf8() { return Error(ErrorCode::invalid_argument, "the text is not valid UTF-8"); }
Error invalidWide() { return Error(ErrorCode::invalid_argument, "the text is not valid Unicode"); }

bool validScalar(std::uint32_t cp) { return cp <= 0x10ffff && (cp < 0xd800 || cp > 0xdfff); }

void appendUtf8(std::string& out, std::uint32_t cp) {
    if (cp < 0x80) {
        out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
        out.push_back(static_cast<char>(0xc0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
    } else if (cp < 0x10000) {
        out.push_back(static_cast<char>(0xe0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3f)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
    } else {
        out.push_back(static_cast<char>(0xf0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3f)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3f)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
    }
}

// Decodes one scalar value starting at `i` and advances `i`. Returns false for
// every malformed form listed in the header.
bool decodeUtf8(std::string_view text, std::size_t& i, std::uint32_t& cp) {
    const auto lead = static_cast<unsigned char>(text[i]);
    std::size_t length = 0;
    if (lead < 0x80) {
        cp = lead;
        ++i;
        return true;
    } else if (lead >= 0xc2 && lead <= 0xdf) {
        length = 2;
        cp = lead & 0x1f;
    } else if (lead >= 0xe0 && lead <= 0xef) {
        length = 3;
        cp = lead & 0x0f;
    } else if (lead >= 0xf0 && lead <= 0xf4) {
        length = 4;
        cp = lead & 0x07;
    } else {
        return false;
    }
    if (length > text.size() - i) return false;
    for (std::size_t j = 1; j < length; ++j) {
        const auto next = static_cast<unsigned char>(text[i + j]);
        if ((next & 0xc0) != 0x80) return false;
        cp = (cp << 6) | (next & 0x3f);
    }
    if ((length == 3 && cp < 0x800) || (length == 4 && cp < 0x10000) || !validScalar(cp)) return false;
    i += length;
    return true;
}

}  // namespace

Result<std::string> utf16ToUtf8(std::u16string_view text) {
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        std::uint32_t cp = text[i];
        if (cp >= 0xd800 && cp <= 0xdbff) {
            if (i + 1 == text.size() || text[i + 1] < 0xdc00 || text[i + 1] > 0xdfff) return invalidWide();
            cp = 0x10000 + ((cp - 0xd800) << 10) + (static_cast<std::uint32_t>(text[++i]) - 0xdc00);
        } else if (cp >= 0xdc00 && cp <= 0xdfff) {
            return invalidWide();
        }
        appendUtf8(out, cp);
    }
    return out;
}

Result<std::string> utf32ToUtf8(std::u32string_view text) {
    std::string out;
    out.reserve(text.size());
    for (const char32_t character : text) {
        const auto cp = static_cast<std::uint32_t>(character);
        if (!validScalar(cp)) return invalidWide();
        appendUtf8(out, cp);
    }
    return out;
}

Result<std::u16string> utf8ToUtf16(std::string_view text) {
    std::u16string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size();) {
        std::uint32_t cp = 0;
        if (!decodeUtf8(text, i, cp)) return invalidUtf8();
        if (cp < 0x10000) {
            out.push_back(static_cast<char16_t>(cp));
        } else {
            cp -= 0x10000;
            out.push_back(static_cast<char16_t>(0xd800 + (cp >> 10)));
            out.push_back(static_cast<char16_t>(0xdc00 + (cp & 0x3ff)));
        }
    }
    return out;
}

Result<std::u32string> utf8ToUtf32(std::string_view text) {
    std::u32string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size();) {
        std::uint32_t cp = 0;
        if (!decodeUtf8(text, i, cp)) return invalidUtf8();
        out.push_back(static_cast<char32_t>(cp));
    }
    return out;
}

Result<std::string> toUtf8(std::wstring_view text) {
    if constexpr (sizeof(wchar_t) == 2) {
        const std::u16string wide(text.begin(), text.end());
        return utf16ToUtf8(wide);
    } else {
        const std::u32string wide(text.begin(), text.end());
        return utf32ToUtf8(wide);
    }
}

Result<std::wstring> fromUtf8(std::string_view text) {
    if constexpr (sizeof(wchar_t) == 2) {
        auto wide = utf8ToUtf16(text);
        if (!wide.ok()) return wide.error();
        return std::wstring(wide->begin(), wide->end());
    } else {
        auto wide = utf8ToUtf32(text);
        if (!wide.ok()) return wide.error();
        return std::wstring(wide->begin(), wide->end());
    }
}

}  // namespace phpvpn
