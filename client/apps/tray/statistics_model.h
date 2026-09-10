#pragma once
#include "status_model.h"
#include "ui_style.h"
#include <chrono>

namespace phpvpn::tray {
inline constexpr std::size_t kNoStatisticsCounter = kCounterFields.size();

struct StatisticsField {
    const wchar_t *label;
    std::size_t counterIndex;
    bool bytes;
    std::size_t addCounterIndex = kNoStatisticsCounter;
};
// Visual/tab order is 3 / 3 / (sent packets, sent bytes, received packets, received bytes).
inline constexpr std::array<StatisticsField, 10> statisticsFields{{{L"Empty polls", 1, false},
                                                                   {L"Polls with payload", 2, false},
                                                                   {L"Total polls", 0, false},
                                                                   {L"Request body", 3, true},
                                                                   {L"Response body", 4, true},
                                                                   {L"Total size", 3, true, 4},
                                                                   {L"Packets sent", 5, false},
                                                                   {L"Bytes sent", 7, true},
                                                                   {L"Packets received", 6, false},
                                                                   {L"Bytes received", 8, true}}};

struct StatisticsByteCount {
    std::uint64_t upper = 0;
    std::uint64_t lower = 0;
};

inline StatisticsByteCount addStatisticsBytes(std::uint64_t first, std::uint64_t second) {
    const auto lower = first + second;
    return {lower < first ? 1U : 0U, lower};
}

struct StatisticsDivision {
    std::uint64_t quotient = 0;
    std::uint64_t remainder = 0;
};

inline StatisticsDivision divideStatisticsBytes(StatisticsByteCount value, std::uint64_t divisor) {
    StatisticsDivision result;
    for (int bit = 64; bit >= 0; --bit) {
        const auto incoming = bit == 64 ? value.upper : (value.lower >> bit) & 1U;
        result.remainder = result.remainder * 2U + incoming;
        if (result.remainder >= divisor) {
            result.remainder -= divisor;
            if (bit < 64)
                result.quotient |= std::uint64_t{1} << bit;
        }
    }
    return result;
}

inline std::wstring formatStatisticsBytes(StatisticsByteCount value) {
    if (!value.upper && value.lower < 1000)
        return std::to_wstring(value.lower) + L" B";

    struct Unit {
        std::uint64_t divisor;
        const wchar_t *name;
    };
    static constexpr std::array<Unit, 5> units{{{1000ULL, L"kB"},
                                                {1000000ULL, L"MB"},
                                                {1000000000ULL, L"GB"},
                                                {1000000000000ULL, L"TB"},
                                                {1000000000000000ULL, L"PB"}}};
    std::size_t unit = units.size() - 1;
    if (!value.upper) {
        unit = 0;
        while (unit + 1 < units.size() && value.lower >= units[unit + 1].divisor)
            ++unit;
    }

    std::uint64_t roundedHundredths = 0;
    for (;;) {
        const auto divided = divideStatisticsBytes(value, units[unit].divisor);
        const auto hundredthDivisor = units[unit].divisor / 100U;
        const auto fractionalHundredths = divided.remainder / hundredthDivisor;
        const auto fractionalRemainder = divided.remainder % hundredthDivisor;
        roundedHundredths = divided.quotient * 100U + fractionalHundredths +
                            (fractionalRemainder * 2U >= hundredthDivisor ? 1U : 0U);
        if (roundedHundredths < 100000U || unit + 1 == units.size())
            break;
        ++unit;
    }

    const auto whole = roundedHundredths / 100U;
    const auto fraction = roundedHundredths % 100U;
    return std::to_wstring(whole) + L"." + (fraction < 10 ? L"0" : L"") + std::to_wstring(fraction) + L" " +
           units[unit].name;
}

inline std::wstring formatStatisticsBytes(std::uint64_t value) {
    return formatStatisticsBytes({0, value});
}

class StatisticsModel {
  public:
    void update(const TrayStatus &snapshot, bool unavailable) {
        if (snapshot.valid) {
            counters_ = snapshot.counters;
            connected_ = snapshot.state == "connected";
            connectionStartedAtUtcMs_ = snapshot.connectionStartedAtUtcMs;
            valid_ = true;
        }
        stale_ = unavailable || !snapshot.valid;
    }
    std::wstring value(std::size_t index) const {
        if (!valid_)
            return L"-";
        const auto &field = statisticsFields[index];
        const auto first = counters_.*kCounterFields[field.counterIndex].member;
        if (field.addCounterIndex != kNoStatisticsCounter)
            return formatStatisticsBytes(
                addStatisticsBytes(first, counters_.*kCounterFields[field.addCounterIndex].member));
        return field.bytes ? formatStatisticsBytes(first) : ui::englishInteger(first);
    }
    const wchar_t *freshness() const {
        return !valid_  ? L"Waiting for service status"
               : stale_ ? L"Service status unavailable. Showing the last received values."
                        : L"";
    }
    bool valid() const { return valid_; }
    bool stale() const { return stale_; }
    bool connected() const { return connected_; }
    std::wstring connectionStatus() const {
        if (!connected_) return L"(Disconnected)";
        if (!connectionStartedAtUtcMs_) return L"(Connected)";
        const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        const auto seconds = now > *connectionStartedAtUtcMs_ ? (now - *connectionStartedAtUtcMs_) / 1000 : 0;
        return L"(Connected for " + std::to_wstring(seconds / 86400) + L"d " +
            std::to_wstring(seconds / 3600 % 24) + L"h " + std::to_wstring(seconds / 60 % 60) +
            L"m " + std::to_wstring(seconds % 60) + L"s)";
    }

  private:
    TrafficCountersView counters_;
    std::optional<std::int64_t> connectionStartedAtUtcMs_;
    bool valid_ = false, stale_ = false, connected_ = false;
};
} // namespace phpvpn::tray
