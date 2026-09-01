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
    RequestPlan plan;

    const std::size_t reserve = SchedulerLimits::kBodyReserveBytes;
    if (limits.maxRequestBodyBytes <= static_cast<std::int64_t>(reserve) ||
        limits.maxDataRecordsPerRequest <= 0) {
        return plan;
    }
    const std::size_t budget = static_cast<std::size_t>(limits.maxRequestBodyBytes) - reserve;
    const std::size_t authBytes = authLineSize();
    if (authBytes >= budget) {
        return plan;
    }

    std::vector<Ipv4Address> peers = queues.nonEmptyPeers();
    if (peers.empty()) {
        return plan;
    }

    // Rotate the circular order so a long queue cannot starve the short ones.
    const auto rotation = std::find_if(peers.begin(), peers.end(), [this](Ipv4Address peer) {
        return peer.value() >= rotationCursor_;
    });
    if (rotation != peers.end()) {
        std::rotate(peers.begin(), rotation, peers.end());
    }

    struct Selection {
        Ipv4Address peer;
        std::size_t addressLength = 0;
        std::vector<PacketBytes> packets;
        std::size_t packetByteSum = 0;
        std::size_t recordBytes = 0;
    };

    std::vector<Selection> selections;
    selections.reserve(peers.size());
    std::size_t bodyBytes = authBytes;

    // Measures the head packet of `selection` and takes it only when the exact
    // assembled body still fits. A rejected packet keeps its place at the head.
    const auto tryTakeOne = [&](Selection& selection) -> bool {
        const auto frontSize = queues.frontPacketSize(selection.peer);
        if (!frontSize.has_value()) {
            return false;
        }

        const std::size_t newRecordBytes = recordSizeFor(
            selection.addressLength, selection.packets.size() + 1, selection.packetByteSum + *frontSize);
        const std::size_t newBodyBytes = bodyBytes - selection.recordBytes + newRecordBytes;

        if (newBodyBytes > budget) {
            // A packet that does not fit even into an otherwise empty valid
            // request is removed from the head and permanently dropped; the
            // relative order of the remaining packets does not change.
            const std::size_t aloneBytes =
                authBytes + recordSizeFor(selection.addressLength, 1, *frontSize);
            if (aloneBytes > budget) {
                queues.popFront(selection.peer);
                drops.record(selection.peer, DropReason::oversized_for_request);
                return true;  // the head changed, so another attempt is useful
            }
            return false;
        }

        // The network worker is the only consumer of the outbound queues, so the
        // head cannot change between the measurement and this pop.
        auto packet = queues.popFront(selection.peer);
        if (!packet.has_value()) {
            return false;
        }
        selection.packetByteSum += packet->size();
        selection.packets.push_back(std::move(*packet));
        selection.recordBytes = newRecordBytes;
        bodyBytes = newBodyBytes;
        return true;
    };

    // First pass: at least one packet from every non empty queue.
    for (Ipv4Address peer : peers) {
        if (static_cast<std::int64_t>(selections.size()) >= limits.maxDataRecordsPerRequest) {
            break;
        }
        Selection selection;
        selection.peer = peer;
        selection.addressLength = peer.toString().size();

        // Keep retrying while the head is dropped as permanently oversized.
        while (selection.packets.empty() && tryTakeOne(selection)) {
        }
        if (!selection.packets.empty()) {
            selections.push_back(std::move(selection));
        }
    }

    // Following passes: top the records up in the same circular order.
    bool progressed = true;
    while (progressed) {
        progressed = false;
        for (Selection& selection : selections) {
            const std::size_t before = selection.packets.size();
            if (tryTakeOne(selection) && selection.packets.size() != before) {
                progressed = true;
            }
        }
    }

    for (Selection& selection : selections) {
        PlannedRecord record;
        record.peer = selection.peer;
        record.packets = std::move(selection.packets);
        plan.records.push_back(std::move(record));
    }
    plan.bodyBytes = bodyBytes;

    if (!plan.records.empty()) {
        rotationCursor_ = plan.records.front().peer.value() + 1;
    }

    return plan;
}

}  // namespace phpvpn
