// Lowercase hexadecimal helpers.
//
// protocol.md chapter 3.2 requires every ACCESS_KEY and PAYLOAD_KEY to be
// written as exactly 64 lowercase hexadecimal characters, and SHA256HEX in the
// canonical strings of chapters 9.2 and 9.5 uses the same alphabet.
#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace phpvpn {

// True when `text` consists only of the characters 0-9 and a-f. Uppercase is
// rejected because the protocol pins the lowercase spelling.
bool isLowercaseHex(std::string_view text) noexcept;

std::string toLowercaseHex(std::span<const std::uint8_t> bytes);

// Returns false when the text is not lowercase hex of even length.
bool decodeLowercaseHex(std::string_view text, std::vector<std::uint8_t>& out);

}  // namespace phpvpn
