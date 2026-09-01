#include "core/queues/packet_queues.h"

#include <algorithm>

namespace phpvpn {

const char* dropReasonName(DropReason reason) noexcept {
    switch (reason) {
        case DropReason::global_limit:
            return "global_limit";
        case DropReason::per_peer_reservation:
            return "per_peer_reservation";
        case DropReason::no_contract:
            return "no_contract";
        case DropReason::invalid_packet:
            return "invalid_packet";
        case DropReason::foreign_destination:
            return "foreign_destination";
        case DropReason::oversized_for_request:
            return "oversized_for_request";
        case DropReason::batch_invalid:
            return "batch_invalid";
        case DropReason::payload_tag_invalid:
            return "payload_tag_invalid";
        case DropReason::tunnel_stopped:
            return "tunnel_stopped";
    }
    return "unknown";
}

void DropCounters::record(Ipv4Address peer, DropReason reason, std::uint64_t count) {
    std::lock_guard<std::mutex> lock(mutex_);
    counters_[{peer.value(), static_cast<int>(reason)}] += count;
}

std::uint64_t DropCounters::total() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::uint64_t sum = 0;
    for (const auto& entry : counters_) {
        sum += entry.second;
    }
    return sum;
}

std::uint64_t DropCounters::totalForReason(DropReason reason) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::uint64_t sum = 0;
    for (const auto& entry : counters_) {
        if (entry.first.second == static_cast<int>(reason)) {
            sum += entry.second;
        }
    }
    return sum;
}

std::vector<DropCounters::Entry> DropCounters::entries() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<Entry> out;
    out.reserve(counters_.size());
    for (const auto& entry : counters_) {
        out.push_back(Entry{Ipv4Address(entry.first.first),
                            static_cast<DropReason>(entry.first.second),
                            entry.second});
    }
    return out;
}

DirectionQueues::DirectionQueues(std::int64_t maxQueueBytes,
                                 std::int64_t minPerPeerQueueBytes,
                                 std::uint32_t maxPeers)
    : maxQueueBytes_(maxQueueBytes),
      minPerPeerQueueBytes_(minPerPeerQueueBytes),
      maxPeers_(maxPeers) {}

bool DirectionQueues::admitsLocked(Ipv4Address peer,
                                   std::int64_t additionalBytes,
                                   DropReason& reason) const {
    if (totalBytes_ + additionalBytes > maxQueueBytes_) {
        reason = DropReason::global_limit;
        return false;
    }

    // Every possible peer queue keeps at least the logical reservation, so the
    // queues that do not exist yet still consume their share of the budget.
    std::int64_t reserved = 0;
    std::size_t existing = 0;
    for (const auto& entry : queues_) {
        std::int64_t bytes = entry.second.bytes;
        if (entry.first == peer.value()) {
            bytes += additionalBytes;
        }
        reserved += std::max(bytes, minPerPeerQueueBytes_);
        ++existing;
    }

    const bool peerIsNew = queues_.find(peer.value()) == queues_.end();
    if (peerIsNew) {
        reserved += std::max(additionalBytes, minPerPeerQueueBytes_);
        ++existing;
    }

    if (existing > maxPeers_) {
        reason = DropReason::per_peer_reservation;
        return false;
    }
    reserved += static_cast<std::int64_t>(maxPeers_ - existing) * minPerPeerQueueBytes_;

    if (reserved > maxQueueBytes_) {
        reason = DropReason::per_peer_reservation;
        return false;
    }
    return true;
}

bool DirectionQueues::tryEnqueue(Ipv4Address peer, PacketBytes packet, DropReason& reason) {
    const auto packetBytes = static_cast<std::int64_t>(packet.size());

    std::lock_guard<std::mutex> lock(mutex_);
    if (!admitsLocked(peer, packetBytes, reason)) {
        return false;
    }

    PeerQueue& queue = queues_[peer.value()];
    queue.bytes += packetBytes;
    queue.packets.push_back(std::move(packet));
    totalBytes_ += packetBytes;
    ++totalItems_;
    return true;
}

std::optional<std::size_t> DirectionQueues::frontPacketSize(Ipv4Address peer) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = queues_.find(peer.value());
    if (it == queues_.end() || it->second.packets.empty()) {
        return std::nullopt;
    }
    return it->second.packets.front().size();
}

std::optional<PacketBytes> DirectionQueues::popFront(Ipv4Address peer) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = queues_.find(peer.value());
    if (it == queues_.end() || it->second.packets.empty()) {
        return std::nullopt;
    }

    PacketBytes packet = std::move(it->second.packets.front());
    it->second.packets.pop_front();
    it->second.bytes -= static_cast<std::int64_t>(packet.size());
    totalBytes_ -= static_cast<std::int64_t>(packet.size());
    --totalItems_;

    // An emptied queue is removed so that it stops consuming an entry, but the
    // logical reservation of that peer stays in the budget either way.
    if (it->second.packets.empty()) {
        queues_.erase(it);
    }
    return packet;
}

void DirectionQueues::pushFront(Ipv4Address peer, std::vector<PacketBytes> packets) {
    if (packets.empty()) {
        return;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    PeerQueue& queue = queues_[peer.value()];
    for (auto it = packets.rbegin(); it != packets.rend(); ++it) {
        const auto bytes = static_cast<std::int64_t>(it->size());
        queue.bytes += bytes;
        totalBytes_ += bytes;
        ++totalItems_;
        queue.packets.push_front(std::move(*it));
    }
}

std::vector<Ipv4Address> DirectionQueues::nonEmptyPeers() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<Ipv4Address> peers;
    peers.reserve(queues_.size());
    for (const auto& entry : queues_) {
        if (!entry.second.packets.empty()) {
            peers.emplace_back(entry.first);
        }
    }
    return peers;
}

QueueStatistics DirectionQueues::statistics() const {
    std::lock_guard<std::mutex> lock(mutex_);
    QueueStatistics stats;
    stats.itemCount = totalItems_;
    stats.occupiedBytes = totalBytes_;
    stats.limitBytes = maxQueueBytes_;
    stats.peerQueueCount = queues_.size();
    return stats;
}

bool DirectionQueues::canAdmitAnotherPacket(std::size_t packetSize) const {
    std::lock_guard<std::mutex> lock(mutex_);
    // The probe is the worst case that can actually occur. While the prefix
    // still has room for another peer that is a brand new queue, because it also
    // has to pay a fresh reservation. Once every possible peer already has a
    // queue the worst case is the largest existing one.
    Ipv4Address probe(0);
    if (queues_.size() >= maxPeers_) {
        std::int64_t largest = -1;
        for (const auto& entry : queues_) {
            if (entry.second.bytes > largest) {
                largest = entry.second.bytes;
                probe = Ipv4Address(entry.first);
            }
        }
    } else {
        // Any address without a queue behaves identically here; the accounting
        // only distinguishes existing from new.
        std::uint32_t candidate = 0;
        while (queues_.find(candidate) != queues_.end()) {
            ++candidate;
        }
        probe = Ipv4Address(candidate);
    }

    DropReason reason = DropReason::global_limit;
    return admitsLocked(probe, static_cast<std::int64_t>(packetSize), reason);
}

void DirectionQueues::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    queues_.clear();
    totalBytes_ = 0;
    totalItems_ = 0;
}

}  // namespace phpvpn
