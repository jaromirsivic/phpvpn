#include "core/protocol/uuid.h"

#include "core/base/hex.h"

namespace phpvpn {
namespace {

constexpr std::size_t kDashPositions[] = {8, 13, 18, 23};

bool isDash(std::size_t index) noexcept {
    for (std::size_t position : kDashPositions) {
        if (position == index) {
            return true;
        }
    }
    return false;
}

}  // namespace

bool Uuid::isValidV4(std::string_view text) noexcept {
    if (text.size() != kTextLength) {
        return false;
    }
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (isDash(i)) {
            if (text[i] != '-') {
                return false;
            }
            continue;
        }
        const char c = text[i];
        const bool hex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
        if (!hex) {
            return false;
        }
    }
    if (text[14] != '4') {
        return false;
    }
    const char variant = text[19];
    return variant == '8' || variant == '9' || variant == 'a' || variant == 'b';
}

std::string Uuid::formatV4(std::span<const std::uint8_t> random16) {
    std::uint8_t bytes[16] = {};
    const std::size_t count = random16.size() < 16 ? random16.size() : 16;
    for (std::size_t i = 0; i < count; ++i) {
        bytes[i] = random16[i];
    }
    bytes[6] = static_cast<std::uint8_t>((bytes[6] & 0x0Fu) | 0x40u);
    bytes[8] = static_cast<std::uint8_t>((bytes[8] & 0x3Fu) | 0x80u);

    const std::string hex = toLowercaseHex(std::span<const std::uint8_t>(bytes, 16));
    std::string out;
    out.reserve(kTextLength);
    out.append(hex, 0, 8);
    out.push_back('-');
    out.append(hex, 8, 4);
    out.push_back('-');
    out.append(hex, 12, 4);
    out.push_back('-');
    out.append(hex, 16, 4);
    out.push_back('-');
    out.append(hex, 20, 12);
    return out;
}

}  // namespace phpvpn
