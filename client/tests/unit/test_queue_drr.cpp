#include "queue_test_support.h"
#include "core/queues/scheduler.h"
#include "harness.h"
#include <algorithm>
#include <array>

namespace phpvpn {
using namespace test;
PHPVPN_TEST(queue_drr, equal_byte_quantum_not_equal_packet_counts_and_visit_continues) {
    for (int direction=0;direction<2;++direction) {
        QueueContext context;
        DirectionQueues queues(1000000,0,1,context.clock,context.hasher,context.drops,5000,1000);
        DropReason reason{}; const Ipv4Address peer(1);
        for (unsigned i=0;i<1000;++i) PHPVPN_CHECK(queues.tryEnqueue(peer,flowPacket(100,1),reason).valueOr(false));
        for (unsigned i=0;i<100;++i) PHPVPN_CHECK(queues.tryEnqueue(peer,flowPacket(1000,2),reason).valueOr(false));
        std::array<std::size_t,2> bytes{}, packets{};
        for (unsigned i=0;i<550;++i) {
            auto packet=queues.takePacket(peer); PHPVPN_CHECK(packet.has_value());
            if(!packet) break;
            const auto flow=keyOf(*packet).sourcePort-1;
            bytes[flow]+=packet->size(); ++packets[flow];
        }
        PHPVPN_CHECK_EQ(bytes[0],std::size_t(50000));
        PHPVPN_CHECK_EQ(bytes[1],std::size_t(50000));
        PHPVPN_CHECK_EQ(packets[0],10*packets[1]);
    }
}
PHPVPN_TEST(queue_drr, carries_deficit_and_resets_only_when_waiting_bucket_empties) {
    QueueContext context;
    DirectionQueues queues(10000,0,1,context.clock,context.hasher,context.drops,5000,1000);
    const Ipv4Address peer(1); DropReason reason{};
    for(unsigned i=0;i<3;++i) PHPVPN_CHECK(queues.tryEnqueue(peer,flowPacket(600,1),reason).valueOr(false));
    PHPVPN_CHECK(queues.tryEnqueue(peer,flowPacket(1500,2),reason).valueOr(false));
    PHPVPN_CHECK(queues.takePacket(peer).has_value());
    PHPVPN_CHECK_EQ(queues.bucketSnapshot(peer)[0].deficit,std::uint64_t(400));
    PHPVPN_CHECK(queues.takePacket(peer).has_value());
    PHPVPN_CHECK_EQ(queues.bucketSnapshot(peer)[0].deficit,std::uint64_t(800));
    PHPVPN_CHECK(queues.takePacket(peer).has_value());
    PHPVPN_CHECK_EQ(queues.bucketSnapshot(peer)[0].deficit,std::uint64_t(0));
    auto large=queues.takePacket(peer); PHPVPN_CHECK(large && large->size()==1500);
    PHPVPN_CHECK_EQ(queues.statistics().occupiedBytes,std::int64_t(0));
}
PHPVPN_TEST(queue_drr, blocked_large_head_does_not_block_small_flow_in_same_request) {
    QueueContext context;
    DirectionQueues queues(10000,0,1,context.clock,context.hasher,context.drops,5000,1000);
    DropReason reason{}; const Ipv4Address peer(1);
    PHPVPN_CHECK(queues.tryEnqueue(peer,flowPacket(900,1,1),reason).valueOr(false));
    PHPVPN_CHECK(queues.tryEnqueue(peer,flowPacket(900,1,2),reason).valueOr(false));
    PHPVPN_CHECK(queues.tryEnqueue(peer,flowPacket(100,2,3),reason).valueOr(false));
    PacketLease lease(queues,peer); BlockedBuckets blocked;
    const auto smallOnly=[](std::size_t size) { return size<=100?CandidateDecision::take:CandidateDecision::defer_for_current_request; };
    PHPVPN_CHECK(queues.select(lease,blocked,smallOnly));
    PHPVPN_CHECK_EQ(packetId(lease.packets().front().bytes),std::uint16_t(3));
    const auto deficit=queues.bucketSnapshot(peer)[0].deficit;
    for(int i=0;i<10;++i) PHPVPN_CHECK(!queues.select(lease,blocked,smallOnly));
    PHPVPN_CHECK_EQ(queues.bucketSnapshot(peer)[0].deficit,deficit);
    lease.commit();
    auto large=queues.takePacket(peer); PHPVPN_CHECK(large && packetId(*large)==1);
    PHPVPN_CHECK_EQ(queues.bucketSnapshot(peer)[0].deficit,std::uint64_t(100));
}
PHPVPN_TEST(queue_drr, retries_charge_new_selection_without_refund_or_double_debit) {
    QueueContext context;
    DirectionQueues queues(10000,0,1,context.clock,context.hasher,context.drops,5000,1000);
    DropReason reason{}; const Ipv4Address peer(1);
    for(int i=0;i<4;++i) PHPVPN_CHECK(queues.tryEnqueue(peer,flowPacket(100),reason).valueOr(false));
    PacketLease first(queues,peer); PHPVPN_CHECK(queues.select(first));
    PHPVPN_CHECK_EQ(queues.bucketSnapshot(peer)[0].deficit,std::uint64_t(900));
    first.rollback();
    PHPVPN_CHECK_EQ(queues.bucketSnapshot(peer)[0].deficit,std::uint64_t(900));
    PacketLease retry(queues,peer); PHPVPN_CHECK(queues.select(retry));
    PHPVPN_CHECK_EQ(queues.bucketSnapshot(peer)[0].deficit,std::uint64_t(800));
    retry.commit();
    PHPVPN_CHECK_EQ(queues.bucketSnapshot(peer)[0].deficit,std::uint64_t(800));
}
PHPVPN_TEST(queue_drr, heavy_flow_leaves_other_bucket_minima_and_remainders_are_exact) {
    for(const std::int64_t minimum : {1,15,16,17,1600}) {
        QueueContext context;
        const std::int64_t limit=minimum*2+3200;
        DirectionQueues queues(limit,minimum,2,context.clock,context.hasher,context.drops);
        DropReason reason{}; const Ipv4Address peer(1);
        const auto maximum=3200+minimum/16;
        PHPVPN_CHECK(queues.tryEnqueue(peer,flowPacket(static_cast<std::size_t>(maximum),1),reason).valueOr(false));
        PHPVPN_CHECK_EQ(queues.statistics().logicalBytes,limit);
        PHPVPN_CHECK(!queues.tryEnqueue(peer,flowPacket(100,1),reason).valueOr(false));
        PHPVPN_CHECK_EQ(reason,minimum>=100?DropReason::sub_bucket_reservation:DropReason::global_limit);
        if(minimum>=1600) {
            for(std::uint16_t flow=2;flow<=16;++flow)
                PHPVPN_CHECK(queues.tryEnqueue(peer,flowPacket(100,flow),reason).valueOr(false));
            PHPVPN_CHECK_EQ(queues.statistics().logicalBytes,limit);
            PHPVPN_CHECK_EQ(queues.statistics().activeSubBucketCount,std::size_t(16));
        }
    }
}
PHPVPN_TEST(queue_drr, scheduler_reaches_interactive_flow_before_bulk_backlog_is_drained) {
    QueueContext context;
    DirectionQueues queues(1000000,0,1,context.clock,context.hasher,context.drops,5000,1000);
    DropReason reason{}; const Ipv4Address peer(0x0a636303);
    for(unsigned i=0;i<100;++i) PHPVPN_CHECK(queues.tryEnqueue(peer,flowPacket(1000,1),reason).valueOr(false));
    PHPVPN_CHECK(queues.tryEnqueue(peer,flowPacket(100,2,42),reason).valueOr(false));
    RequestScheduler scheduler(Ipv4Address(0x0a636302)); SchedulerLimits limits; limits.maxRequestBodyBytes=4000;
    auto plan=scheduler.planNextRequest(queues,limits,context.drops);
    PHPVPN_CHECK_EQ(plan.records.size(),std::size_t(1));
    bool found=false; unsigned index=0;
    for(const auto& packet:plan.records.front().lease.packets()) {
        if(packetId(packet.bytes)==42) { found=true; PHPVPN_CHECK(index<=1); }
        ++index;
    }
    PHPVPN_CHECK(found);
    PHPVPN_CHECK(queues.statistics().queuedItemCount>0);
    PHPVPN_CHECK(plan.bodyBytes<=4000-SchedulerLimits::kBodyReserveBytes);
}
PHPVPN_TEST(queue_drr, one_active_bucket_is_work_conserving_across_quantum_boundaries) {
    QueueContext context;
    DirectionQueues queues(100000,0,1,context.clock,context.hasher,context.drops,5000,1000);
    const Ipv4Address peer(1); DropReason reason{};
    for(unsigned i=0;i<100;++i) PHPVPN_CHECK(queues.tryEnqueue(peer,flowPacket(600),reason).valueOr(false));
    for(unsigned i=0;i<100;++i) PHPVPN_CHECK(queues.takePacket(peer).has_value());
    PHPVPN_CHECK_EQ(queues.statistics().itemCount,std::size_t(0));
}
} // namespace phpvpn
namespace phpvpn {
PHPVPN_TEST(queue_drr, default_quantum_is_actual_tunnel_maximum_and_bounds_interactive_service) {
    test::QueueContext context;
    DirectionQueues queues(1000000,0,1,context.clock,context.hasher,context.drops);
    const Ipv4Address peer(1);DropReason reason{};
    for(unsigned i=0;i<100;++i) PHPVPN_CHECK(queues.tryEnqueue(peer,test::flowPacket(1500,1),reason).valueOr(false));
    PHPVPN_CHECK(queues.tryEnqueue(peer,test::flowPacket(100,2,42),reason).valueOr(false));
    unsigned bulkBeforeInteractive=0;
    for(unsigned i=0;i<100;++i) {
        auto packet=queues.takePacket(peer);PHPVPN_CHECK(packet.has_value());
        if(!packet) break;
        if(test::packetId(*packet)==42) break;
        ++bulkBeforeInteractive;
    }
    PHPVPN_CHECK_EQ(bulkBeforeInteractive,static_cast<unsigned>(kMaxTunnelPacketBytes/1500));
    PHPVPN_CHECK(queues.statistics().queuedItemCount>0);
}
} // namespace phpvpn
