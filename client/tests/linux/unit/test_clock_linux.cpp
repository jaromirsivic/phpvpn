// Linux platform tests owned by WP02 (plan-v001 section 9.4).
#include "harness.h"

#include <time.h>

#include "platform/iface/clock.h"

namespace phpvpn {

PHPVPN_TEST(LinuxClock, MonotonicSourcesAdvanceAndWallClockIsUnixMilliseconds) {
    auto clock = createPlatformClock();
    const std::int64_t mono = clock->monotonicMs();
    const std::int64_t lease = clock->leaseMonotonicMs();
    timespec pause{0, 20 * 1000 * 1000};
    nanosleep(&pause, nullptr);
    PHPVPN_CHECK(clock->monotonicMs() >= mono + 15);
    PHPVPN_CHECK(clock->leaseMonotonicMs() >= lease + 15);
    // 2020-01-01 as a sanity floor for the Unix epoch based wall clock.
    PHPVPN_CHECK(clock->systemUtcMs() > 1577836800000LL);
}

}  // namespace phpvpn
