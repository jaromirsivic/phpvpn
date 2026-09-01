// Canonical base64url without padding, protocol.md chapter 6.1.
//
// The alphabet is A-Z a-z 0-9 - _ in that order. The canonical length modulo 4
// is 0, 2 or 3; a remainder of 1 is always invalid. The unused low bits of the
// last character must be zero, so `_w` and `__8` are canonical while `_x` and
// `__9` are not. Padding, whitespace, `+` and `/` are always rejected.
#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace phpvpn {

class Base64Url {
public:
    static std::string encode(std::span<const std::uint8_t> bytes);
    static std::string encode(std::string_view bytes);

    // Full canonicity check: alphabet, length class and zero unused bits.
    static bool isCanonical(std::string_view text) noexcept;

    // Number of bytes `text` decodes into. Only meaningful for canonical text.
    static std::size_t decodedSize(std::string_view text) noexcept;

    // True when `text` is canonical and decodes into exactly `bytes` bytes.
    static bool isCanonicalWithByteLength(std::string_view text, std::size_t bytes) noexcept;

    // Decodes canonical text. Returns false for any non canonical input, so a
    // tolerant decoder can never accept a value the protocol forbids.
    static bool decode(std::string_view text, std::vector<std::uint8_t>& out);
};

}  // namespace phpvpn
