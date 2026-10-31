// Android clock, see clock_android.h (plan-android-v001 point 11).
#include "platform/android/clock_android.h"

#include <time.h>

namespace phpvpn {
namespace {

std::int64_t readMs(clockid_t clock) {
    timespec now{};
    if (clock_gettime(clock, &now) != 0) {
        return 0;
    }
    return static_cast<std::int64_t>(now.tv_sec) * 1000 + now.tv_nsec / 1000000;
}

class AndroidClock final : public Clock {
public:
    // CLOCK_BOOTTIME includes suspend, so a request after a sleep carries a
    // current timestamp and a sleep is never mistaken for a clock change.
    std::int64_t monotonicMs() override { return readMs(CLOCK_BOOTTIME); }
    std::int64_t leaseMonotonicMs() override { return readMs(CLOCK_BOOTTIME); }
    std::int64_t systemUtcMs() override { return readMs(CLOCK_REALTIME); }
};

}  // namespace

std::unique_ptr<Clock> createAndroidClock() {
    return std::make_unique<AndroidClock>();
}

#if defined(__ANDROID__)
std::unique_ptr<Clock> createPlatformClock() {
    return createAndroidClock();
}
#endif

}  // namespace phpvpn
