#include "core/protocol/iso8601.h"

#include <array>

namespace phpvpn {
namespace {

bool isDigit(char c) noexcept { return c >= '0' && c <= '9'; }

// Reads `count` digits starting at `offset`. Returns false when any byte is not
// an ASCII digit, so no locale dependent parsing is involved.
bool readNumber(std::string_view text, std::size_t offset, std::size_t count, int& out) noexcept {
    int value = 0;
    for (std::size_t i = 0; i < count; ++i) {
        const char c = text[offset + i];
        if (!isDigit(c)) {
            return false;
        }
        value = value * 10 + (c - '0');
    }
    out = value;
    return true;
}

bool isLeapYear(int year) noexcept {
    return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
}

int daysInMonth(int year, int month) noexcept {
    constexpr std::array<int, 12> kDays{31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month == 2 && isLeapYear(year)) {
        return 29;
    }
    return kDays[static_cast<std::size_t>(month - 1)];
}

// Howard Hinnant's days_from_civil. Purely integral, valid for the whole range
// of years the protocol can express.
std::int64_t daysFromCivil(std::int64_t year, unsigned month, unsigned day) noexcept {
    year -= month <= 2;
    const std::int64_t era = (year >= 0 ? year : year - 399) / 400;
    const auto yoe = static_cast<unsigned>(year - era * 400);
    const unsigned doy = (153u * (month + (month > 2 ? -3 : 9)) + 2u) / 5u + day - 1u;
    const unsigned doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
    return era * 146097 + static_cast<std::int64_t>(doe) - 719468;
}

// Inverse of daysFromCivil.
void civilFromDays(std::int64_t days, int& year, unsigned& month, unsigned& day) noexcept {
    days += 719468;
    const std::int64_t era = (days >= 0 ? days : days - 146096) / 146097;
    const auto doe = static_cast<unsigned>(days - era * 146097);
    const unsigned yoe = (doe - doe / 1460u + doe / 36524u - doe / 146096u) / 365u;
    const std::int64_t y = static_cast<std::int64_t>(yoe) + era * 400;
    const unsigned doy = doe - (365u * yoe + yoe / 4u - yoe / 100u);
    const unsigned mp = (5u * doy + 2u) / 153u;
    day = doy - (153u * mp + 2u) / 5u + 1u;
    month = mp + (mp < 10u ? 3u : static_cast<unsigned>(-9));
    year = static_cast<int>(y + (month <= 2u ? 1 : 0));
}

void appendPadded(std::string& out, int value, int width) {
    char buffer[8];
    for (int i = width - 1; i >= 0; --i) {
        buffer[i] = static_cast<char>('0' + value % 10);
        value /= 10;
    }
    out.append(buffer, static_cast<std::size_t>(width));
}

std::string formatWithTimeSeparator(std::int64_t epochMs, char separator) {
    std::int64_t days = epochMs / 86400000;
    std::int64_t remainder = epochMs % 86400000;
    if (remainder < 0) {
        remainder += 86400000;
        days -= 1;
    }

    int year = 0;
    unsigned month = 0;
    unsigned day = 0;
    civilFromDays(days, year, month, day);

    const int milliseconds = static_cast<int>(remainder % 1000);
    const std::int64_t totalSeconds = remainder / 1000;
    const int seconds = static_cast<int>(totalSeconds % 60);
    const int minutes = static_cast<int>((totalSeconds / 60) % 60);
    const int hours = static_cast<int>(totalSeconds / 3600);

    std::string out;
    out.reserve(Iso8601::kLength);
    appendPadded(out, year, 4);
    out.push_back('-');
    appendPadded(out, static_cast<int>(month), 2);
    out.push_back('-');
    appendPadded(out, static_cast<int>(day), 2);
    out.push_back('T');
    appendPadded(out, hours, 2);
    out.push_back(separator);
    appendPadded(out, minutes, 2);
    out.push_back(separator);
    appendPadded(out, seconds, 2);
    out.push_back('.');
    appendPadded(out, milliseconds, 3);
    out.push_back('Z');
    return out;
}

}  // namespace

std::optional<std::int64_t> Iso8601::parse(std::string_view text) noexcept {
    if (text.size() != kLength) {
        return std::nullopt;
    }
    if (text[4] != '-' || text[7] != '-' || text[10] != 'T' || text[13] != ':' ||
        text[16] != ':' || text[19] != '.' || text[23] != 'Z') {
        return std::nullopt;
    }

    int year = 0;
    int month = 0;
    int day = 0;
    int hour = 0;
    int minute = 0;
    int second = 0;
    int milli = 0;
    if (!readNumber(text, 0, 4, year) || !readNumber(text, 5, 2, month) ||
        !readNumber(text, 8, 2, day) || !readNumber(text, 11, 2, hour) ||
        !readNumber(text, 14, 2, minute) || !readNumber(text, 17, 2, second) ||
        !readNumber(text, 20, 3, milli)) {
        return std::nullopt;
    }

    if (month < 1 || month > 12) {
        return std::nullopt;
    }
    if (day < 1 || day > daysInMonth(year, month)) {
        return std::nullopt;
    }
    // Seconds stop at 59: the protocol forbids the leap second 60.
    if (hour > 23 || minute > 59 || second > 59) {
        return std::nullopt;
    }

    const std::int64_t days =
        daysFromCivil(year, static_cast<unsigned>(month), static_cast<unsigned>(day));
    const std::int64_t seconds =
        days * 86400 + static_cast<std::int64_t>(hour) * 3600 +
        static_cast<std::int64_t>(minute) * 60 + static_cast<std::int64_t>(second);
    return seconds * 1000 + milli;
}

std::string Iso8601::format(std::int64_t epochMs) { return formatWithTimeSeparator(epochMs, ':'); }

std::string Iso8601::formatFileNameSafe(std::int64_t epochMs) {
    return formatWithTimeSeparator(epochMs, '-');
}

}  // namespace phpvpn
