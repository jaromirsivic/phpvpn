#include "core/queues/packet_queues.h"
#include <algorithm>
#include <cassert>
#include <limits>
#include <utility>

namespace phpvpn {
const char* dropReasonName(DropReason reason) noexcept {
    switch (reason) {
#define PHPVPN_DROP_NAME(name) case DropReason::name: return #name
        PHPVPN_DROP_NAME(global_limit);
        PHPVPN_DROP_NAME(per_peer_reservation);
        PHPVPN_DROP_NAME(sub_bucket_reservation);
        PHPVPN_DROP_NAME(queue_expired);
        PHPVPN_DROP_NAME(no_contract);
        PHPVPN_DROP_NAME(invalid_packet);
        PHPVPN_DROP_NAME(foreign_destination);
        PHPVPN_DROP_NAME(oversized_for_request);
        PHPVPN_DROP_NAME(batch_invalid);
        PHPVPN_DROP_NAME(payload_tag_invalid);
        PHPVPN_DROP_NAME(tunnel_stopped);
#undef PHPVPN_DROP_NAME
    }
    return "unknown";
}
void DropCounters::record(Ipv4Address peer, DropReason reason, std::uint64_t count) {
    std::lock_guard lock(mutex_);
    counters_[{peer.value(), static_cast<int>(reason)}] += count;
}
std::uint64_t DropCounters::total() const {
    std::lock_guard lock(mutex_);
    std::uint64_t sum = 0;
    for (const auto& entry : counters_) sum += entry.second;
    return sum;
}
std::uint64_t DropCounters::totalForReason(DropReason reason) const {
    std::lock_guard lock(mutex_);
    std::uint64_t sum = 0;
    for (const auto& entry : counters_)
        if (entry.first.second == static_cast<int>(reason)) sum += entry.second;
    return sum;
}
std::vector<DropCounters::Entry> DropCounters::entries() const {
    std::lock_guard lock(mutex_);
    std::vector<Entry> out;
    out.reserve(counters_.size());
    for (const auto& entry : counters_)
        out.push_back({Ipv4Address(entry.first.first), static_cast<DropReason>(entry.first.second), entry.second});
    return out;
}
PacketLease::PacketLease(DirectionQueues& queue, Ipv4Address peer)
    : queue_(&queue), state_(std::make_unique<PacketLeaseState>()) {
    state_->peer = peer;
    queue_->registerLease(*state_);
}
PacketLease::~PacketLease() { rollback(); }
PacketLease::PacketLease(PacketLease&& other) noexcept
    : queue_(std::exchange(other.queue_, nullptr)), state_(std::move(other.state_)) {}
PacketLease& PacketLease::operator=(PacketLease&& other) noexcept {
    if (this != &other) {
        rollback();
        queue_ = std::exchange(other.queue_, nullptr);
        state_ = std::move(other.state_);
    }
    return *this;
}
const std::list<QueuedPacket>& PacketLease::packets() const noexcept {
    static const std::list<QueuedPacket> empty;
    return state_ ? state_->packets : empty;
}
void PacketLease::commit() noexcept {
    if (queue_) queue_->closeLease(*state_, true);
    queue_ = nullptr;
    state_.reset();
}
void PacketLease::rollback() noexcept {
    if (queue_) queue_->closeLease(*state_, false);
    queue_ = nullptr;
    state_.reset();
}
DirectionQueues::DirectionQueues(std::int64_t maxQueueBytes, std::int64_t peerMinimumBytes,
    std::uint32_t maxPeers, Clock& clock, FlowHasher& hasher, DropCounters& drops,
    std::int64_t maxQueuedPacketAgeMs, std::size_t quantum)
    : maxQueueBytes_(maxQueueBytes), peerMinimumBytes_(peerMinimumBytes),
      bucketMinimum_(peerMinimumBytes / 16), baseline_(-1), maxPeers_(maxPeers),
      clock_(clock), hasher_(hasher), drops_(drops),
      maxAge_(static_cast<std::uint64_t>(maxQueuedPacketAgeMs)),
      quantum_(std::max<std::size_t>(1, quantum)) {
    if (maxQueueBytes >= 0 && peerMinimumBytes >= 0 && maxPeers > 0 &&
        peerMinimumBytes <= maxQueueBytes / maxPeers && maxQueuedPacketAgeMs > 0)
        baseline_ = peerMinimumBytes * maxPeers;
}
DirectionQueues::~DirectionQueues() { assert(leases_ == nullptr); }
FlowKey DirectionQueues::packetKey(const QueuedPacket& packet) const noexcept {
    ++work_.classifications;
    Ipv4PacketView view;
    const bool valid = validateIpv4Packet(packet.bytes, view);
    assert(valid);
    (void)valid;
    return flowKeyFromValidated(packet.bytes, view);
}
Result<std::uint8_t> DirectionQueues::chooseBucket(const PeerQueue* peer, const FlowKey& key) {
    if (!peer) return std::uint8_t{0};
    for (std::uint8_t i = 0; i < 16; ++i)
        if (peer->buckets[i].owner == key) return i;
    for (std::uint8_t i = 0; i < 16; ++i)
        if (peer->buckets[i].packets.empty()) return i;
    ++work_.hashCalls;
    auto result = hasher_.bucket(key);
    if (!result) return result.error();
    if (*result >= 16) return Error(ErrorCode::internal, "invalid flow hash bucket");
    return result;
}
bool DirectionQueues::admitsLocked(const PeerQueue* peer, std::uint8_t index,
                                   std::int64_t bytes, DropReason& reason) const {
    if (bytes > maxQueueBytes_ - totalBytes_) { reason = DropReason::global_limit; return false; }
    if (baseline_ < 0 || (!peer && queues_.size() >= maxPeers_)) {
        reason = DropReason::per_peer_reservation; return false;
    }
    const auto excess = [](std::int64_t x, std::int64_t minimum) { return std::max<std::int64_t>(0, x - minimum); };
    const auto peerBytes = peer ? peer->bytes : 0;
    const auto peerDelta = excess(peerBytes + bytes, peerMinimumBytes_) - excess(peerBytes, peerMinimumBytes_);
    if (peerDelta > maxQueueBytes_ - baseline_ - peerExcess_) {
        reason = DropReason::per_peer_reservation; return false;
    }
    const auto x = peer ? peer->buckets[index].waitingBytes + peer->buckets[index].heldBytes : 0;
    const auto delta = excess(x + bytes, bucketMinimum_) - excess(x, bucketMinimum_);
    if (delta > maxQueueBytes_ - baseline_ - totalExcess_) {
        reason = DropReason::sub_bucket_reservation; return false;
    }
    return true;
}
void DirectionQueues::changeBytes(PeerQueue& peer, SubBucket& bucket, std::int64_t delta) {
    const auto excess = [](std::int64_t x, std::int64_t minimum) { return std::max<std::int64_t>(0, x - minimum); };
    const auto x = bucket.waitingBytes + bucket.heldBytes;
    totalExcess_ += excess(x + delta, bucketMinimum_) - excess(x, bucketMinimum_);
    peerExcess_ += excess(peer.bytes + delta, peerMinimumBytes_) - excess(peer.bytes, peerMinimumBytes_);
    peer.bytes += delta;
    totalBytes_ += delta;
}
Result<bool> DirectionQueues::tryEnqueue(Ipv4Address address, PacketBytes packet, DropReason& reason) {
    Ipv4PacketView view;
    if (!validateIpv4Packet(packet, view)) { reason = DropReason::invalid_packet; return false; }
    const auto key = flowKeyFromValidated(packet, view);
    const auto bytes = static_cast<std::int64_t>(packet.size());
    std::lock_guard lock(mutex_);
    ++work_.classifications;
    for (;;) {
        auto found = queues_.find(address.value());
        auto* peer = found == queues_.end() ? nullptr : &found->second;
        auto chosen = chooseBucket(peer, key);
        if (!chosen) return chosen.error();
        const auto index = chosen.value();
        if (!admitsLocked(peer, index, bytes, reason)) {
            if (cleanupOldest(static_cast<std::uint64_t>(clock_.monotonicMs()))) continue;
            return false;
        }
        // Prepare all allocations before publishing ownership/accounting.
        std::list<QueuedPacket> prepared;
        prepared.push_back({std::move(packet), 0, 0, index});
        if (nextSequence_ == (std::numeric_limits<std::uint64_t>::max)()) compactSequences();
        if (!peer) {
            heads_.reserve(16 * (queues_.size() + 1));
            peer = &queues_.try_emplace(address.value()).first->second;
        }
        auto& bucket = peer->buckets[index];
        auto& node = prepared.front();
        node.queuedAt = static_cast<std::uint64_t>(clock_.monotonicMs());
        node.sequence = nextSequence_++;
        if (bucket.packets.empty()) { bucket.owner = key; bucket.ownerPacketCount = 0; }
        if (bucket.owner == key) ++bucket.ownerPacketCount;
        changeBytes(*peer, bucket, bytes);
        bucket.waitingBytes += bytes;
        ++peer->items;
        ++totalItems_;
        bucket.packets.splice(bucket.packets.end(), prepared);
        updateHead(address.value(), index, bucket);
        return true;
    }
}
void DirectionQueues::removeOwnerPacket(SubBucket& bucket, const QueuedPacket& packet) {
    if (bucket.owner && *bucket.owner == packetKey(packet)) {
        assert(bucket.ownerPacketCount > 0);
        if (--bucket.ownerPacketCount == 0) bucket.owner.reset();
    }
}
void DirectionQueues::updateOwner(PeerQueue& peer, SubBucket& bucket) {
    if (bucket.packets.empty()) {
        bucket.owner.reset(); bucket.ownerPacketCount = 0;
        bucket.deficit = 0; bucket.visiting = false;
        return;
    }
    if (bucket.owner || bucket.packets.size() > kPromotionScanLimit) return;
    const auto candidate = packetKey(bucket.packets.front());
    for (const auto& other : peer.buckets) if (other.owner == candidate) return;
    std::size_t count = 0;
    for (const auto& packet : bucket.packets) {
        ++work_.promotionPackets;
        if (packetKey(packet) == candidate) ++count;
    }
    bucket.owner = candidate;
    bucket.ownerPacketCount = count;
}
bool DirectionQueues::expired(const QueuedPacket& packet, std::uint64_t now) const noexcept {
    return now - packet.queuedAt >= maxAge_;
}
void DirectionQueues::discardHead(std::uint32_t address, PeerQueue& peer, std::uint8_t index, DropReason reason, bool updateIndex) {
    auto& bucket = peer.buckets[index];
    const auto& packet = bucket.packets.front();
    // Record before mutation, so a counter allocation failure cannot lose a packet.
    drops_.record(Ipv4Address(address), reason);
    removeOwnerPacket(bucket, packet);
    const auto bytes = static_cast<std::int64_t>(packet.size());
    changeBytes(peer, bucket, -bytes);
    bucket.waitingBytes -= bytes;
    --peer.items; --totalItems_;
    bucket.packets.pop_front();
    updateOwner(peer, bucket);
    if (updateIndex) updateHead(address, index, bucket);
}
void DirectionQueues::expirePrefix(std::uint32_t address, PeerQueue& peer, std::uint8_t index, std::uint64_t now) {
    auto& bucket = peer.buckets[index];
    bool changed = false;
    while (!bucket.packets.empty() && expired(bucket.packets.front(), now)) {
        discardHead(address, peer, index, DropReason::queue_expired, false);
        changed = true;
    }
    if (changed) updateHead(address, index, bucket);
}
bool DirectionQueues::cleanupOldest(std::uint64_t now) {
    ++work_.pressureHeads;
    if (heads_.empty() || !expired(heads_.front().queue->packets.front(), now)) return false;
    const auto head = heads_.front();
    auto found = queues_.find(head.peer);
    expirePrefix(head.peer, found->second, head.bucket, now);
    if (!found->second.items) queues_.erase(found);
    return true;
}
void DirectionQueues::swapHeads(std::size_t a, std::size_t b) noexcept {
    std::swap(heads_[a], heads_[b]);
    heads_[a].queue->heapPosition = a; heads_[b].queue->heapPosition = b;
}
void DirectionQueues::repairHeap(std::size_t index) noexcept {
    const auto earlier = [this](std::size_t a, std::size_t b) {
        return heads_[a].queue->packets.front().sequence < heads_[b].queue->packets.front().sequence;
    };
    while (index && earlier(index, (index - 1) / 2)) {
        const auto parent = (index - 1) / 2; swapHeads(index, parent); index = parent;
    }
    for (;;) {
        const auto left = index * 2 + 1;
        if (left >= heads_.size()) break;
        auto child = left;
        if (left + 1 < heads_.size() && earlier(left + 1, left)) child = left + 1;
        if (!earlier(child, index)) break;
        swapHeads(index, child); index = child;
    }
}
void DirectionQueues::updateHead(std::uint32_t peer, std::uint8_t index, SubBucket& bucket) noexcept {
    auto position = bucket.heapPosition;
    if (bucket.packets.empty()) {
        if (position == kNoIndex) return;
        if (position != heads_.size() - 1) swapHeads(position, heads_.size() - 1);
        heads_.pop_back(); bucket.heapPosition = kNoIndex;
        if (position < heads_.size()) repairHeap(position);
    } else if (position == kNoIndex) {
        assert(heads_.size() < heads_.capacity());
        bucket.heapPosition = heads_.size();
        heads_.push_back({peer, index, &bucket});
        repairHeap(bucket.heapPosition);
    } else repairHeap(position);
}
void DirectionQueues::registerLease(PacketLeaseState& state) {
    std::lock_guard lock(mutex_);
    state.next = leases_;
    if (leases_) leases_->previous = &state;
    leases_ = &state;
}
void DirectionQueues::closeLease(PacketLeaseState& state, bool commit) noexcept {
    std::lock_guard lock(mutex_);
    auto found = queues_.find(state.peer.value());
    if (!state.packets.empty()) {
        assert(found != queues_.end());
        auto& peer = found->second;
        if (commit) {
            for (const auto& packet : state.packets) {
                auto& bucket = peer.buckets[packet.bucket];
                const auto bytes = static_cast<std::int64_t>(packet.size());
                changeBytes(peer, bucket, -bytes);
                bucket.heldBytes -= bytes; --bucket.heldItems;
                heldBytes_ -= bytes; --heldItems_; --totalItems_; --peer.items;
            }
        } else {
            // DRR charges local transmission attempts. No refund and no second
            // debit here, even if the bucket acquired a different owner meanwhile.
            while (!state.packets.empty()) {
                auto packet = std::prev(state.packets.end());
                auto& bucket = peer.buckets[packet->bucket];
                const auto bytes = static_cast<std::int64_t>(packet->size());
                if (bucket.owner == packetKey(*packet)) ++bucket.ownerPacketCount;
                bucket.heldBytes -= bytes; --bucket.heldItems;
                heldBytes_ -= bytes; --heldItems_;
                bucket.waitingBytes += bytes;
                bucket.packets.splice(bucket.packets.begin(), state.packets, packet);
            }
            for (std::uint8_t i = 0; i < 16; ++i) {
                updateOwner(peer, peer.buckets[i]);
                updateHead(state.peer.value(), i, peer.buckets[i]);
            }
        }
        if (!peer.items) queues_.erase(found);
    }
    if (state.previous) state.previous->next = state.next;
    else leases_ = state.next;
    if (state.next) state.next->previous = state.previous;
    state.next = state.previous = nullptr;
}
bool DirectionQueues::select(PacketLease& lease, BlockedBuckets& blocked,
    const std::function<CandidateDecision(std::size_t)>& decide) {
    assert(lease.queue_ == this && lease.state_);
    std::lock_guard lock(mutex_);
    const auto address = lease.state_->peer.value();
    auto found = queues_.find(address);
    if (found == queues_.end()) return false;
    auto& peer = found->second;
    std::size_t unavailable = 0;
    for (;;) {
        const auto index = static_cast<std::uint8_t>(peer.cursor);
        auto& bucket = peer.buckets[index];
        expirePrefix(address, peer, index, static_cast<std::uint64_t>(clock_.monotonicMs()));
        if (bucket.packets.empty() || blocked[index]) {
            peer.cursor = (peer.cursor + 1) % 16;
            if (++unavailable == 16) {
                if (!peer.items) queues_.erase(found);
                return false;
            }
            continue;
        }
        unavailable = 0;
        if (!bucket.visiting) {
            const auto maximum = (std::numeric_limits<std::uint64_t>::max)();
            bucket.deficit += std::min(quantum_, maximum - bucket.deficit);
            bucket.visiting = true;
        }
        const auto size = bucket.packets.front().size();
        if (size > bucket.deficit) {
            bucket.visiting = false;
            peer.cursor = (peer.cursor + 1) % 16;
            continue;
        }
        const auto decision = decide(size);
        if (decision == CandidateDecision::defer_for_current_request) {
            blocked.set(index);
            peer.cursor = (peer.cursor + 1) % 16;
            continue;
        }
        if (decision == CandidateDecision::permanently_oversized) {
            discardHead(address, peer, index, DropReason::oversized_for_request);
            continue;
        }
        auto packet = bucket.packets.begin();
        removeOwnerPacket(bucket, *packet);
        bucket.deficit -= size;
        bucket.waitingBytes -= static_cast<std::int64_t>(size);
        bucket.heldBytes += static_cast<std::int64_t>(size); ++bucket.heldItems;
        heldBytes_ += static_cast<std::int64_t>(size); ++heldItems_;
        lease.state_->packets.splice(lease.state_->packets.end(), bucket.packets, packet);
        updateOwner(peer, bucket);
        updateHead(address, index, bucket);
        if (bucket.packets.empty()) peer.cursor = (peer.cursor + 1) % 16;
        return true;
    }
}
bool DirectionQueues::select(PacketLease& lease) {
    BlockedBuckets blocked;
    return select(lease, blocked, [](std::size_t) { return CandidateDecision::take; });
}
std::optional<PacketBytes> DirectionQueues::takePacket(Ipv4Address peer) {
    PacketLease lease(*this, peer);
    if (!select(lease)) return std::nullopt;
    closeLease(*lease.state_, true);
    lease.queue_ = nullptr;
    return std::move(lease.state_->packets.front().bytes);
}
std::vector<Ipv4Address> DirectionQueues::nonEmptyPeers() const {
    std::lock_guard lock(mutex_);
    std::vector<Ipv4Address> peers;
    for (const auto& [address, peer] : queues_)
        if (std::any_of(peer.buckets.begin(), peer.buckets.end(), [](const auto& b) { return !b.packets.empty(); }))
            peers.emplace_back(address);
    return peers;
}
QueueStatistics DirectionQueues::statistics() const {
    std::lock_guard lock(mutex_);
    return {totalItems_, totalBytes_, maxQueueBytes_, queues_.size(), heads_.size(),
            heldBytes_, totalItems_ - heldItems_, baseline_ + totalExcess_, heads_.size(),
            drops_.totalForReason(DropReason::queue_expired), drops_.total()};
}
QueueWorkStatistics DirectionQueues::workStatistics() const {
    std::lock_guard lock(mutex_);
    return work_;
}
bool DirectionQueues::canAdmitAnotherPacket(std::size_t size) {
    std::lock_guard lock(mutex_);
    while (cleanupOldest(static_cast<std::uint64_t>(clock_.monotonicMs()))) {}
    if (size > static_cast<std::uint64_t>((std::numeric_limits<std::int64_t>::max)())) return false;
    DropReason reason{};
    const auto bytes = static_cast<std::int64_t>(size);
    if (baseline_ < 0 || bytes > maxQueueBytes_ - totalBytes_) return false;
    for (const auto& [address, peer] : queues_)
        for (std::uint8_t i = 0; i < 16; ++i)
            if (!admitsLocked(&peer, i, bytes, reason)) return false;
    return queues_.size() == maxPeers_ || admitsLocked(nullptr, 0, bytes, reason);
}
void DirectionQueues::clear() {
    std::lock_guard lock(mutex_);
    heads_.clear();
    for (auto it = queues_.begin(); it != queues_.end();) {
        auto& peer = it->second;
        for (auto& bucket : peer.buckets) {
            changeBytes(peer, bucket, -bucket.waitingBytes);
            totalItems_ -= bucket.packets.size(); peer.items -= bucket.packets.size();
            bucket.waitingBytes = 0; bucket.packets.clear(); bucket.heapPosition = kNoIndex;
            updateOwner(peer, bucket);
        }
        if (!peer.items) it = queues_.erase(it); else ++it;
    }
}
std::array<DirectionQueues::BucketSnapshot, 16> DirectionQueues::bucketSnapshot(Ipv4Address address) const {
    std::lock_guard lock(mutex_);
    std::array<BucketSnapshot, 16> result{};
    const auto found = queues_.find(address.value());
    if (found != queues_.end()) for (std::size_t i = 0; i < 16; ++i) {
        const auto& b = found->second.buckets[i];
        result[i] = {b.packets.size(), b.heldItems, b.ownerPacketCount, b.waitingBytes, b.heldBytes, b.deficit, b.owner};
    }
    return result;
}
void DirectionQueues::compactSequences() {
    std::vector<QueuedPacket*> packets;
    packets.reserve(totalItems_);
    for (auto& [address, peer] : queues_) for (auto& bucket : peer.buckets)
        for (auto& packet : bucket.packets) packets.push_back(&packet);
    for (auto* lease = leases_; lease; lease = lease->next)
        for (auto& packet : lease->packets) packets.push_back(&packet);
    std::sort(packets.begin(), packets.end(), [](const auto* a, const auto* b) { return a->sequence < b->sequence; });
    nextSequence_ = 0;
    for (auto* packet : packets) packet->sequence = nextSequence_++;
    // Monotone renumbering preserves the existing heap order.
}
void DirectionQueues::setSequenceForTesting(std::uint64_t sequence) {
    std::lock_guard lock(mutex_);
    assert(sequence >= nextSequence_);
    nextSequence_ = sequence;
}
} // namespace phpvpn
