// Linux time sources.
//
// CLOCK_MONOTONIC is unaffected by a manual clock change, by NTP steps and by
// daylight saving, but stops during suspend. The lease clock uses
// CLOCK_BOOTTIME, which keeps counting while suspended, like GetTickCount64 on
// Windows. The wall clock is only used for logging, for the state change
// timestamp and for detecting that the user moved the clock.

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

class LinuxClock final : public Clock {
public:
    std::int64_t monotonicMs() override { return readMs(CLOCK_MONOTONIC); }
    std::int64_t leaseMonotonicMs() override { return readMs(CLOCK_BOOTTIME); }
    std::int64_t systemUtcMs() override { return readMs(CLOCK_REALTIME); }
};

}  // namespace

std::unique_ptr<Clock> createPlatformClock() {
    return std::make_unique<LinuxClock>();
}

}  // namespace phpvpn
