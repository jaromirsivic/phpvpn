#pragma once

#include <cstdint>
#include <mutex>

namespace phpvpn {

struct TrafficCountersSnapshot {
    std::uint64_t dataPollsTotal = 0;
    std::uint64_t dataPollsEmpty = 0;
    std::uint64_t dataPollsWithPayload = 0;
    std::uint64_t serializedDataRequestBodyBytes = 0;
    std::uint64_t serializedDataResponseBodyBytes = 0;
    std::uint64_t ipv4PacketsSent = 0;
    std::uint64_t ipv4PacketsReceived = 0;
    std::uint64_t ipv4BytesSent = 0;
    std::uint64_t ipv4BytesReceived = 0;
};

class TrafficCounters {
public:
    void recordRequest(bool hasPayload, std::uint64_t bodyBytes,
                       std::uint64_t packets, std::uint64_t packetBytes);
    void recordResponseBody(std::uint64_t bodyBytes);
    void recordAcceptedPacket(std::uint64_t packetBytes);
    void reset();
    TrafficCountersSnapshot snapshot() const;

private:
    friend struct TrafficCountersTestAccess;
    mutable std::mutex mutex_;
    TrafficCountersSnapshot values_;
};

}  // namespace phpvpn
