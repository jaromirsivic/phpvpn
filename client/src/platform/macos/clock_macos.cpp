// macOS time sources (plan-macos-v001, limitation 7).
//
// Darwin names its clocks differently from Linux: CLOCK_MONOTONIC keeps
// counting while the machine sleeps (the Darwin counterpart of the Linux
// CLOCK_BOOTTIME), and CLOCK_UPTIME_RAW stops during sleep (the counterpart of
// the Linux CLOCK_MONOTONIC). Both are unaffected by a manual clock change, by
// NTP steps and by daylight saving. The mapping therefore is:
//
//   monotonicMs      -> CLOCK_UPTIME_RAW  (stops in sleep, like Linux CLOCK_MONOTONIC)
//   leaseMonotonicMs -> CLOCK_MONOTONIC   (includes sleep, like CLOCK_BOOTTIME and
//                                          GetTickCount64 on Windows)
//   systemUtcMs      -> CLOCK_REALTIME
//
// The wall clock is only used for logging, for the state change timestamp and
// for detecting that the user moved the clock.

#include <time.h>

#include "platform/iface/clock.h"

namespace phpvpn {
namespace {

std::int64_t readMs(clockid_t clock) {
    timespec now{};
    if (clock_gettime(clock, &now) != 0) {
        return 0;
    }
    return static_cast<std::int64_t>(now.tv_sec) * 1000 + now.tv_nsec / 1000000;
}

class MacClock final : public Clock {
public:
    std::int64_t monotonicMs() override { return readMs(CLOCK_UPTIME_RAW); }
    std::int64_t leaseMonotonicMs() override { return readMs(CLOCK_MONOTONIC); }
    std::int64_t systemUtcMs() override { return readMs(CLOCK_REALTIME); }
};

}  // namespace

std::unique_ptr<Clock> createPlatformClock() {
    return std::make_unique<MacClock>();
}

}  // namespace phpvpn
