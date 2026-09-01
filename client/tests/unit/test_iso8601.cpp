// Exact 24 character UTC timestamps, protocol.md chapter 6.2.

#include "core/protocol/iso8601.h"

#include <string>

#include "harness.h"

namespace phpvpn {

PHPVPN_TEST(iso8601, accepts_only_the_exact_shape) {
    PHPVPN_CHECK(Iso8601::isValid("2024-01-01T00:00:00.000Z"));
    PHPVPN_CHECK(Iso8601::isValid("1970-01-01T00:00:00.000Z"));
    PHPVPN_CHECK(Iso8601::isValid("2038-01-19T03:14:07.999Z"));

    // Missing milliseconds, an offset, a lowercase marker and a space separator
    // are all refused, because the format is exact rather than tolerant.
    PHPVPN_CHECK(!Iso8601::isValid("2024-01-01T00:00:00Z"));
    PHPVPN_CHECK(!Iso8601::isValid("2024-01-01T00:00:00.000+00:00"));
    PHPVPN_CHECK(!Iso8601::isValid("2024-01-01t00:00:00.000Z"));
    PHPVPN_CHECK(!Iso8601::isValid("2024-01-01 00:00:00.000Z"));
    PHPVPN_CHECK(!Iso8601::isValid("2024-01-01T00:00:00.0000Z"));
    PHPVPN_CHECK(!Iso8601::isValid(""));
}

PHPVPN_TEST(iso8601, rejects_out_of_range_components) {
    PHPVPN_CHECK(!Iso8601::isValid("2024-13-01T00:00:00.000Z"));
    PHPVPN_CHECK(!Iso8601::isValid("2024-00-01T00:00:00.000Z"));
    PHPVPN_CHECK(!Iso8601::isValid("2024-01-32T00:00:00.000Z"));
    PHPVPN_CHECK(!Iso8601::isValid("2024-01-00T00:00:00.000Z"));
    PHPVPN_CHECK(!Iso8601::isValid("2024-01-01T24:00:00.000Z"));
    PHPVPN_CHECK(!Iso8601::isValid("2024-01-01T00:60:00.000Z"));

    // A leap second is not representable in this format.
    PHPVPN_CHECK(!Iso8601::isValid("2024-01-01T00:00:60.000Z"));
}

PHPVPN_TEST(iso8601, validates_the_day_against_the_month) {
    PHPVPN_CHECK(Iso8601::isValid("2024-02-29T12:00:00.000Z"));
    PHPVPN_CHECK(!Iso8601::isValid("2023-02-29T12:00:00.000Z"));
    PHPVPN_CHECK(!Iso8601::isValid("2024-04-31T12:00:00.000Z"));
    PHPVPN_CHECK(Iso8601::isValid("2000-02-29T12:00:00.000Z"));
    PHPVPN_CHECK(!Iso8601::isValid("1900-02-29T12:00:00.000Z"));
}

PHPVPN_TEST(iso8601, round_trips_through_the_epoch) {
    const char* samples[] = {
        "1970-01-01T00:00:00.000Z",
        "2000-02-29T12:34:56.789Z",
        "2024-12-31T23:59:59.999Z",
        "2099-06-15T08:09:10.011Z",
    };

    for (const char* sample : samples) {
        const auto parsed = Iso8601::parse(sample);
        PHPVPN_CHECK(parsed.has_value());
        if (parsed.has_value()) {
            PHPVPN_CHECK_EQ(Iso8601::format(*parsed), std::string(sample));
        }
    }
}

PHPVPN_TEST(iso8601, formats_a_known_instant) {
    PHPVPN_CHECK_EQ(Iso8601::format(0), std::string("1970-01-01T00:00:00.000Z"));
    PHPVPN_CHECK_EQ(Iso8601::format(1000), std::string("1970-01-01T00:00:01.000Z"));
    PHPVPN_CHECK_EQ(Iso8601::format(1704067200000LL), std::string("2024-01-01T00:00:00.000Z"));
}

PHPVPN_TEST(iso8601, produces_a_file_name_without_forbidden_characters) {
    const std::string name = Iso8601::formatFileNameSafe(1704067200000LL);
    PHPVPN_CHECK(name.find(':') == std::string::npos);
    PHPVPN_CHECK(!name.empty());
}

}  // namespace phpvpn
