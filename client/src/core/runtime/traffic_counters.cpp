#include "core/runtime/traffic_counters.h"

#include <limits>

namespace phpvpn {
namespace {
void add(std::uint64_t& target, std::uint64_t increment) {
    const auto limit = (std::numeric_limits<std::uint64_t>::max)();
    target = increment > limit - target ? limit : target + increment;
}
}

void TrafficCounters::recordRequest(bool hasPayload, std::uint64_t bodyBytes,
                                    std::uint64_t packets, std::uint64_t packetBytes) {
    std::lock_guard<std::mutex> lock(mutex_);
    // client.md: freeze the polling triplet together to preserve its invariant.
    if (values_.dataPollsTotal != (std::numeric_limits<std::uint64_t>::max)()) {
        ++values_.dataPollsTotal;
        ++(hasPayload ? values_.dataPollsWithPayload : values_.dataPollsEmpty);
    }
    add(values_.serializedDataRequestBodyBytes, bodyBytes);
    add(values_.ipv4PacketsSent, packets);
    add(values_.ipv4BytesSent, packetBytes);
}

void TrafficCounters::recordResponseBody(std::uint64_t bodyBytes) {
    std::lock_guard<std::mutex> lock(mutex_);
    add(values_.serializedDataResponseBodyBytes, bodyBytes);
}

void TrafficCounters::recordAcceptedPacket(std::uint64_t packetBytes) {
    std::lock_guard<std::mutex> lock(mutex_);
    add(values_.ipv4PacketsReceived, 1);
    add(values_.ipv4BytesReceived, packetBytes);
}

void TrafficCounters::reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    values_ = {};
}

TrafficCountersSnapshot TrafficCounters::snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return values_;
}
}  // namespace phpvpn
