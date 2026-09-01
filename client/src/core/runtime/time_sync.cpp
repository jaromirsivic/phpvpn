#include "core/runtime/time_sync.h"

#include <limits>

#include "core/protocol/iso8601.h"

namespace phpvpn {
namespace {

bool addOverflows(std::int64_t a, std::int64_t b) noexcept {
    if (b > 0 && a > std::numeric_limits<std::int64_t>::max() - b) {
        return true;
    }
    if (b < 0 && a < std::numeric_limits<std::int64_t>::min() - b) {
        return true;
    }
    return false;
}

}  // namespace

Result<TimeSnapshot> TimeSync::makeSnapshot(std::int64_t authenticatedServerTimestampMs,
                                            std::int64_t sendMonoMs,
                                            std::int64_t receiveMonoMs,
                                            std::int64_t localSystemUtcAtReceiveMs) {
    if (receiveMonoMs < sendMonoMs) {
        return Error(ErrorCode::time_invalid_sample, "the monotonic clock moved backwards");
    }
    const std::int64_t roundTrip = receiveMonoMs - sendMonoMs;

    const std::int64_t halfRoundTrip = roundTrip / 2;
    if (addOverflows(authenticatedServerTimestampMs, halfRoundTrip)) {
        return Error(ErrorCode::time_invalid_sample, "the estimated server time is not representable");
    }
    const std::int64_t serverTimeAtReceive = authenticatedServerTimestampMs + halfRoundTrip;

    if (addOverflows(serverTimeAtReceive, -localSystemUtcAtReceiveMs)) {
        return Error(ErrorCode::time_invalid_sample, "the clock offset is not representable");
    }

    TimeSnapshot snapshot;
    snapshot.authenticatedServerTimestampMs = authenticatedServerTimestampMs;
    snapshot.serverTimeAtReceiveMs = serverTimeAtReceive;
    snapshot.receiveMonoMs = receiveMonoMs;
    snapshot.roundTripMs = roundTrip;
    snapshot.serverLocalOffsetAtSyncMs = serverTimeAtReceive - localSystemUtcAtReceiveMs;
    snapshot.synchronisedAtMonoMs = receiveMonoMs;
    return snapshot;
}

std::int64_t TimeSync::safetyMarginMs(std::int64_t roundTripMs) noexcept {
    const std::int64_t fromRoundTrip = roundTripMs / 2 + kSafetyMarginSlackMs;
    return fromRoundTrip > kMinimumSafetyMarginMs ? fromRoundTrip : kMinimumSafetyMarginMs;
}

Result<std::int64_t> TimeSync::requestTimestampMs(std::int64_t currentMonotonicMs) const {
    if (!snapshot_.has_value()) {
        return Error(ErrorCode::time_not_synchronised, "no verified server time is available");
    }
    const TimeSnapshot& snapshot = *snapshot_;

    if (currentMonotonicMs < snapshot.receiveMonoMs) {
        return Error(ErrorCode::time_invalid_sample, "the monotonic clock moved backwards");
    }
    const std::int64_t elapsed = currentMonotonicMs - snapshot.receiveMonoMs;

    if (addOverflows(snapshot.serverTimeAtReceiveMs, elapsed)) {
        return Error(ErrorCode::time_invalid_sample, "the request time is not representable");
    }
    const std::int64_t projected = snapshot.serverTimeAtReceiveMs + elapsed;
    const std::int64_t margin = safetyMarginMs(snapshot.roundTripMs);
    if (addOverflows(projected, -margin)) {
        return Error(ErrorCode::time_invalid_sample, "the request time is not representable");
    }
    return projected - margin;
}

Result<std::string> TimeSync::requestTimestamp(std::int64_t currentMonotonicMs) const {
    std::int64_t epochMs = 0;
    PHPVPN_TRY_ASSIGN(epochMs, requestTimestampMs(currentMonotonicMs));
    return Iso8601::format(epochMs);
}

bool TimeSync::systemClockChanged(std::int64_t currentMonotonicMs,
                                  std::int64_t currentSystemUtcMs,
                                  std::int64_t toleranceMs) const {
    if (!snapshot_.has_value()) {
        return false;
    }
    const TimeSnapshot& snapshot = *snapshot_;
    if (currentMonotonicMs < snapshot.receiveMonoMs) {
        return true;
    }
    const std::int64_t elapsed = currentMonotonicMs - snapshot.receiveMonoMs;
    const std::int64_t expectedSystemUtc =
        snapshot.serverTimeAtReceiveMs + elapsed - snapshot.serverLocalOffsetAtSyncMs;
    const std::int64_t difference = currentSystemUtcMs - expectedSystemUtc;
    return difference > toleranceMs || difference < -toleranceMs;
}

}  // namespace phpvpn
