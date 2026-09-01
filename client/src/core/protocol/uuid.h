// UUID version 4 in the 8-4-4-4-12 lowercase hexadecimal form.
//
// protocol.md chapter 6.3 requires `requestId` and `messageId` to be UUID
// version 4 generated from a CSPRNG. The validator checks the version nibble at
// index 14 and the variant nibble at index 19 as well, matching the server.
#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace phpvpn {

class Uuid {
public:
    static constexpr std::size_t kTextLength = 36;

    static bool isValidV4(std::string_view text) noexcept;

    // Formats 16 random bytes, forcing the version and variant nibbles.
    static std::string formatV4(std::span<const std::uint8_t> random16);
};

}  // namespace phpvpn
