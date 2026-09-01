// Verified server time, client.md "Synchronizace casu".

#include "core/runtime/time_sync.h"

#include <string>

#include "core/protocol/iso8601.h"
#include "harness.h"

namespace phpvpn {

PHPVPN_TEST(time, computes_the_documented_estimate) {
    // RTT of 200 ms, so the server time at reception is the authenticated stamp
    // plus 100 ms.
    auto snapshot = TimeSync::makeSnapshot(1700000000000LL, 1000, 1200, 1699999999000LL);
    PHPVPN_REQUIRE_OK(snapshot);
    PHPVPN_CHECK_EQ(snapshot.value().roundTripMs, std::int64_t(200));
    PHPVPN_CHECK_EQ(snapshot.value().serverTimeAtReceiveMs, std::int64_t(1700000000100LL));
    PHPVPN_CHECK_EQ(snapshot.value().serverLocalOffsetAtSyncMs, std::int64_t(1100));
}

PHPVPN_TEST(time, applies_the_safety_margin) {
    // Below 1500 ms of round trip the floor of one second wins.
    PHPVPN_CHECK_EQ(TimeSync::safetyMarginMs(0), std::int64_t(1000));
    PHPVPN_CHECK_EQ(TimeSync::safetyMarginMs(200), std::int64_t(1000));
    PHPVPN_CHECK_EQ(TimeSync::safetyMarginMs(1500), std::int64_t(1000));
    PHPVPN_CHECK_EQ(TimeSync::safetyMarginMs(2000), std::int64_t(1250));
    PHPVPN_CHECK_EQ(TimeSync::safetyMarginMs(10000), std::int64_t(5250));
}

PHPVPN_TEST(time, the_request_timestamp_lags_by_the_margin) {
    TimeSync sync;
    auto snapshot = TimeSync::makeSnapshot(1700000000000LL, 1000, 1200, 1699999999000LL);
    PHPVPN_REQUIRE_OK(snapshot);
    sync.publish(snapshot.value());

    // Right at reception the timestamp is the estimate minus the margin.
    auto atReceive = sync.requestTimestampMs(1200);
    PHPVPN_REQUIRE_OK(atReceive);
    PHPVPN_CHECK_EQ(atReceive.value(), std::int64_t(1700000000100LL - 1000));

    // Five seconds of monotonic progress move it by exactly five seconds.
    auto later = sync.requestTimestampMs(6200);
    PHPVPN_REQUIRE_OK(later);
    PHPVPN_CHECK_EQ(later.value(), std::int64_t(1700000000100LL + 5000 - 1000));
}

PHPVPN_TEST(time, refuses_to_produce_a_timestamp_without_a_snapshot) {
    TimeSync sync;
    PHPVPN_REQUIRE_ERROR(sync.requestTimestampMs(0), ErrorCode::time_not_synchronised);
    PHPVPN_REQUIRE_ERROR(sync.requestTimestamp(0), ErrorCode::time_not_synchronised);
    PHPVPN_CHECK(!sync.hasSnapshot());
}

PHPVPN_TEST(time, refuses_a_backwards_monotonic_clock) {
    PHPVPN_REQUIRE_ERROR(TimeSync::makeSnapshot(1700000000000LL, 2000, 1000, 0),
                         ErrorCode::time_invalid_sample);

    TimeSync sync;
    auto snapshot = TimeSync::makeSnapshot(1700000000000LL, 1000, 1200, 1699999999000LL);
    PHPVPN_REQUIRE_OK(snapshot);
    sync.publish(snapshot.value());
    PHPVPN_REQUIRE_ERROR(sync.requestTimestampMs(1100), ErrorCode::time_invalid_sample);
}

PHPVPN_TEST(time, a_failed_sample_keeps_the_previous_snapshot) {
    TimeSync sync;
    auto good = TimeSync::makeSnapshot(1700000000000LL, 1000, 1200, 1699999999000LL);
    PHPVPN_REQUIRE_OK(good);
    sync.publish(good.value());

    auto bad = TimeSync::makeSnapshot(1700000000000LL, 5000, 4000, 0);
    PHPVPN_CHECK(!bad.ok());

    // The stored snapshot is untouched, because a candidate is only published
    // after it validated.
    PHPVPN_CHECK(sync.hasSnapshot());
    auto timestamp = sync.requestTimestampMs(1200);
    PHPVPN_REQUIRE_OK(timestamp);
    PHPVPN_CHECK_EQ(timestamp.value(), std::int64_t(1700000000100LL - 1000));
}

PHPVPN_TEST(time, detects_a_system_clock_change) {
    TimeSync sync;
    auto snapshot = TimeSync::makeSnapshot(1700000000000LL, 1000, 1200, 1699999999000LL);
    PHPVPN_REQUIRE_OK(snapshot);
    sync.publish(snapshot.value());

    // The wall clock advancing in step with the monotonic clock is normal.
    PHPVPN_CHECK(!sync.systemClockChanged(6200, 1699999999000LL + 5000));
    // A jump of an hour is not.
    PHPVPN_CHECK(sync.systemClockChanged(6200, 1699999999000LL + 5000 + 3600000));
    PHPVPN_CHECK(sync.systemClockChanged(6200, 1699999999000LL + 5000 - 3600000));
}

PHPVPN_TEST(time, formats_an_exact_wire_timestamp) {
    TimeSync sync;
    auto snapshot = TimeSync::makeSnapshot(1704067200000LL, 0, 0, 1704067200000LL);
    PHPVPN_REQUIRE_OK(snapshot);
    sync.publish(snapshot.value());

    auto text = sync.requestTimestamp(0);
    PHPVPN_REQUIRE_OK(text);
    PHPVPN_CHECK_EQ(text.value().size(), std::size_t(24));
    PHPVPN_CHECK(Iso8601::isValid(text.value()));
    PHPVPN_CHECK_EQ(text.value(), std::string("2023-12-31T23:59:59.000Z"));
}

PHPVPN_TEST(time, reset_clears_the_snapshot) {
    TimeSync sync;
    auto snapshot = TimeSync::makeSnapshot(1700000000000LL, 0, 0, 0);
    PHPVPN_REQUIRE_OK(snapshot);
    sync.publish(snapshot.value());
    PHPVPN_CHECK(sync.hasSnapshot());

    sync.reset();
    PHPVPN_CHECK(!sync.hasSnapshot());
}

}  // namespace phpvpn
