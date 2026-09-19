#include "queue_test_support.h"
#include "harness.h"
#include <set>

namespace phpvpn {
using namespace test;
PHPVPN_TEST(flow_key, directional_tuple_and_network_serialization) {
    const auto packet = flowPacket(60, 0x1234);
    auto key = keyOf(packet);
    PHPVPN_CHECK_EQ(key.sourcePort, std::uint16_t(0x1234));
    PHPVPN_CHECK_EQ(key.destinationPort, std::uint16_t(443));
    PHPVPN_CHECK_EQ(key.protocol, std::uint8_t(17));
    const std::array<std::uint8_t, 13> expected{10,99,99,2,10,99,99,3,0x12,0x34,1,0xbb,17};
    PHPVPN_CHECK_EQ(key.serialize(), expected);
    auto reversed = key;
    std::swap(reversed.source, reversed.destination);
    std::swap(reversed.sourcePort, reversed.destinationPort);
    PHPVPN_CHECK_NE(key, reversed);
    for (int field = 0; field < 5; ++field) {
        auto changed = key;
        switch (field) {
            case 0: changed.source = Ipv4Address(1); break;
            case 1: changed.destination = Ipv4Address(1); break;
            case 2: ++changed.sourcePort; break;
            case 3: ++changed.destinationPort; break;
            default: ++changed.protocol; break;
        }
        PHPVPN_CHECK_NE(key, changed);
    }
}
PHPVPN_TEST(flow_key, safe_transport_options_fragment_and_protocol_fallbacks) {
    for (const std::uint8_t protocol : {std::uint8_t(6), std::uint8_t(17)}) {
        for (const std::size_t header : {20u,24u,60u}) {
            auto bytes = flowPacket(header + 24, 1234, 0, protocol, 0, header);
            PHPVPN_CHECK_EQ(keyOf(bytes).sourcePort, std::uint16_t(1234));
            bytes = flowPacket(header + 24, 1234, 0, protocol, 0x4000, header);
            PHPVPN_CHECK_EQ(keyOf(bytes).sourcePort, std::uint16_t(1234));
            for (const std::uint16_t fragment : {std::uint16_t(0x2000),std::uint16_t(1),std::uint16_t(0x2001)}) {
                bytes = flowPacket(header + 24, 1234, 0, protocol, fragment, header);
                PHPVPN_CHECK_EQ(keyOf(bytes).sourcePort, std::uint16_t(0));
                PHPVPN_CHECK_EQ(keyOf(bytes).destinationPort, std::uint16_t(0));
            }
        }
        for (std::size_t length = 20; length < (protocol == 6 ? 40u : 28u); ++length)
            PHPVPN_CHECK_EQ(keyOf(flowPacket(length, 1234, 0, protocol)).sourcePort, std::uint16_t(0));
    }
    auto tcp = flowPacket(40, 1234, 0, 6);
    tcp[32] = 0x40;
    PHPVPN_CHECK_EQ(keyOf(tcp).sourcePort, std::uint16_t(0));
    tcp[32] = 0xf0;
    PHPVPN_CHECK_EQ(keyOf(tcp).sourcePort, std::uint16_t(0));
    auto udp = flowPacket(40, 1234);
    udp[24] = 0; udp[25] = 7;
    PHPVPN_CHECK_EQ(keyOf(udp).sourcePort, std::uint16_t(0));
    udp[25] = 21;
    PHPVPN_CHECK_EQ(keyOf(udp).sourcePort, std::uint16_t(0));
    for (const std::uint8_t protocol : {std::uint8_t(1),std::uint8_t(253)}) {
        PHPVPN_CHECK_EQ(keyOf(flowPacket(100, 1234, 0, protocol)).sourcePort, std::uint16_t(0));
        PHPVPN_CHECK_EQ(keyOf(flowPacket(100, 1234, 0, protocol)).protocol, protocol);
    }
}
PHPVPN_TEST(flow_key, malformed_ipv4_never_reaches_classification) {
    QueueContext context;
    DirectionQueues queues(10000,0,1,context.clock,context.hasher,context.drops);
    DropReason reason{};
    for (std::size_t size = 0; size < 20; ++size) {
        auto accepted = queues.tryEnqueue(Ipv4Address(1), PacketBytes(size), reason);
        PHPVPN_REQUIRE_OK(accepted);
        PHPVPN_CHECK(!*accepted);
        PHPVPN_CHECK_EQ(reason, DropReason::invalid_packet);
    }
    auto packet = flowPacket(); packet[10] ^= 1;
    PHPVPN_CHECK(!queues.tryEnqueue(Ipv4Address(1), packet, reason).valueOr(false));
    PHPVPN_CHECK_EQ(queues.statistics().itemCount, std::size_t(0));
}
PHPVPN_TEST(flow_key, hmac_is_stable_uses_all_sixteen_slots_and_has_collisions) {
    auto crypto = createPlatformCryptoProvider();
    PHPVPN_REQUIRE_OK(crypto);
    std::array<std::uint8_t,32> secret{};
    for (std::size_t i=0;i<secret.size();++i) secret[i]=static_cast<std::uint8_t>(i);
    HmacFlowHasher hasher(**crypto, SecureBuffer(secret));
    std::array<unsigned,16> counts{};
    for (std::uint16_t flow=1;flow<=256;++flow) {
        const auto key = keyOf(flowPacket(100,flow));
        auto first = hasher.bucket(key); auto second = hasher.bucket(key);
        PHPVPN_REQUIRE_OK(first); PHPVPN_REQUIRE_OK(second);
        PHPVPN_CHECK_EQ(*first,*second);
        ++counts[*first];
        const auto digest = (*crypto)->hmacSha256(secret,key.serialize());
        PHPVPN_REQUIRE_OK(digest);
        PHPVPN_CHECK_EQ(*first, static_cast<std::uint8_t>(digest->back() & 15));
    }
    for (auto count:counts) PHPVPN_CHECK(count > 1);
}
PHPVPN_TEST(flow_key, sixteen_owners_overflow_and_hash_errors_are_atomic_in_both_directions) {
    for (int direction=0;direction<2;++direction) {
        QueueContext context;
        DirectionQueues queues(1000000,0,1,context.clock,context.hasher,context.drops);
        const Ipv4Address peer(1);
        DropReason reason{};
        for (std::uint16_t flow=1;flow<=16;++flow)
            PHPVPN_CHECK(queues.tryEnqueue(peer,flowPacket(100,flow),reason).valueOr(false));
        auto buckets=queues.bucketSnapshot(peer);
        for (std::size_t i=0;i<16;++i) {
            PHPVPN_CHECK(buckets[i].owner.has_value());
            PHPVPN_CHECK_EQ(buckets[i].owner->sourcePort, static_cast<std::uint16_t>(i+1));
        }
        PHPVPN_CHECK_EQ(context.hasher.calls.load(),0u);
        context.hasher.index=9;
        for(int i=0;i<3;++i) PHPVPN_CHECK(queues.tryEnqueue(peer,flowPacket(100,17),reason).valueOr(false));
        buckets=queues.bucketSnapshot(peer);
        PHPVPN_CHECK_EQ(buckets[9].waitingItems,std::size_t(4));
        PHPVPN_CHECK_EQ(context.hasher.calls.load(),3u);
        context.hasher.fail=true;
        PHPVPN_REQUIRE_ERROR(queues.tryEnqueue(peer,flowPacket(100,18),reason),ErrorCode::crypto_failed);
        PHPVPN_CHECK_EQ(queues.statistics().itemCount,std::size_t(19));
        PHPVPN_CHECK(queues.tryEnqueue(peer,flowPacket(100,1),reason).valueOr(false));
        PHPVPN_CHECK_EQ(context.hasher.calls.load(),4u);
    }
}
PHPVPN_TEST(flow_key, owner_release_promotion_and_bounded_collision_backlog) {
    for (const unsigned overflow : {64u,65u,10000u}) {
        QueueContext context;
        DirectionQueues queues(2000000,0,1,context.clock,context.hasher,context.drops,5000,100);
        const Ipv4Address peer(1); DropReason reason{};
        for (std::uint16_t flow=1;flow<=16;++flow)
            PHPVPN_CHECK(queues.tryEnqueue(peer,flowPacket(100,flow),reason).valueOr(false));
        for(unsigned i=0;i<overflow;++i)
            PHPVPN_CHECK(queues.tryEnqueue(peer,flowPacket(100,17),reason).valueOr(false));
        const auto before=queues.workStatistics();
        auto consumed=queues.takePacket(peer); PHPVPN_CHECK(consumed.has_value());
        const auto work=queues.workStatistics();
        PHPVPN_CHECK(work.promotionPackets-before.promotionPackets<=64);
        if(overflow>64) PHPVPN_CHECK_EQ(work.promotionPackets-before.promotionPackets,std::uint64_t(0));
        const auto bucket=queues.bucketSnapshot(peer)[0];
        PHPVPN_CHECK_EQ(bucket.waitingItems,std::size_t(overflow));
        if(overflow<=64) {
            PHPVPN_CHECK(bucket.owner.has_value());
            PHPVPN_CHECK_EQ(bucket.owner->sourcePort,std::uint16_t(17));
            PHPVPN_CHECK_EQ(bucket.ownerPacketCount,std::size_t(overflow));
        } else PHPVPN_CHECK(!bucket.owner);
    }
}
PHPVPN_TEST(flow_key, held_capacity_does_not_hold_flow_ownership_or_duplicate_it_on_rollback) {
    QueueContext context;
    DirectionQueues queues(100000,0,1,context.clock,context.hasher,context.drops,5000,100);
    const Ipv4Address peer(1); DropReason reason{};
    PHPVPN_CHECK(queues.tryEnqueue(peer,flowPacket(100,1),reason).valueOr(false));
    PacketLease lease(queues,peer); PHPVPN_CHECK(queues.select(lease));
    PHPVPN_CHECK(!queues.bucketSnapshot(peer)[0].owner);
    PHPVPN_CHECK(queues.tryEnqueue(peer,flowPacket(100,2),reason).valueOr(false));
    PHPVPN_CHECK(queues.tryEnqueue(peer,flowPacket(100,1),reason).valueOr(false));
    lease.rollback();
    auto buckets=queues.bucketSnapshot(peer);
    PHPVPN_CHECK_EQ(buckets[0].owner->sourcePort,std::uint16_t(2));
    PHPVPN_CHECK_EQ(buckets[0].ownerPacketCount,std::size_t(1));
    PHPVPN_CHECK_EQ(buckets[1].owner->sourcePort,std::uint16_t(1));
    PHPVPN_CHECK_EQ(buckets[0].waitingItems,std::size_t(2));
    // Consume restored flow 1 from bucket 0, which remains owned by flow 2.
    for (unsigned i=0;i<3;++i) PHPVPN_CHECK(queues.takePacket(peer).has_value());
    PHPVPN_CHECK_EQ(queues.statistics().peerQueueCount,std::size_t(0));
}
} // namespace phpvpn
namespace phpvpn {
PHPVPN_TEST(flow_key, promotion_never_duplicates_owner_in_another_bucket) {
    test::QueueContext context;
    DirectionQueues queues(100000,0,1,context.clock,context.hasher,context.drops,5000,100);
    const Ipv4Address peer(1);DropReason reason{};
    for(std::uint16_t flow=1;flow<=16;++flow) PHPVPN_CHECK(queues.tryEnqueue(peer,test::flowPacket(100,flow),reason).valueOr(false));
    for(unsigned i=0;i<65;++i) PHPVPN_CHECK(queues.tryEnqueue(peer,test::flowPacket(100,17),reason).valueOr(false));
    PHPVPN_CHECK(queues.takePacket(peer).has_value()); // bucket 0 now unowned, 65 overflow packets
    PHPVPN_CHECK(queues.takePacket(peer).has_value()); // frees bucket 1
    PHPVPN_CHECK(queues.tryEnqueue(peer,test::flowPacket(100,17),reason).valueOr(false));
    const auto takeBucket=[&](std::size_t index) {
        PacketLease lease(queues,peer);BlockedBuckets blocked;blocked.set();blocked.reset(index);
        const auto taken=queues.select(lease,blocked,[](std::size_t){return CandidateDecision::take;});
        lease.commit();return taken;
    };
    PHPVPN_CHECK(takeBucket(0));
    auto buckets=queues.bucketSnapshot(peer);
    PHPVPN_CHECK_EQ(buckets[0].waitingItems,std::size_t(64));PHPVPN_CHECK(!buckets[0].owner);
    PHPVPN_CHECK_EQ(buckets[1].owner->sourcePort,std::uint16_t(17));
    PHPVPN_CHECK(takeBucket(1));PHPVPN_CHECK(takeBucket(0));
    buckets=queues.bucketSnapshot(peer);
    PHPVPN_CHECK_EQ(buckets[0].owner->sourcePort,std::uint16_t(17));
    PHPVPN_CHECK_EQ(buckets[0].ownerPacketCount,std::size_t(63));
    PHPVPN_CHECK(!buckets[1].owner);
}
} // namespace phpvpn
