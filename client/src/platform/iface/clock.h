// Time sources used by the core.
//
// The monotonic source is QueryPerformanceCounter on Windows. The wall clock is
// only used for logging, for the state change timestamp and for detecting a
// system clock change; it never produces an authentication timestamp once the
// verified server time exists, see client.md "Synchronizace casu".
#pragma once

#include <cstdint>
#include <memory>

namespace phpvpn {

class Clock {
public:
    virtual ~Clock() = default;

    // Milliseconds from an arbitrary but monotonic origin.
    virtual std::int64_t monotonicMs() = 0;

    // Milliseconds since the Unix epoch from the system wall clock.
    virtual std::int64_t systemUtcMs() = 0;
};

std::unique_ptr<Clock> createPlatformClock();

}  // namespace phpvpn
