// Per peer FIFO queues with the admission accounting of client.md.
//
// Outbound queues are accounted by the destination VPN address, inbound queues
// by the source VPN address. A queue is created lazily on the first packet of
// that peer; the per peer reservation is purely logical and never preallocates
// a buffer for all possible peers.
//
// A new packet is admitted only when both conditions hold for the occupancy
// after a hypothetical insertion:
//
//   sum(actual occupancy)                                   <= maxQueueBytes
//   sum(max(occupancy, minPerPeerQueueBytes)) over all      <= maxQueueBytes
//   possible peer queues
//
// On a violation only the new packet is dropped. Already queued packets are
// never removed and never reordered.
#pragma once

#include <cstdint>
#include <deque>
#include <map>
#include <mutex>
#include <optional>
#include <vector>

#include "core/protocol/ipv4.h"
#include "core/protocol/payload_batch.h"

namespace phpvpn {

enum class DropReason {
    global_limit,
    per_peer_reservation,
    no_contract,
    invalid_packet,
    foreign_destination,
    oversized_for_request,
    batch_invalid,
    payload_tag_invalid,
    tunnel_stopped,
};

const char* dropReasonName(DropReason reason) noexcept;

// Aggregated drop accounting by reason and peer, without any packet content.
class DropCounters {
public:
    void record(Ipv4Address peer, DropReason reason, std::uint64_t count = 1);

    std::uint64_t total() const;
    std::uint64_t totalForReason(DropReason reason) const;

    struct Entry {
        Ipv4Address peer;
        DropReason reason;
        std::uint64_t count;
    };
    std::vector<Entry> entries() const;

private:
    mutable std::mutex mutex_;
    std::map<std::pair<std::uint32_t, int>, std::uint64_t> counters_;
};

struct QueueStatistics {
    std::size_t itemCount = 0;
    std::int64_t occupiedBytes = 0;
    std::int64_t limitBytes = 0;
    std::size_t peerQueueCount = 0;
};

class DirectionQueues {
public:
    DirectionQueues(std::int64_t maxQueueBytes,
                    std::int64_t minPerPeerQueueBytes,
                    std::uint32_t maxPeers);

    // Returns true when the packet was queued. On false `reason` explains which
    // of the two invariants refused it.
    bool tryEnqueue(Ipv4Address peer, PacketBytes packet, DropReason& reason);

    // Size of the head packet of `peer` without removing it. The scheduler
    // measures first and pops only once it decided to take the packet, so a
    // rejected packet keeps its place at the head of the FIFO.
    std::optional<std::size_t> frontPacketSize(Ipv4Address peer) const;

    // Removes and returns the front packet of `peer`, if any.
    std::optional<PacketBytes> popFront(Ipv4Address peer);

    // Returns packets that were already admitted back to the head of their FIFO
    // in their original order. Used when the server defers a target group or
    // when a response fails verification, in which case nothing may be removed
    // from the outbound queue. Admission is not re-evaluated because the bytes
    // were accounted for when the packets first entered.
    void pushFront(Ipv4Address peer, std::vector<PacketBytes> packets);

    // Peers whose queue currently holds at least one packet, in address order.
    std::vector<Ipv4Address> nonEmptyPeers() const;

    QueueStatistics statistics() const;

    // Largest packet a single, otherwise empty request could still admit under
    // the current occupancy. Used by the inbound pre-poll protection.
    bool canAdmitAnotherPacket(std::size_t packetSize) const;

    void clear();

    std::int64_t maxQueueBytes() const noexcept { return maxQueueBytes_; }
    std::int64_t minPerPeerQueueBytes() const noexcept { return minPerPeerQueueBytes_; }
    std::uint32_t maxPeers() const noexcept { return maxPeers_; }

private:
    struct PeerQueue {
        std::deque<PacketBytes> packets;
        std::int64_t bytes = 0;
    };

    // Both admission conditions evaluated for the occupancy that would result
    // from adding `additionalBytes` to the queue of `peer`.
    bool admitsLocked(Ipv4Address peer, std::int64_t additionalBytes, DropReason& reason) const;

    mutable std::mutex mutex_;
    std::map<std::uint32_t, PeerQueue> queues_;
    std::int64_t maxQueueBytes_;
    std::int64_t minPerPeerQueueBytes_;
    std::uint32_t maxPeers_;
    std::int64_t totalBytes_ = 0;
    std::size_t totalItems_ = 0;
};

}  // namespace phpvpn
