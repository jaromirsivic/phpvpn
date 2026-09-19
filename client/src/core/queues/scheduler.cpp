#include "core/queues/scheduler.h"

#include <algorithm>

#include "core/crypto/crypto_provider.h"
#include "core/protocol/data_record.h"
#include "core/protocol/request_builder.h"
#include "core/protocol/uuid.h"

namespace phpvpn {
namespace {

// The IV is 12 bytes and the GCM tag 16 bytes, so their canonical texts have
// fixed lengths of 16 and 22 characters.
constexpr std::size_t kIvTextLength = 16;
constexpr std::size_t kTagTextLength = 22;

}  // namespace

std::size_t base64UrlTextLength(std::size_t byteCount) noexcept {
    const std::size_t groups = byteCount / 3;
    const std::size_t remainder = byteCount % 3;
    std::size_t length = groups * 4;
    if (remainder == 1) {
        length += 2;
    } else if (remainder == 2) {
        length += 3;
    }
    return length;
}

RequestScheduler::RequestScheduler(Ipv4Address localAddress,
                                   std::size_t requestIdLength,
                                   std::size_t timestampLength)
    : localAddress_(localAddress),
      requestIdLength_(requestIdLength),
      timestampLength_(timestampLength),
      localAddressLength_(localAddress.toString().size()) {}

std::size_t RequestScheduler::authLineSize() const {
    return RequestBuilder::authFullLineSize(localAddressLength_, timestampLength_,
                                            requestIdLength_);
}

std::size_t RequestScheduler::recordSizeFor(std::size_t peerAddressLength,
                                            std::size_t packetCount,
                                            std::size_t packetByteSum) const {
    const std::size_t plaintextBytes = PayloadBatch::kHeaderBytes +
                                       packetCount * PayloadBatch::kLengthEntryBytes +
                                       packetByteSum;
    const std::size_t ciphertextTextLength = base64UrlTextLength(plaintextBytes);
    return DataRecord::encodedSizeWithLineFeed(Uuid::kTextLength,
                                               localAddressLength_,
                                               peerAddressLength,
                                               kIvTextLength,
                                               kTagTextLength,
                                               ciphertextTextLength);
}

RequestPlan RequestScheduler::planNextRequest(DirectionQueues& queues,
                                              const SchedulerLimits& limits,
                                              DropCounters& drops) {
    (void)drops; // Queue owns expiry and oversized drop accounting atomically.
    RequestPlan plan;
    const auto reserve = SchedulerLimits::kBodyReserveBytes;
    if (limits.maxRequestBodyBytes <= static_cast<std::int64_t>(reserve) ||
        limits.maxDataRecordsPerRequest <= 0) return plan;
    const auto budget = static_cast<std::size_t>(limits.maxRequestBodyBytes) - reserve;
    const auto authBytes = authLineSize();
    if (authBytes >= budget) return plan;
    auto peers = queues.nonEmptyPeers();
    const auto rotation = std::find_if(peers.begin(), peers.end(), [this](Ipv4Address peer) {
        return peer.value() >= rotationCursor_;
    });
    if (rotation != peers.end()) std::rotate(peers.begin(), rotation, peers.end());
    struct Selection {
        std::size_t addressLength = 0, packetByteSum = 0, recordBytes = 0;
        BlockedBuckets blocked;
    };
    std::vector<Selection> selections;
    selections.reserve(peers.size());
    plan.records.reserve(peers.size());
    std::size_t bodyBytes = authBytes;
    const auto tryTakeOne = [&](PlannedRecord& record, Selection& selection) {
        std::size_t selectedBytes = 0, newRecordBytes = 0, newBodyBytes = 0;
        const bool took = queues.select(record.lease, selection.blocked, [&](std::size_t size) {
            const auto aloneBytes = authBytes + recordSizeFor(selection.addressLength, 1, size);
            if (aloneBytes > budget) return CandidateDecision::permanently_oversized;
            newRecordBytes = recordSizeFor(selection.addressLength,
                record.lease.packets().size() + 1, selection.packetByteSum + size);
            newBodyBytes = bodyBytes - selection.recordBytes + newRecordBytes;
            if (newBodyBytes > budget) return CandidateDecision::defer_for_current_request;
            selectedBytes = size;
            return CandidateDecision::take;
        });
        if (took) {
            selection.packetByteSum += selectedBytes;
            selection.recordBytes = newRecordBytes;
            bodyBytes = newBodyBytes;
        }
        return took;
    };
    for (auto peer : peers) {
        if (static_cast<std::int64_t>(plan.records.size()) >= limits.maxDataRecordsPerRequest) break;
        PlannedRecord record{peer, PacketLease(queues, peer)};
        Selection selection;
        selection.addressLength = peer.toString().size();
        if (tryTakeOne(record, selection)) {
            plan.records.push_back(std::move(record));
            selections.push_back(std::move(selection));
        }
    }
    bool progressed = true;
    while (progressed) {
        progressed = false;
        for (std::size_t i = 0; i < plan.records.size(); ++i)
            if (tryTakeOne(plan.records[i], selections[i])) progressed = true;
    }
    plan.bodyBytes = bodyBytes;
    if (!plan.empty()) rotationCursor_ = plan.records.front().peer.value() + 1;
    return plan;
}
} // namespace phpvpn
