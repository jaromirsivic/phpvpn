// Verified server time, client.md "Synchronizace casu".
//
//   RTT                 = receiveMono - sendMono
//   serverTimeAtReceive = authenticatedServerTimestamp + RTT / 2
//   safetyMargin        = max(1000 ms, RTT / 2 + 250 ms)
//   requestTimestamp    = serverTimeAtReceive + (currentMonotonic - receiveMono)
//                         - safetyMargin
//
// A negative RTT, a monotonic clock that went backwards or an unrepresentable
// result invalidates the attempt and leaves the previous snapshot untouched.
// The whole snapshot is published as one unit, never field by field.
#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "core/base/result.h"

namespace phpvpn {

struct TimeSnapshot {
    std::int64_t authenticatedServerTimestampMs = 0;
    std::int64_t serverTimeAtReceiveMs = 0;
    std::int64_t receiveMonoMs = 0;
    std::int64_t roundTripMs = 0;
    std::int64_t serverLocalOffsetAtSyncMs = 0;
    std::int64_t synchronisedAtMonoMs = 0;
};

class TimeSync {
public:
    static constexpr std::int64_t kMinimumSafetyMarginMs = 1000;
    static constexpr std::int64_t kSafetyMarginSlackMs = 250;

    bool hasSnapshot() const noexcept { return snapshot_.has_value(); }
    const std::optional<TimeSnapshot>& snapshot() const noexcept { return snapshot_; }

    // Builds a candidate snapshot from one verified `health` exchange. Returns
    // an error without touching the stored snapshot when the sample is unusable.
    static Result<TimeSnapshot> makeSnapshot(std::int64_t authenticatedServerTimestampMs,
                                             std::int64_t sendMonoMs,
                                             std::int64_t receiveMonoMs,
                                             std::int64_t localSystemUtcAtReceiveMs);

    // Replaces the whole snapshot atomically.
    void publish(const TimeSnapshot& snapshot) { snapshot_ = snapshot; }

    void reset() { snapshot_.reset(); }

    // Authentication timestamp for the next request, in milliseconds.
    Result<std::int64_t> requestTimestampMs(std::int64_t currentMonotonicMs) const;

    // Exact 24 character UTC text for the next request.
    Result<std::string> requestTimestamp(std::int64_t currentMonotonicMs) const;

    static std::int64_t safetyMarginMs(std::int64_t roundTripMs) noexcept;

    // True when the wall clock moved away from the offset recorded at the last
    // synchronisation by more than `toleranceMs`, which schedules a fast refresh.
    bool systemClockChanged(std::int64_t currentMonotonicMs,
                            std::int64_t currentSystemUtcMs,
                            std::int64_t toleranceMs = 2000) const;

private:
    std::optional<TimeSnapshot> snapshot_;
};

}  // namespace phpvpn
