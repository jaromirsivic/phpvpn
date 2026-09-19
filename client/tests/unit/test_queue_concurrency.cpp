#include "queue_test_support.h"
#include "harness.h"
#include <barrier>
#include <random>
#include <set>
#include <thread>

namespace phpvpn {
struct QueueTestAccess {
    static std::size_t peerBytes() { return sizeof(DirectionQueues::PeerQueue); }
    static std::size_t bucketBytes() { return sizeof(DirectionQueues::SubBucket); }
    static std::size_t headBytes() { return sizeof(DirectionQueues::Head); }
    static bool valid(DirectionQueues& queues) {
        std::lock_guard lock(queues.mutex_);
        std::int64_t totalBytes=0,totalExcess=0,peerExcess=0,heldBytes=0;
        std::size_t totalItems=0,heldItems=0,heads=0;
        for(const auto& [address,peer]:queues.queues_) {
            std::int64_t peerBytes=0; std::size_t peerItems=0;
            for(std::size_t i=0;i<16;++i) {
                const auto& b=peer.buckets[i];
                std::int64_t waiting=0,held=0; std::size_t owned=0,heldCount=0;
                std::optional<std::uint64_t> sequence;
                for(const auto& packet:b.packets) {
                    if(packet.bucket!=i || (sequence && packet.sequence<=*sequence)) return false;
                    sequence=packet.sequence; waiting+=static_cast<std::int64_t>(packet.size());
                    if(b.owner==queues.packetKey(packet)) ++owned;
                }
                for(auto* lease=queues.leases_;lease;lease=lease->next) if(lease->peer.value()==address)
                    for(const auto& packet:lease->packets) if(packet.bucket==i) { held+=static_cast<std::int64_t>(packet.size());++heldCount; }
                if(waiting!=b.waitingBytes || held!=b.heldBytes || heldCount!=b.heldItems || owned!=b.ownerPacketCount) return false;
                if(b.owner && !owned) return false;
                for(std::size_t j=i+1;j<16;++j) if(b.owner && b.owner==peer.buckets[j].owner) return false;
                if(b.packets.empty()) {
                    if(b.owner || b.deficit || b.visiting || b.heapPosition!=DirectionQueues::kNoIndex) return false;
                } else {
                    ++heads;
                    if(b.heapPosition>=queues.heads_.size() || queues.heads_[b.heapPosition].queue!=&b) return false;
                }
                peerBytes+=waiting+held; peerItems+=b.packets.size()+heldCount;
                heldBytes+=held; heldItems+=heldCount;
                totalExcess+=std::max<std::int64_t>(0,waiting+held-queues.bucketMinimum_);
            }
            if(peer.bytes!=peerBytes || peer.items!=peerItems || !peer.items) return false;
            totalBytes+=peerBytes;totalItems+=peerItems;
            peerExcess+=std::max<std::int64_t>(0,peerBytes-queues.peerMinimumBytes_);
        }
        for(std::size_t i=1;i<queues.heads_.size();++i)
            if(queues.heads_[i].queue->packets.front().sequence<queues.heads_[(i-1)/2].queue->packets.front().sequence) return false;
        return totalBytes==queues.totalBytes_ && totalItems==queues.totalItems_ && totalExcess==queues.totalExcess_ &&
            peerExcess==queues.peerExcess_ && heldBytes==queues.heldBytes_ && heldItems==queues.heldItems_ &&
            heads==queues.heads_.size() && heads<=16*queues.queues_.size() && totalBytes<=queues.maxQueueBytes_ &&
            queues.baseline_+totalExcess<=queues.maxQueueBytes_;
    }
};
using namespace test;
PHPVPN_TEST(queue_concurrency, reproducible_reference_accounting_over_mixed_operations) {
    constexpr unsigned seed=0x51f09a17; std::mt19937 random(seed);
    std::cout<<"    reference seed="<<seed<<"\n";
    QueueContext context;
    DirectionQueues queues(20000,1600,4,context.clock,context.hasher,context.drops,100,1000);
    std::array<std::optional<PacketLease>,4> leases;
    DropReason reason{};
    for(unsigned step=0;step<3000;++step) {
        const unsigned p=random()%4; const Ipv4Address peer(p+1);
        switch(random()%7) {
            case 0: case 1: case 2: {
                auto result=queues.tryEnqueue(peer,flowPacket(100+(random()%10)*100,static_cast<std::uint16_t>(1+random()%40)),reason);
                PHPVPN_REQUIRE_OK(result); break;
            }
            case 3:
                if(!leases[p]) leases[p].emplace(queues,peer);
                (void)queues.select(*leases[p]); break;
            case 4:
                if(leases[p]) { leases[p]->commit(); leases[p].reset(); } break;
            case 5:
                if(leases[p]) { leases[p]->rollback(); leases[p].reset(); } break;
            case 6: context.clock.now+=random()%20; break;
        }
        if(step==1500) queues.setSequenceForTesting(UINT64_MAX);
        PHPVPN_CHECK(QueueTestAccess::valid(queues));
    }
    for(auto& lease:leases) lease.reset();
    PHPVPN_CHECK(QueueTestAccess::valid(queues));
    queues.clear(); PHPVPN_CHECK(QueueTestAccess::valid(queues));
}
PHPVPN_TEST(queue_concurrency, barrier_producers_cannot_consume_rollback_capacity) {
    QueueContext context;
    DirectionQueues queues(10000,1600,2,context.clock,context.hasher,context.drops,5000,1000);
    DropReason reason{}; const Ipv4Address peer(1);
    for(std::uint16_t id=1;id<=10;++id) PHPVPN_CHECK(queues.tryEnqueue(peer,flowPacket(100,1,id),reason).valueOr(false));
    PacketLease lease(queues,peer);
    for(unsigned i=0;i<10;++i) PHPVPN_CHECK(queues.select(lease));
    const auto* storage=lease.packets().front().bytes.data();
    std::barrier gate(3); std::atomic<bool> valid{true};
    auto producer=[&](std::uint16_t flow) {
        gate.arrive_and_wait(); DropReason drop{};
        for(unsigned i=0;i<200;++i) {
            auto result=queues.tryEnqueue(peer,flowPacket(100,flow,static_cast<std::uint16_t>(100+i)),drop);
            if(!result) valid=false;
        }
    };
    std::thread first(producer,std::uint16_t{1}),second(producer,std::uint16_t{2});
    gate.arrive_and_wait();first.join();second.join();
    PHPVPN_CHECK(valid.load());PHPVPN_CHECK(QueueTestAccess::valid(queues));
    PHPVPN_CHECK_EQ(lease.packets().front().bytes.data(),storage);
    const auto occupied=queues.statistics().occupiedBytes;
    lease.rollback();
    PHPVPN_CHECK_EQ(queues.statistics().occupiedBytes,occupied);
    PHPVPN_CHECK_EQ(queues.statistics().inFlightBytes,std::int64_t(0));
    PHPVPN_CHECK(QueueTestAccess::valid(queues));
    // The ten original packets remain ahead of newer packets in bucket zero.
    PacketLease restored(queues,peer); BlockedBuckets blocked; for(std::size_t i=1;i<16;++i) blocked.set(i);
    for(std::uint16_t id=1;id<=10;++id) {
        PHPVPN_CHECK(queues.select(restored,blocked,[](std::size_t){return CandidateDecision::take;}));
        PHPVPN_CHECK_EQ(packetId(restored.packets().back().bytes),id);
    }
}
PHPVPN_TEST(queue_concurrency, concurrent_producers_consumer_and_diagnostics_preserve_invariants) {
    QueueContext context;
    DirectionQueues queues(50000,1600,4,context.clock,context.hasher,context.drops,5000,1000);
    std::barrier gate(6); std::atomic<unsigned> done{0},admitted{0},consumed{0};std::atomic<bool> valid{true};
    std::vector<std::thread> producers;
    for(unsigned p=0;p<4;++p) producers.emplace_back([&,p] {
        gate.arrive_and_wait(); DropReason reason{};
        for(unsigned i=0;i<1000;++i) {
            auto result=queues.tryEnqueue(Ipv4Address(p+1),flowPacket(100,static_cast<std::uint16_t>(i%32+1)),reason);
            if(!result) valid=false; else if(*result) ++admitted;
        }
        ++done;
    });
    std::thread consumer([&] {
        gate.arrive_and_wait();
        do {
            for(const auto peer:queues.nonEmptyPeers()) if(queues.takePacket(peer)) ++consumed;
            std::this_thread::yield();
        } while(done.load()!=4 || queues.statistics().queuedItemCount!=0);
    });
    gate.arrive_and_wait();
    for(unsigned sample=0;sample<100;++sample) {
        if(!QueueTestAccess::valid(queues)) valid=false;
        const auto stats=queues.statistics();
        if(stats.occupiedBytes>stats.limitBytes || stats.logicalBytes>stats.limitBytes || stats.activeSubBucketCount>16*stats.peerQueueCount) valid=false;
        std::this_thread::yield();
    }
    for(auto& producer:producers) producer.join();consumer.join();
    PHPVPN_CHECK(valid.load());PHPVPN_CHECK_EQ(admitted.load(),consumed.load());
    PHPVPN_CHECK(QueueTestAccess::valid(queues));
    PHPVPN_CHECK_EQ(queues.statistics().occupiedBytes,std::int64_t(0));
}
} // namespace phpvpn
#include <windows.h>
#include <psapi.h>
#include <chrono>
namespace phpvpn {
namespace {
std::uint64_t flowWorkingSet() {
    PROCESS_MEMORY_COUNTERS memory{};memory.cb=sizeof(memory);
    return K32GetProcessMemoryInfo(GetCurrentProcess(),&memory,sizeof(memory))?memory.WorkingSetSize:0;
}
}
PHPVPN_TEST(queue_performance, measured_flow_peer_and_packet_size_matrix_without_timing_thresholds) {
    auto crypto=createPlatformCryptoProvider();PHPVPN_REQUIRE_OK(crypto);
    std::array<std::uint8_t,32> key{};HmacFlowHasher hasher(**crypto,SecureBuffer(key));
    std::cout<<"    metadata queuedPacket="<<sizeof(QueuedPacket)<<" peer="<<QueueTestAccess::peerBytes()
        <<" bucket="<<QueueTestAccess::bucketBytes()<<" heapHead="<<QueueTestAccess::headBytes()
        <<" lease="<<sizeof(PacketLeaseState)<<" (excludes list links and allocator)\n";
    using Timer=std::chrono::steady_clock;
    const auto micros=[](auto elapsed){return std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count();};
    for(const unsigned peerCount:{1u,252u}) for(const unsigned flows:{1u,16u,256u})
    for(const std::size_t size:{std::size_t(100),kMaxTunnelPacketBytes}) {
        test::QueueClock clock;DropCounters drops;
        const auto beforeRss=flowWorkingSet();
        DirectionQueues queues(1073741824,0,peerCount,clock,hasher,drops);
        PHPVPN_CHECK_EQ(queues.statistics().peerQueueCount,std::size_t(0));
        PHPVPN_CHECK_EQ(queues.statistics().expiryIndexSize,std::size_t(0));
        DropReason reason{};
        for(unsigned p=1;p<=peerCount;++p) for(unsigned flow=1;flow<=std::min(flows,16u);++flow)
            PHPVPN_CHECK(queues.tryEnqueue(Ipv4Address(p),test::flowPacket(size,static_cast<std::uint16_t>(flow)),reason).valueOr(false));
        const auto populatedRss=flowWorkingSet();
        constexpr unsigned operations=2048;
        std::chrono::nanoseconds enqueue{},select{};
        for(unsigned op=0;op<operations;++op) {
            const Ipv4Address peer(op%peerCount+1);
            auto packet=test::flowPacket(size,static_cast<std::uint16_t>(op%flows+1));
            auto start=Timer::now();
            const auto accepted=queues.tryEnqueue(peer,std::move(packet),reason);
            enqueue+=Timer::now()-start;
            PHPVPN_CHECK(accepted.valueOr(false));
            start=Timer::now();const auto consumed=queues.takePacket(peer);select+=Timer::now()-start;
            PHPVPN_CHECK(consumed.has_value());
        }
        const auto work=queues.workStatistics();const auto indexed=queues.statistics().expiryIndexSize;
        clock.now=5000;
        const auto start=Timer::now();PHPVPN_CHECK(queues.canAdmitAnotherPacket(100));
        const auto cleanup=Timer::now()-start;
        const auto after=queues.workStatistics();
        PHPVPN_CHECK(after.pressureHeads-work.pressureHeads<=indexed+1);
        PHPVPN_CHECK_EQ(queues.statistics().itemCount,std::size_t(0));
        std::cout<<"    peers="<<peerCount<<" flows="<<flows<<" packetBytes="<<size<<" operations="<<operations
            <<" enqueueUs="<<micros(enqueue)<<" selectUs="<<micros(select)<<" cleanupUs="<<micros(cleanup)
            <<" hashCalls="<<work.hashCalls<<" classifications="<<work.classifications
            <<" pressureHeads="<<(after.pressureHeads-work.pressureHeads)<<" maxIndexed="<<indexed
            <<" rssBefore="<<beforeRss<<" rssPopulated="<<populatedRss<<" rssAfterCleanup="<<flowWorkingSet()<<"\n";
    }
}
} // namespace phpvpn
