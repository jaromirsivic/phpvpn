#include "core/protocol/base64url.h"

namespace phpvpn {
namespace {

constexpr char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

// -1 marks every byte outside the base64url alphabet, including `=`, `+`, `/`,
// whitespace and any byte above 0x7F.
constexpr int decodeValue(char c) noexcept {
    if (c >= 'A' && c <= 'Z') {
        return c - 'A';
    }
    if (c >= 'a' && c <= 'z') {
        return c - 'a' + 26;
    }
    if (c >= '0' && c <= '9') {
        return c - '0' + 52;
    }
    if (c == '-') {
        return 62;
    }
    if (c == '_') {
        return 63;
    }
    return -1;
}

}  // namespace

std::string Base64Url::encode(std::span<const std::uint8_t> bytes) {
    std::string out;
    out.reserve(((bytes.size() + 2) / 3) * 4);

    std::size_t i = 0;
    while (i + 3 <= bytes.size()) {
        const std::uint32_t triple = (static_cast<std::uint32_t>(bytes[i]) << 16) |
                                     (static_cast<std::uint32_t>(bytes[i + 1]) << 8) |
                                     static_cast<std::uint32_t>(bytes[i + 2]);
        out.push_back(kAlphabet[(triple >> 18) & 0x3Fu]);
        out.push_back(kAlphabet[(triple >> 12) & 0x3Fu]);
        out.push_back(kAlphabet[(triple >> 6) & 0x3Fu]);
        out.push_back(kAlphabet[triple & 0x3Fu]);
        i += 3;
    }

    const std::size_t remaining = bytes.size() - i;
    if (remaining == 1) {
        const std::uint32_t value = static_cast<std::uint32_t>(bytes[i]) << 16;
        out.push_back(kAlphabet[(value >> 18) & 0x3Fu]);
        out.push_back(kAlphabet[(value >> 12) & 0x3Fu]);
    } else if (remaining == 2) {
        const std::uint32_t value = (static_cast<std::uint32_t>(bytes[i]) << 16) |
                                    (static_cast<std::uint32_t>(bytes[i + 1]) << 8);
        out.push_back(kAlphabet[(value >> 18) & 0x3Fu]);
        out.push_back(kAlphabet[(value >> 12) & 0x3Fu]);
        out.push_back(kAlphabet[(value >> 6) & 0x3Fu]);
    }

    return out;
}

std::string Base64Url::encode(std::string_view bytes) {
    return encode(std::span<const std::uint8_t>(
        reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size()));
}

bool Base64Url::isCanonical(std::string_view text) noexcept {
    const std::size_t remainder = text.size() % 4;
    if (remainder == 1) {
        return false;
    }

    int last = 0;
    for (char c : text) {
        last = decodeValue(c);
        if (last < 0) {
            return false;
        }
    }

    // Remainder 2 leaves four unused low bits, remainder 3 leaves two. They must
    // be zero, otherwise two different texts would decode to the same bytes.
    if (remainder == 2) {
        return (last & 0x0F) == 0;
    }
    if (remainder == 3) {
        return (last & 0x03) == 0;
    }
    return true;
}

std::size_t Base64Url::decodedSize(std::string_view text) noexcept {
    const std::size_t groups = text.size() / 4;
    const std::size_t remainder = text.size() % 4;
    std::size_t size = groups * 3;
    if (remainder == 2) {
        size += 1;
    } else if (remainder == 3) {
        size += 2;
    }
    return size;
}

bool Base64Url::isCanonicalWithByteLength(std::string_view text, std::size_t bytes) noexcept {
    return isCanonical(text) && decodedSize(text) == bytes;
}

bool Base64Url::decode(std::string_view text, std::vector<std::uint8_t>& out) {
    if (!isCanonical(text)) {
        return false;
    }

    std::vector<std::uint8_t> decoded;
    decoded.reserve(decodedSize(text));

    std::size_t i = 0;
    while (i + 4 <= text.size()) {
        const std::uint32_t value =
            (static_cast<std::uint32_t>(decodeValue(text[i])) << 18) |
            (static_cast<std::uint32_t>(decodeValue(text[i + 1])) << 12) |
            (static_cast<std::uint32_t>(decodeValue(text[i + 2])) << 6) |
            static_cast<std::uint32_t>(decodeValue(text[i + 3]));
        decoded.push_back(static_cast<std::uint8_t>((value >> 16) & 0xFFu));
        decoded.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFFu));
        decoded.push_back(static_cast<std::uint8_t>(value & 0xFFu));
        i += 4;
    }

    const std::size_t remainder = text.size() - i;
    if (remainder == 2) {
        const std::uint32_t value =
            (static_cast<std::uint32_t>(decodeValue(text[i])) << 18) |
            (static_cast<std::uint32_t>(decodeValue(text[i + 1])) << 12);
        decoded.push_back(static_cast<std::uint8_t>((value >> 16) & 0xFFu));
    } else if (remainder == 3) {
        const std::uint32_t value =
            (static_cast<std::uint32_t>(decodeValue(text[i])) << 18) |
            (static_cast<std::uint32_t>(decodeValue(text[i + 1])) << 12) |
            (static_cast<std::uint32_t>(decodeValue(text[i + 2])) << 6);
        decoded.push_back(static_cast<std::uint8_t>((value >> 16) & 0xFFu));
        decoded.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFFu));
    }

    out = std::move(decoded);
    return true;
}

}  // namespace phpvpn
