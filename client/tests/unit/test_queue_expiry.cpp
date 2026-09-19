#include "queue_test_support.h"
#include "core/queues/scheduler.h"
#include "harness.h"
#include <limits>

namespace phpvpn {
using namespace test;
PHPVPN_TEST(queue_expiry, exact_ttl_boundary_both_directions_and_clock_wraps) {
    for (int direction=0;direction<2;++direction)
    for (const std::uint64_t origin : {std::uint64_t(0), std::uint64_t(INT64_MAX)-2000, UINT64_MAX-2000})
    for (const std::uint64_t elapsed : {4999u,5000u,5001u}) {
        QueueContext context; context.clock.now=origin;
        DirectionQueues queues(10000,0,1,context.clock,context.hasher,context.drops);
        const Ipv4Address peer(1); DropReason reason{};
        PHPVPN_CHECK(queues.tryEnqueue(peer,flowPacket(),reason).valueOr(false));
        context.clock.now=origin+elapsed;
        context.clock.wall=INT64_MIN;
        const auto packet=queues.takePacket(peer);
        PHPVPN_CHECK_EQ(packet.has_value(),elapsed<5000);
        PHPVPN_CHECK_EQ(context.drops.totalForReason(DropReason::queue_expired),std::uint64_t(elapsed>=5000));
        PHPVPN_CHECK_EQ(queues.statistics().occupiedBytes,std::int64_t(0));
        PHPVPN_CHECK_EQ(queues.statistics().expiryIndexSize,std::size_t(0));
    }
}
PHPVPN_TEST(queue_expiry, tiny_and_maximum_ttl_and_wall_clock_independence) {
    for (const std::int64_t ttl : {std::int64_t(1),INT64_MAX}) {
        QueueContext context;
        DirectionQueues queues(10000,0,1,context.clock,context.hasher,context.drops,ttl);
        const Ipv4Address peer(1); DropReason reason{};
        PHPVPN_CHECK(queues.tryEnqueue(peer,flowPacket(),reason).valueOr(false));
        context.clock.wall=INT64_MAX;
        context.clock.now=static_cast<std::uint64_t>(ttl)-1;
        PacketLease lease(queues,peer); PHPVPN_CHECK(queues.select(lease));
        lease.rollback();
        context.clock.now=static_cast<std::uint64_t>(ttl);
        PHPVPN_CHECK(!queues.takePacket(peer));
        PHPVPN_CHECK_EQ(context.drops.total(),std::uint64_t(1));
    }
}
PHPVPN_TEST(queue_expiry, only_expired_prefix_is_removed_and_fresh_tail_keeps_fifo) {
    QueueContext context;
    DirectionQueues queues(10000,0,1,context.clock,context.hasher,context.drops);
    const Ipv4Address peer(1); DropReason reason{};
    for (std::uint16_t id=1;id<=3;++id) PHPVPN_CHECK(queues.tryEnqueue(peer,flowPacket(100,1,id),reason).valueOr(false));
    context.clock.now=100;
    for (std::uint16_t id=4;id<=6;++id) PHPVPN_CHECK(queues.tryEnqueue(peer,flowPacket(100,1,id),reason).valueOr(false));
    context.clock.now=5000;
    for (std::uint16_t id=4;id<=6;++id) {
        auto packet=queues.takePacket(peer); PHPVPN_CHECK(packet.has_value());
        if(packet) PHPVPN_CHECK_EQ(packetId(*packet),id);
    }
    PHPVPN_CHECK_EQ(context.drops.total(),std::uint64_t(3));
    PHPVPN_CHECK_EQ(context.drops.entries().front().peer,peer);
    PHPVPN_CHECK_EQ(queues.statistics().itemCount,std::size_t(0));
}
PHPVPN_TEST(queue_expiry, pressure_finds_other_peer_then_stops_as_soon_as_admission_succeeds) {
    QueueContext context;
    DirectionQueues queues(400,0,3,context.clock,context.hasher,context.drops);
    DropReason reason{};
    for (unsigned i=0;i<2;++i) PHPVPN_CHECK(queues.tryEnqueue(Ipv4Address(1),flowPacket(),reason).valueOr(false));
    context.clock.now=100;
    for (unsigned i=0;i<2;++i) PHPVPN_CHECK(queues.tryEnqueue(Ipv4Address(2),flowPacket(),reason).valueOr(false));
    context.clock.now=5000;
    PHPVPN_CHECK(queues.tryEnqueue(Ipv4Address(3),flowPacket(),reason).valueOr(false));
    PHPVPN_CHECK_EQ(context.drops.total(),std::uint64_t(2));
    PHPVPN_CHECK_EQ(queues.statistics().occupiedBytes,std::int64_t(300));
    PHPVPN_CHECK_EQ(queues.statistics().peerQueueCount,std::size_t(2));
    PHPVPN_CHECK_EQ(queues.statistics().expiryIndexSize,std::size_t(2));
    PHPVPN_CHECK(!queues.tryEnqueue(Ipv4Address(3),flowPacket(200),reason).valueOr(false));
    PHPVPN_CHECK_EQ(reason,DropReason::global_limit);
    PHPVPN_CHECK_EQ(context.drops.total(),std::uint64_t(2));
}
PHPVPN_TEST(queue_expiry, rollback_preserves_original_age_and_capacity_until_ack) {
    QueueContext context;
    DirectionQueues queues(300,0,1,context.clock,context.hasher,context.drops);
    const Ipv4Address peer(1); DropReason reason{};
    PHPVPN_CHECK(queues.tryEnqueue(peer,flowPacket(100,1,1),reason).valueOr(false));
    PacketLease lease(queues,peer); PHPVPN_CHECK(queues.select(lease));
    context.clock.now=4999;
    PHPVPN_CHECK(queues.tryEnqueue(peer,flowPacket(200,1,2),reason).valueOr(false));
    PHPVPN_CHECK(!queues.tryEnqueue(peer,flowPacket(20),reason).valueOr(false));
    PHPVPN_CHECK_EQ(queues.statistics().inFlightBytes,std::int64_t(100));
    PHPVPN_CHECK_EQ(queues.statistics().occupiedBytes,std::int64_t(300));
    context.clock.now=5000;
    PHPVPN_CHECK_EQ(lease.packets().size(),std::size_t(1)); // TTL does not cancel transport.
    lease.rollback();
    auto packet=queues.takePacket(peer); PHPVPN_CHECK(packet.has_value());
    if(packet) PHPVPN_CHECK_EQ(packetId(*packet),std::uint16_t(2));
    PHPVPN_CHECK_EQ(context.drops.total(),std::uint64_t(1));
    PHPVPN_CHECK_EQ(queues.statistics().occupiedBytes,std::int64_t(0));
}
PHPVPN_TEST(queue_expiry, sequence_compaction_includes_held_packets_and_rebuilds_oldest_order) {
    QueueContext context;
    DirectionQueues queues(300,0,2,context.clock,context.hasher,context.drops);
    DropReason reason{}; const Ipv4Address peer(1);
    queues.setSequenceForTesting(UINT64_MAX-1);
    PHPVPN_CHECK(queues.tryEnqueue(peer,flowPacket(100,1,1),reason).valueOr(false));
    PacketLease lease(queues,peer); PHPVPN_CHECK(queues.select(lease));
    const auto* storage=lease.packets().front().bytes.data();
    context.clock.now=100;
    PHPVPN_CHECK(queues.tryEnqueue(Ipv4Address(2),flowPacket(100,2,2),reason).valueOr(false));
    PHPVPN_CHECK_EQ(lease.packets().front().sequence,std::uint64_t(0));
    PHPVPN_CHECK_EQ(lease.packets().front().bytes.data(),storage);
    lease.rollback();
    context.clock.now=5000;
    PHPVPN_CHECK(queues.tryEnqueue(Ipv4Address(2),flowPacket(200,2,3),reason).valueOr(false));
    PHPVPN_CHECK_EQ(context.drops.total(),std::uint64_t(1));
    PHPVPN_CHECK_EQ(queues.statistics().occupiedBytes,std::int64_t(300));
}
PHPVPN_TEST(queue_expiry, clear_retains_live_lease_and_destructor_rollback_cannot_touch_another_epoch) {
    QueueContext context;
    DirectionQueues first(1000,0,1,context.clock,context.hasher,context.drops);
    DirectionQueues second(1000,0,1,context.clock,context.hasher,context.drops);
    DropReason reason{}; const Ipv4Address peer(1);
    PHPVPN_CHECK(first.tryEnqueue(peer,flowPacket(),reason).valueOr(false));
    {
        PacketLease lease(first,peer); PHPVPN_CHECK(first.select(lease));
        first.clear();
        PHPVPN_CHECK_EQ(first.statistics().occupiedBytes,std::int64_t(100));
        PHPVPN_CHECK_EQ(first.statistics().inFlightBytes,std::int64_t(100));
    }
    PHPVPN_CHECK_EQ(first.statistics().queuedItemCount,std::size_t(1));
    PHPVPN_CHECK_EQ(second.statistics().itemCount,std::size_t(0));
    first.clear();
    PHPVPN_CHECK_EQ(first.statistics().occupiedBytes,std::int64_t(0));
}
PHPVPN_TEST(queue_expiry, conservative_admission_cleans_expiry_and_counts_held_budget) {
    QueueContext context;
    DirectionQueues queues(400,0,1,context.clock,context.hasher,context.drops);
    DropReason reason{}; const Ipv4Address peer(1);
    PHPVPN_CHECK(queues.tryEnqueue(peer,flowPacket(400),reason).valueOr(false));
    PHPVPN_CHECK(!queues.canAdmitAnotherPacket(20));
    PacketLease lease(queues,peer); PHPVPN_CHECK(queues.select(lease));
    context.clock.now=5000;
    PHPVPN_CHECK(!queues.canAdmitAnotherPacket(20));
    lease.rollback();
    PHPVPN_CHECK(queues.canAdmitAnotherPacket(400));
    PHPVPN_CHECK_EQ(context.drops.total(),std::uint64_t(1));
}
} // namespace phpvpn
