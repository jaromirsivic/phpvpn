// Exactly 24 character UTC timestamps, protocol.md chapter 8.1.
//
// The accepted shape is YYYY-MM-DDTHH:MM:SS.sssZ. The year has four digits,
// every other numeric part except the three digit milliseconds has exactly two,
// `T` and `Z` are uppercase, seconds run from 00 to 59 and milliseconds from 000
// to 999. A missing or differently sized fraction, a numeric zone offset and the
// leap second 60 are all invalid.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace phpvpn {

class Iso8601 {
public:
    static constexpr std::size_t kLength = 24;

    // Milliseconds since the Unix epoch, or nullopt for any invalid input.
    // Uses an integer days-from-civil conversion, so the process time zone and
    // the C library locale never influence the result.
    static std::optional<std::int64_t> parse(std::string_view text) noexcept;

    static std::string format(std::int64_t epochMs);

    // Filename safe variant YYYY-MM-DDTHH-MM-SS.sssZ used by the server queue.
    static std::string formatFileNameSafe(std::int64_t epochMs);

    static bool isValid(std::string_view text) noexcept { return parse(text).has_value(); }
};

}  // namespace phpvpn
