#include <atomic>
#include <limits>
#include <thread>

#include "core/protocol/json.h"
#include "core/runtime/traffic_counters.h"
#include "harness.h"

namespace phpvpn {
struct TrafficCountersTestAccess {
    static void seed(TrafficCounters& counters, const TrafficCountersSnapshot& value) {
        std::lock_guard<std::mutex> lock(counters.mutex_);
        counters.values_ = value;
    }
};

PHPVPN_TEST(traffic, saturates_without_breaking_polling_invariant) {
    const auto maximum = (std::numeric_limits<std::uint64_t>::max)();
    TrafficCounters counters;
    TrafficCountersSnapshot nearLimit;
    nearLimit.dataPollsTotal = maximum - 1;
    nearLimit.dataPollsEmpty = maximum - 2;
    nearLimit.dataPollsWithPayload = 1;
    nearLimit.serializedDataRequestBodyBytes = maximum - 2;
    nearLimit.serializedDataResponseBodyBytes = maximum - 2;
    nearLimit.ipv4PacketsSent = maximum - 2;
    nearLimit.ipv4PacketsReceived = maximum;
    nearLimit.ipv4BytesSent = maximum - 2;
    nearLimit.ipv4BytesReceived = maximum - 2;
    TrafficCountersTestAccess::seed(counters, nearLimit);
    counters.recordRequest(true, 100, 4, 100);
    counters.recordResponseBody(100);
    counters.recordAcceptedPacket(100);
    counters.recordRequest(false, 100, 4, 100);
    const auto value = counters.snapshot();
    PHPVPN_CHECK_EQ(value.dataPollsTotal, maximum);
    PHPVPN_CHECK_EQ(value.dataPollsEmpty, maximum - 2);
    PHPVPN_CHECK_EQ(value.dataPollsWithPayload, std::uint64_t(2));
    PHPVPN_CHECK_EQ(value.dataPollsTotal, value.dataPollsEmpty + value.dataPollsWithPayload);
    PHPVPN_CHECK_EQ(value.serializedDataRequestBodyBytes, maximum);
    PHPVPN_CHECK_EQ(value.serializedDataResponseBodyBytes, maximum);
    PHPVPN_CHECK_EQ(value.ipv4PacketsSent, maximum);
    PHPVPN_CHECK_EQ(value.ipv4PacketsReceived, maximum);
    PHPVPN_CHECK_EQ(value.ipv4BytesSent, maximum);
    PHPVPN_CHECK_EQ(value.ipv4BytesReceived, maximum);
    counters.reset();
    PHPVPN_CHECK_EQ(counters.snapshot().dataPollsTotal, std::uint64_t(0));
}

PHPVPN_TEST(traffic, concurrent_snapshots_are_consistent) {
    TrafficCounters counters;
    std::atomic<bool> finished{false};
    std::atomic<bool> consistent{true};
    std::thread reader([&] {
        while (!finished.load()) {
            const auto value = counters.snapshot();
            if (value.dataPollsTotal != value.dataPollsEmpty + value.dataPollsWithPayload ||
                value.serializedDataRequestBodyBytes != value.dataPollsTotal * 300 ||
                value.ipv4PacketsSent != value.dataPollsWithPayload * 2 ||
                value.ipv4BytesSent != value.dataPollsWithPayload * 100)
                consistent.store(false);
        }
    });
    for (int i = 0; i < 20000; ++i)
        counters.recordRequest(i % 2 != 0, 300, i % 2 ? 2 : 0, i % 2 ? 100 : 0);
    finished.store(true);
    reader.join();
    PHPVPN_CHECK(consistent.load());
    PHPVPN_CHECK_EQ(counters.snapshot().dataPollsTotal, std::uint64_t(20000));
}

PHPVPN_TEST(json, unsigned_integer_boundaries_remain_exact) {
    const auto maximum = (std::numeric_limits<std::uint64_t>::max)();
    for (const auto value : {maximum, maximum - 1, maximum / 2 + 1, maximum / 2, std::uint64_t(0)}) {
        const auto encoded = Json::serialize(JsonValue::makeUnsignedInteger(value));
        auto decoded = Json::parse(encoded);
        PHPVPN_REQUIRE_OK(decoded);
        PHPVPN_CHECK_EQ(jsonUnsignedOrZero(&decoded.value()), value);
        PHPVPN_CHECK_EQ(Json::serialize(decoded.value()), std::to_string(value));
        PHPVPN_CHECK(decoded.value().isUnsignedInteger() == (value > maximum / 2));
    }
    for (const auto& invalid : {JsonValue::makeInteger(-1), JsonValue::makeReal(1),
                               JsonValue::makeString("12"), JsonValue::makeNull()})
        PHPVPN_CHECK_EQ(jsonUnsignedOrZero(&invalid), std::uint64_t(0));
    PHPVPN_CHECK_EQ(jsonUnsignedOrZero(nullptr), std::uint64_t(0));
}
}  // namespace phpvpn
