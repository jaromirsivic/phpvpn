#include "queue_test_support.h"
#include "recording_tunnel.h"
#include "core/runtime/tunnel_workers.h"
#include "harness.h"
namespace phpvpn {
using namespace test;
PHPVPN_TEST(tunnel_queues, real_output_worker_keeps_peer_rotation_and_flow_fairness) {
    QueueContext context;
    DirectionQueues queues(1000000,0,2,context.clock,context.hasher,context.drops,5000,1000);
    DropReason reason{};
    for(unsigned i=0;i<100;++i) PHPVPN_CHECK(queues.tryEnqueue(Ipv4Address(1),flowPacket(1000,1),reason).valueOr(false));
    PHPVPN_CHECK(queues.tryEnqueue(Ipv4Address(1),flowPacket(100,2,42),reason).valueOr(false));
    PHPVPN_CHECK(queues.tryEnqueue(Ipv4Address(2),flowPacket(100,3,43),reason).valueOr(false));
    auto tunnel=std::make_shared<RecordingTunnel>();
    std::atomic<bool> backlogAtInteractive{false};
    tunnel->onSend=[&](std::span<const std::uint8_t> bytes) {
        if(bytes[5]==42) backlogAtInteractive=queues.statistics().queuedItemCount>0;
        return ok();
    };
    TunnelWorkerCounters counters;
    TunnelOutputWorker worker(tunnel,queues,context.drops,counters);
    worker.start();PHPVPN_CHECK(tunnel->awaitCount(3));worker.stop();
    const auto sent=tunnel->packets();
    PHPVPN_CHECK(sent.size()>=3);
    if(sent.size()>=3) {
        PHPVPN_CHECK_EQ(packetId(sent[0]),std::uint16_t(0));
        PHPVPN_CHECK_EQ(packetId(sent[1]),std::uint16_t(43));
        PHPVPN_CHECK_EQ(packetId(sent[2]),std::uint16_t(42));
    }
    PHPVPN_CHECK(backlogAtInteractive.load());
    PHPVPN_CHECK_EQ(counters.packetsWritten.load(),static_cast<std::uint64_t>(sent.size()));
    PHPVPN_CHECK_EQ(context.drops.total(),std::uint64_t(0));
    PHPVPN_CHECK_EQ(queues.statistics().inFlightBytes,std::int64_t(0));
}
PHPVPN_TEST(tunnel_queues, expired_packets_never_reach_tunnel_but_fresh_tail_does) {
    QueueContext context;
    DirectionQueues queues(10000,0,1,context.clock,context.hasher,context.drops);
    DropReason reason{};const Ipv4Address peer(1);
    PHPVPN_CHECK(queues.tryEnqueue(peer,flowPacket(100,1,1),reason).valueOr(false));
    context.clock.now=100;
    PHPVPN_CHECK(queues.tryEnqueue(peer,flowPacket(100,1,2),reason).valueOr(false));
    context.clock.now=5000;
    auto tunnel=std::make_shared<RecordingTunnel>();TunnelWorkerCounters counters;
    TunnelOutputWorker worker(tunnel,queues,context.drops,counters);
    worker.start();PHPVPN_CHECK(tunnel->awaitCount(1));worker.stop();
    auto sent=tunnel->packets();PHPVPN_CHECK_EQ(sent.size(),std::size_t(1));
    PHPVPN_CHECK_EQ(packetId(sent.front()),std::uint16_t(2));
    PHPVPN_CHECK_EQ(context.drops.totalForReason(DropReason::queue_expired),std::uint64_t(1));
    PHPVPN_CHECK_EQ(counters.packetsWritten.load(),std::uint64_t(1));
}
PHPVPN_TEST(tunnel_queues, write_failure_consumes_exactly_once_and_stops_without_retry) {
    QueueContext context;
    DirectionQueues queues(10000,0,1,context.clock,context.hasher,context.drops);
    DropReason reason{};const Ipv4Address peer(1);
    for(unsigned i=0;i<2;++i) PHPVPN_CHECK(queues.tryEnqueue(peer,flowPacket(),reason).valueOr(false));
    auto tunnel=std::make_shared<RecordingTunnel>();TunnelWorkerCounters counters;
    tunnel->onSend=[](std::span<const std::uint8_t>){return Status(Error(ErrorCode::tunnel_io_failed));};
    TunnelOutputWorker worker(tunnel,queues,context.drops,counters);
    worker.start();PHPVPN_CHECK(tunnel->awaitCount(1));worker.stop();
    PHPVPN_CHECK_EQ(tunnel->packets().size(),std::size_t(1));
    PHPVPN_CHECK_EQ(counters.packetsWritten.load(),std::uint64_t(0));
    PHPVPN_CHECK_EQ(counters.writeFailures.load(),std::uint64_t(1));
    PHPVPN_CHECK_EQ(counters.packetsDropped.load(),std::uint64_t(1));
    PHPVPN_CHECK_EQ(context.drops.totalForReason(DropReason::tunnel_stopped),std::uint64_t(1));
    PHPVPN_CHECK_EQ(queues.statistics().queuedItemCount,std::size_t(1));
    PHPVPN_CHECK_EQ(queues.statistics().occupiedBytes,std::int64_t(100));
    PHPVPN_CHECK_EQ(queues.statistics().inFlightBytes,std::int64_t(0));
}
} // namespace phpvpn
