#include "core/base/hex.h"

namespace phpvpn {
namespace {

constexpr char kDigits[] = "0123456789abcdef";

int digitValue(char c) noexcept {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    return -1;
}

}  // namespace

bool isLowercaseHex(std::string_view text) noexcept {
    for (char c : text) {
        if (digitValue(c) < 0) {
            return false;
        }
    }
    return true;
}

std::string toLowercaseHex(std::span<const std::uint8_t> bytes) {
    std::string out;
    out.resize(bytes.size() * 2);
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        out[i * 2] = kDigits[bytes[i] >> 4];
        out[i * 2 + 1] = kDigits[bytes[i] & 0x0Fu];
    }
    return out;
}

bool decodeLowercaseHex(std::string_view text, std::vector<std::uint8_t>& out) {
    if (text.size() % 2 != 0) {
        return false;
    }
    std::vector<std::uint8_t> decoded;
    decoded.reserve(text.size() / 2);
    for (std::size_t i = 0; i < text.size(); i += 2) {
        const int hi = digitValue(text[i]);
        const int lo = digitValue(text[i + 1]);
        if (hi < 0 || lo < 0) {
            return false;
        }
        decoded.push_back(static_cast<std::uint8_t>((hi << 4) | lo));
    }
    out = std::move(decoded);
    return true;
}

}  // namespace phpvpn
