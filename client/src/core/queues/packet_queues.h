// Symmetric flow queues. All storage/ownership/DRR changes hold mutex_.
// Lock order: queue -> drops. No I/O, engine callbacks or reverse nesting.
#pragma once
#include <array>
#include <bitset>
#include <functional>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>
#include "core/protocol/payload_batch.h"
#include "core/queues/flow_key.h"
#include "platform/iface/clock.h"

namespace phpvpn {
enum class DropReason {
    global_limit, per_peer_reservation, sub_bucket_reservation, queue_expired,
    no_contract, invalid_packet, foreign_destination, oversized_for_request,
    batch_invalid, payload_tag_invalid, tunnel_stopped,
};
const char* dropReasonName(DropReason reason) noexcept;
class DropCounters {
public:
    void record(Ipv4Address peer, DropReason reason, std::uint64_t count = 1);
    std::uint64_t total() const;
    std::uint64_t totalForReason(DropReason reason) const;
    struct Entry { Ipv4Address peer; DropReason reason; std::uint64_t count; };
    std::vector<Entry> entries() const;
private:
    mutable std::mutex mutex_;
    std::map<std::pair<std::uint32_t, int>, std::uint64_t> counters_;
};
struct QueueStatistics {
    std::size_t itemCount = 0;
    std::int64_t occupiedBytes = 0, limitBytes = 0;
    std::size_t peerQueueCount = 0, activeSubBucketCount = 0;
    std::int64_t inFlightBytes = 0;
    std::size_t queuedItemCount = 0;
    std::int64_t logicalBytes = 0;
    std::size_t expiryIndexSize = 0;
    std::uint64_t expiredPackets = 0, droppedPackets = 0;
};
struct QueuedPacket {
    PacketBytes bytes;
    std::uint64_t queuedAt = 0, sequence = 0;
    std::uint8_t bucket = 0;
    std::size_t size() const noexcept { return bytes.size(); }
};
class DirectionQueues;
// Stable node registered in the queue, including while its owner moves.
struct PacketLeaseState {
    Ipv4Address peer;
    std::list<QueuedPacket> packets;
    PacketLeaseState* previous = nullptr;
    PacketLeaseState* next = nullptr;
};
class PacketLease {
public:
    PacketLease() = default;
    PacketLease(DirectionQueues& queue, Ipv4Address peer);
    ~PacketLease();
    PacketLease(PacketLease&& other) noexcept;
    PacketLease& operator=(PacketLease&& other) noexcept;
    PacketLease(const PacketLease&) = delete;
    PacketLease& operator=(const PacketLease&) = delete;
    const std::list<QueuedPacket>& packets() const noexcept;
    bool empty() const noexcept { return packets().empty(); }
    void commit() noexcept;
    void rollback() noexcept;
private:
    friend class DirectionQueues;
    DirectionQueues* queue_ = nullptr;
    std::unique_ptr<PacketLeaseState> state_;
};
enum class CandidateDecision { take, defer_for_current_request, permanently_oversized };
using BlockedBuckets = std::bitset<16>;
struct QueueWorkStatistics {
    std::uint64_t classifications = 0, hashCalls = 0, pressureHeads = 0, promotionPackets = 0;
};
class DirectionQueues {
public:
    static constexpr std::size_t kBucketCount = 16;
    static constexpr std::size_t kPromotionScanLimit = 64;
    DirectionQueues(std::int64_t maxQueueBytes, std::int64_t peerMinimumBytes,
                    std::uint32_t maxPeers, Clock& clock, FlowHasher& hasher,
                    DropCounters& drops, std::int64_t maxQueuedPacketAgeMs = 5000,
                    std::size_t quantum = kMaxTunnelPacketBytes);
    ~DirectionQueues();
    Result<bool> tryEnqueue(Ipv4Address peer, PacketBytes packet, DropReason& reason);
    // Atomic expiry + DRR + sizing + splice. Decision must not acquire locks or do I/O.
    bool select(PacketLease& lease, BlockedBuckets& blocked,
                const std::function<CandidateDecision(std::size_t)>& decide);
    bool select(PacketLease& lease);
    // Convenience terminal consumption, through the same DRR/lease path.
    std::optional<PacketBytes> takePacket(Ipv4Address peer);
    std::vector<Ipv4Address> nonEmptyPeers() const;
    QueueStatistics statistics() const;
    QueueWorkStatistics workStatistics() const;
    bool canAdmitAnotherPacket(std::size_t packetSize);
    // Removes waiting packets. Held packets/peers remain valid until leases close.
    void clear();
    std::int64_t maxQueueBytes() const noexcept { return maxQueueBytes_; }
    std::int64_t peerMinimumBytes() const noexcept { return peerMinimumBytes_; }
    std::uint32_t maxPeers() const noexcept { return maxPeers_; }
    std::uint64_t maxQueuedPacketAgeMs() const noexcept { return maxAge_; }
    FlowHasher& hasher() noexcept { return hasher_; }
    struct BucketSnapshot {
        std::size_t waitingItems = 0, heldItems = 0, ownerPacketCount = 0;
        std::int64_t waitingBytes = 0, heldBytes = 0;
        std::uint64_t deficit = 0;
        std::optional<FlowKey> owner;
    };
    std::array<BucketSnapshot, 16> bucketSnapshot(Ipv4Address peer) const;
    void setSequenceForTesting(std::uint64_t sequence);
private:
    friend class PacketLease;
    friend struct QueueTestAccess;
    static constexpr std::size_t kNoIndex = static_cast<std::size_t>(-1);
    struct SubBucket {
        std::list<QueuedPacket> packets;
        std::int64_t waitingBytes = 0, heldBytes = 0;
        std::size_t heldItems = 0, ownerPacketCount = 0, heapPosition = kNoIndex;
        std::optional<FlowKey> owner;
        std::uint64_t deficit = 0;
        bool visiting = false;
    };
    struct PeerQueue {
        std::array<SubBucket, 16> buckets;
        std::size_t cursor = 0, items = 0;
        std::int64_t bytes = 0;
    };
    struct Head { std::uint32_t peer; std::uint8_t bucket; SubBucket* queue; };
    void registerLease(PacketLeaseState& state);
    void closeLease(PacketLeaseState& state, bool commit) noexcept;
    Result<std::uint8_t> chooseBucket(const PeerQueue* peer, const FlowKey& key);
    bool admitsLocked(const PeerQueue* peer, std::uint8_t bucket, std::int64_t bytes,
                      DropReason& reason) const;
    void changeBytes(PeerQueue& peer, SubBucket& bucket, std::int64_t delta);
    void updateOwner(PeerQueue& peer, SubBucket& bucket);
    void removeOwnerPacket(SubBucket& bucket, const QueuedPacket& packet);
    void discardHead(std::uint32_t peer, PeerQueue& queue, std::uint8_t bucket, DropReason reason, bool updateIndex = true);
    void expirePrefix(std::uint32_t peer, PeerQueue& queue, std::uint8_t bucket, std::uint64_t now);
    bool cleanupOldest(std::uint64_t now);
    bool expired(const QueuedPacket& packet, std::uint64_t now) const noexcept;
    void updateHead(std::uint32_t peer, std::uint8_t index, SubBucket& bucket) noexcept;
    void swapHeads(std::size_t a, std::size_t b) noexcept;
    void repairHeap(std::size_t index) noexcept;
    void compactSequences();
    FlowKey packetKey(const QueuedPacket& packet) const noexcept;
    mutable std::mutex mutex_;
    std::map<std::uint32_t, PeerQueue> queues_;
    std::vector<Head> heads_;
    std::int64_t maxQueueBytes_, peerMinimumBytes_, bucketMinimum_, baseline_;
    std::uint32_t maxPeers_;
    Clock& clock_;
    FlowHasher& hasher_;
    DropCounters& drops_;
    std::uint64_t maxAge_, quantum_, nextSequence_ = 0;
    std::int64_t totalBytes_ = 0, totalExcess_ = 0, peerExcess_ = 0, heldBytes_ = 0;
    std::size_t totalItems_ = 0, heldItems_ = 0;
    PacketLeaseState* leases_ = nullptr;
    mutable QueueWorkStatistics work_;
};
} // namespace phpvpn
