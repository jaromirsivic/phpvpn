// Windows time sources.
//
// The monotonic source is QueryPerformanceCounter, which is unaffected by a
// manual clock change, by NTP and by daylight saving. The wall clock comes from
// GetSystemTimePreciseAsFileTime and is only used for logging, for the state
// change timestamp and for detecting that the user moved the clock.

#include <windows.h>

#include "platform/iface/clock.h"

namespace phpvpn {
namespace {

// 100 nanosecond intervals between 1601-01-01 and 1970-01-01.
constexpr std::int64_t kFileTimeEpochOffset = 116444736000000000LL;
constexpr std::int64_t kFileTimeTicksPerMillisecond = 10000LL;

class WindowsClock final : public Clock {
public:
    WindowsClock() {
        LARGE_INTEGER frequency{};
        // The call cannot fail on Windows XP or later, but a zero would turn
        // into a division by zero, so it is guarded anyway.
        if (QueryPerformanceFrequency(&frequency) && frequency.QuadPart > 0) {
            frequency_ = frequency.QuadPart;
        } else {
            frequency_ = 1;
        }

        LARGE_INTEGER origin{};
        QueryPerformanceCounter(&origin);
        origin_ = origin.QuadPart;
    }

    std::int64_t monotonicMs() override {
        LARGE_INTEGER counter{};
        if (!QueryPerformanceCounter(&counter)) {
            return 0;
        }
        const std::int64_t ticks = counter.QuadPart - origin_;
        // The multiplication is split so that a long uptime cannot overflow.
        const std::int64_t seconds = ticks / frequency_;
        const std::int64_t remainder = ticks % frequency_;
        return seconds * 1000 + (remainder * 1000) / frequency_;
    }

    std::int64_t systemUtcMs() override {
        FILETIME fileTime{};
        GetSystemTimePreciseAsFileTime(&fileTime);
        ULARGE_INTEGER value{};
        value.LowPart = fileTime.dwLowDateTime;
        value.HighPart = fileTime.dwHighDateTime;
        const std::int64_t ticks = static_cast<std::int64_t>(value.QuadPart) - kFileTimeEpochOffset;
        return ticks / kFileTimeTicksPerMillisecond;
    }

private:
    std::int64_t frequency_ = 1;
    std::int64_t origin_ = 0;
};

}  // namespace

std::unique_ptr<Clock> createPlatformClock() {
    return std::make_unique<WindowsClock>();
}

}  // namespace phpvpn
