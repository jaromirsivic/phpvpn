// T-N02 (plan-android-v001 point 11): the Android clock reads CLOCK_BOOTTIME
// for both monotonic sources and CLOCK_REALTIME for the wall clock, and never
// runs backwards. Linux and Android only (CLOCK_BOOTTIME).
#include "harness.h"

#include <time.h>

#include <chrono>
#include <cstdint>
#include <thread>

#include "platform/android/clock_android.h"

namespace phpvpn {
namespace {

std::int64_t readMs(clockid_t clock) {
    timespec now{};
    clock_gettime(clock, &now);
    return static_cast<std::int64_t>(now.tv_sec) * 1000 + now.tv_nsec / 1000000;
}

}  // namespace

PHPVPN_TEST(AndroidClock, BootTimeNeverRunsBackwards) {
    auto clock = createAndroidClock();
    std::int64_t previous = clock->monotonicMs();
    std::int64_t previousLease = clock->leaseMonotonicMs();
    for (int round = 0; round < 200000; ++round) {
        const std::int64_t now = clock->monotonicMs();
        const std::int64_t lease = clock->leaseMonotonicMs();
        PHPVPN_CHECK(now >= previous);
        PHPVPN_CHECK(lease >= previousLease);
        if (now < previous || lease < previousLease) return;
        previous = now;
        previousLease = lease;
    }
    const std::int64_t before = clock->monotonicMs();
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    PHPVPN_CHECK(clock->monotonicMs() - before >= 25);
}

PHPVPN_TEST(AndroidClock, MonotonicSourcesAreBootTime) {
    auto clock = createAndroidClock();
    // CLOCK_BOOTTIME includes suspend, so it is never behind CLOCK_MONOTONIC;
    // after a suspend it is ahead, which no tolerance check here can assume.
    const std::int64_t bootBefore = readMs(CLOCK_BOOTTIME);
    const std::int64_t monotonicBefore = readMs(CLOCK_MONOTONIC);
    const std::int64_t value = clock->monotonicMs();
    const std::int64_t lease = clock->leaseMonotonicMs();
    const std::int64_t bootAfter = readMs(CLOCK_BOOTTIME);
    PHPVPN_CHECK(value >= bootBefore && value <= bootAfter);
    PHPVPN_CHECK(lease >= bootBefore && lease <= bootAfter);
    PHPVPN_CHECK(value >= monotonicBefore);
}

PHPVPN_TEST(AndroidClock, WallClockIsRealtime) {
    auto clock = createAndroidClock();
    const std::int64_t before = readMs(CLOCK_REALTIME);
    const std::int64_t value = clock->systemUtcMs();
    const std::int64_t after = readMs(CLOCK_REALTIME);
    PHPVPN_CHECK(value >= before && value <= after);
    // 2026-01-01T00:00:00Z: the wall clock is epoch based, not boot based.
    PHPVPN_CHECK(value > 1767225600000);
}

}  // namespace phpvpn
