// Android time sources (plan-android-v001 point 11).
//
// A phone suspends many times an hour and CLOCK_MONOTONIC stops while it
// sleeps. With the Linux clock the first authenticated request after a sleep
// would carry a timestamp behind by the sleep (401, refresh health) and the
// clock change detection of TimeSync would see every sleep as a wall clock
// jump. So monotonicMs and leaseMonotonicMs both read CLOCK_BOOTTIME, which
// keeps counting during suspend, and systemUtcMs reads CLOCK_REALTIME. Queue
// expiry, the soft recovery budget and the recovery backoff therefore include
// the sleep as well (accepted consequences, T-A20 measures them).
#pragma once

#include <memory>

#include "platform/iface/clock.h"

namespace phpvpn {

// The Android clock; also built on Linux for the host unit tests (T-N02).
std::unique_ptr<Clock> createAndroidClock();

}  // namespace phpvpn
