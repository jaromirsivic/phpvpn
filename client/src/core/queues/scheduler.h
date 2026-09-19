// Fair circular request scheduler, client.md "Velikostni a spravedlive
// planovani".
//
// The worker keeps a circular order of destinations. In the first pass it takes
// at least one packet from every non empty queue, in the following passes it
// tops the records up in the same circular order while the byte budget and the
// record count allow it. FIFO inside a peer queue is never disturbed.
//
// The budget is `maxRequestBodyBytes - 1024`, and the decision always uses the
// exact size of the body that will be assembled. Every size is computed from
// the deterministic serialisation rules, so no trial encryption is needed:
// AES-GCM keeps the ciphertext the same size as the plaintext and canonical
// base64url has a fixed expansion.
#pragma once

#include <cstdint>
#include <vector>

#include "core/protocol/ipv4.h"
#include "core/queues/packet_queues.h"

namespace phpvpn {

// One planned `data` record: the batch of packets for exactly one destination.
struct PlannedRecord {
    Ipv4Address peer;
    PacketLease lease;
};

struct RequestPlan {
    RequestPlan() = default;
    RequestPlan(RequestPlan&&) noexcept = default;
    RequestPlan& operator=(RequestPlan&&) noexcept = default;
    RequestPlan(const RequestPlan&) = delete;
    RequestPlan& operator=(const RequestPlan&) = delete;
    std::vector<PlannedRecord> records;
    std::size_t bodyBytes = 0;

    bool empty() const noexcept { return records.empty(); }
};

struct SchedulerLimits {
    // Reserve of 1 KiB above the expected serialisation overhead.
    static constexpr std::size_t kBodyReserveBytes = 1024;

    std::int64_t maxRequestBodyBytes = 983040;
    std::int64_t maxDataRecordsPerRequest = 16;
};

// Byte length of the canonical base64url text of `byteCount` bytes.
std::size_t base64UrlTextLength(std::size_t byteCount) noexcept;

class RequestScheduler {
public:
    RequestScheduler(Ipv4Address localAddress, std::size_t requestIdLength = 36,
                     std::size_t timestampLength = 24);

    // Builds a rollback-on-destruction plan by leasing selected packets from
    // `queues`. A packet that cannot fit even into an otherwise empty valid
    // request is atomically removed from the head of its queue and permanently
    // dropped, which is reported through `drops`.
    RequestPlan planNextRequest(DirectionQueues& queues,
                                const SchedulerLimits& limits,
                                DropCounters& drops);

private:
    // Exact size of the assembled `data` record for a batch of `packetCount`
    // packets holding `packetByteSum` bytes, including the terminating LF.
    std::size_t recordSizeFor(std::size_t peerAddressLength,
                              std::size_t packetCount,
                              std::size_t packetByteSum) const;

    std::size_t authLineSize() const;

    Ipv4Address localAddress_;
    std::size_t requestIdLength_;
    std::size_t timestampLength_;
    std::size_t localAddressLength_;
    // Address the next request starts from, advanced after every request.
    std::uint32_t rotationCursor_ = 0;
};

}  // namespace phpvpn
