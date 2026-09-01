// Per peer admission and the fair request scheduler, client.md "Fronty a
// zahlceni" and "Velikostni a spravedlive planovani".

#include <string>
#include <vector>

#include "core/queues/packet_queues.h"
#include "core/queues/scheduler.h"
#include "harness.h"

namespace phpvpn {
namespace {

Ipv4Address address(const char* text) {
    const auto parsed = Ipv4Address::parse(text);
    return parsed.has_value() ? *parsed : Ipv4Address(0);
}

PacketBytes packetOf(std::size_t size) {
    return PacketBytes(size, 0x5A);
}

}  // namespace

PHPVPN_TEST(queues, accepts_a_packet_within_both_limits) {
    DirectionQueues queues(1000, 100, 4);

    DropReason reason = DropReason::global_limit;
    PHPVPN_CHECK(queues.tryEnqueue(address("10.0.0.2"), packetOf(50), reason));
    PHPVPN_CHECK_EQ(queues.statistics().itemCount, std::size_t(1));
    PHPVPN_CHECK_EQ(queues.statistics().occupiedBytes, std::int64_t(50));
    PHPVPN_CHECK_EQ(queues.statistics().peerQueueCount, std::size_t(1));
}

PHPVPN_TEST(queues, the_reservation_of_every_possible_peer_is_charged) {
    // Four possible peers each reserve 100 bytes, so a single queue may hold at
    // most 1000 - 3 * 100 = 700 bytes.
    DirectionQueues queues(1000, 100, 4);
    DropReason reason = DropReason::global_limit;

    PHPVPN_CHECK(queues.tryEnqueue(address("10.0.0.2"), packetOf(700), reason));
    PHPVPN_CHECK(!queues.tryEnqueue(address("10.0.0.2"), packetOf(1), reason));
    PHPVPN_CHECK_EQ(reason, DropReason::per_peer_reservation);

    // The refused packet changed nothing.
    PHPVPN_CHECK_EQ(queues.statistics().occupiedBytes, std::int64_t(700));
    PHPVPN_CHECK_EQ(queues.statistics().itemCount, std::size_t(1));
}

PHPVPN_TEST(queues, the_default_budget_matches_the_documented_single_queue_limit) {
    // 64 MiB total, 128 KiB per peer and 252 possible peers gives a single queue
    // limit of 32.625 MiB, exactly as client.md states.
    DirectionQueues queues(67108864, 131072, 252);
    DropReason reason = DropReason::global_limit;

    const std::size_t single = 67108864 - 251 * 131072;
    PHPVPN_CHECK_EQ(single, std::size_t(34209792));

    PHPVPN_CHECK(queues.tryEnqueue(address("10.99.99.2"), packetOf(single), reason));
    PHPVPN_CHECK(!queues.tryEnqueue(address("10.99.99.2"), packetOf(1), reason));
}

PHPVPN_TEST(queues, the_global_limit_is_enforced_across_peers) {
    // Two possible peers with a small reservation, so the shared budget is the
    // binding constraint rather than the per peer share.
    DirectionQueues queues(1000, 10, 2);
    DropReason reason = DropReason::per_peer_reservation;

    PHPVPN_CHECK(queues.tryEnqueue(address("10.0.0.2"), packetOf(600), reason));
    PHPVPN_CHECK(!queues.tryEnqueue(address("10.0.0.3"), packetOf(600), reason));
    PHPVPN_CHECK_EQ(reason, DropReason::global_limit);
    PHPVPN_CHECK_EQ(queues.statistics().occupiedBytes, std::int64_t(600));
}

PHPVPN_TEST(queues, keeps_fifo_order_within_a_peer) {
    DirectionQueues queues(10000, 100, 4);
    DropReason reason = DropReason::global_limit;

    for (std::size_t index = 0; index < 5; ++index) {
        PacketBytes packet = packetOf(20 + index);
        PHPVPN_CHECK(queues.tryEnqueue(address("10.0.0.2"), std::move(packet), reason));
    }

    for (std::size_t index = 0; index < 5; ++index) {
        auto popped = queues.popFront(address("10.0.0.2"));
        PHPVPN_CHECK(popped.has_value());
        if (popped.has_value()) {
            PHPVPN_CHECK_EQ(popped->size(), std::size_t(20 + index));
        }
    }
    PHPVPN_CHECK(!queues.popFront(address("10.0.0.2")).has_value());
}

PHPVPN_TEST(queues, push_front_restores_the_original_order) {
    DirectionQueues queues(10000, 100, 4);
    DropReason reason = DropReason::global_limit;

    PHPVPN_CHECK(queues.tryEnqueue(address("10.0.0.2"), packetOf(30), reason));

    std::vector<PacketBytes> returned;
    returned.push_back(packetOf(21));
    returned.push_back(packetOf(22));
    queues.pushFront(address("10.0.0.2"), std::move(returned));

    auto first = queues.popFront(address("10.0.0.2"));
    auto second = queues.popFront(address("10.0.0.2"));
    auto third = queues.popFront(address("10.0.0.2"));
    PHPVPN_CHECK(first.has_value() && second.has_value() && third.has_value());
    if (first.has_value() && second.has_value() && third.has_value()) {
        PHPVPN_CHECK_EQ(first->size(), std::size_t(21));
        PHPVPN_CHECK_EQ(second->size(), std::size_t(22));
        PHPVPN_CHECK_EQ(third->size(), std::size_t(30));
    }
}

PHPVPN_TEST(queues, drop_counters_aggregate_by_reason_and_peer) {
    DropCounters counters;
    counters.record(address("10.0.0.2"), DropReason::global_limit, 3);
    counters.record(address("10.0.0.2"), DropReason::global_limit);
    counters.record(address("10.0.0.3"), DropReason::no_contract, 2);

    PHPVPN_CHECK_EQ(counters.total(), std::uint64_t(6));
    PHPVPN_CHECK_EQ(counters.totalForReason(DropReason::global_limit), std::uint64_t(4));
    PHPVPN_CHECK_EQ(counters.totalForReason(DropReason::no_contract), std::uint64_t(2));
    PHPVPN_CHECK_EQ(counters.entries().size(), std::size_t(2));
}

PHPVPN_TEST(scheduler, base64url_length_matches_the_encoder) {
    PHPVPN_CHECK_EQ(base64UrlTextLength(0), std::size_t(0));
    PHPVPN_CHECK_EQ(base64UrlTextLength(1), std::size_t(2));
    PHPVPN_CHECK_EQ(base64UrlTextLength(2), std::size_t(3));
    PHPVPN_CHECK_EQ(base64UrlTextLength(3), std::size_t(4));
    PHPVPN_CHECK_EQ(base64UrlTextLength(12), std::size_t(16));
    PHPVPN_CHECK_EQ(base64UrlTextLength(16), std::size_t(22));
    PHPVPN_CHECK_EQ(base64UrlTextLength(32), std::size_t(43));
}

PHPVPN_TEST(scheduler, serves_every_non_empty_queue_before_topping_up) {
    DirectionQueues queues(1000000, 1000, 8);
    DropCounters drops;
    DropReason reason = DropReason::global_limit;

    for (int index = 0; index < 4; ++index) {
        PHPVPN_CHECK(queues.tryEnqueue(address("10.0.0.2"), packetOf(100), reason));
    }
    PHPVPN_CHECK(queues.tryEnqueue(address("10.0.0.3"), packetOf(100), reason));
    PHPVPN_CHECK(queues.tryEnqueue(address("10.0.0.4"), packetOf(100), reason));

    RequestScheduler scheduler(address("10.0.0.9"));
    SchedulerLimits limits;

    const RequestPlan plan = scheduler.planNextRequest(queues, limits, drops);
    PHPVPN_CHECK_EQ(plan.records.size(), std::size_t(3));

    std::size_t total = 0;
    for (const PlannedRecord& record : plan.records) {
        PHPVPN_CHECK(!record.packets.empty());
        total += record.packets.size();
    }
    PHPVPN_CHECK_EQ(total, std::size_t(6));
    PHPVPN_CHECK_EQ(queues.statistics().itemCount, std::size_t(0));
}

PHPVPN_TEST(scheduler, respects_the_record_count_limit) {
    DirectionQueues queues(1000000, 1000, 32);
    DropCounters drops;
    DropReason reason = DropReason::global_limit;

    for (int index = 0; index < 8; ++index) {
        const std::string peer = "10.0.0." + std::to_string(2 + index);
        PHPVPN_CHECK(queues.tryEnqueue(address(peer.c_str()), packetOf(100), reason));
    }

    RequestScheduler scheduler(address("10.0.0.1"));
    SchedulerLimits limits;
    limits.maxDataRecordsPerRequest = 3;

    const RequestPlan plan = scheduler.planNextRequest(queues, limits, drops);
    PHPVPN_CHECK_EQ(plan.records.size(), std::size_t(3));
    // The peers that did not fit keep their packets for the next request.
    PHPVPN_CHECK_EQ(queues.statistics().itemCount, std::size_t(5));
}

PHPVPN_TEST(scheduler, keeps_the_body_inside_the_budget) {
    DirectionQueues queues(100000000, 1000, 8);
    DropCounters drops;
    DropReason reason = DropReason::global_limit;

    for (int index = 0; index < 200; ++index) {
        PHPVPN_CHECK(queues.tryEnqueue(address("10.0.0.2"), packetOf(1400), reason));
    }

    RequestScheduler scheduler(address("10.0.0.1"));
    SchedulerLimits limits;
    limits.maxRequestBodyBytes = 20000;

    const RequestPlan plan = scheduler.planNextRequest(queues, limits, drops);
    PHPVPN_CHECK(!plan.empty());
    PHPVPN_CHECK(plan.bodyBytes <= std::size_t(20000 - SchedulerLimits::kBodyReserveBytes));
    // The rest stays queued rather than being dropped.
    PHPVPN_CHECK(queues.statistics().itemCount > 0);
    PHPVPN_CHECK_EQ(drops.total(), std::uint64_t(0));
}

PHPVPN_TEST(scheduler, permanently_drops_a_packet_that_can_never_fit) {
    DirectionQueues queues(100000000, 1000, 8);
    DropCounters drops;
    DropReason reason = DropReason::global_limit;

    // The first packet is larger than any valid request body, the second is not.
    PHPVPN_CHECK(queues.tryEnqueue(address("10.0.0.2"), packetOf(60000), reason));
    PHPVPN_CHECK(queues.tryEnqueue(address("10.0.0.2"), packetOf(100), reason));

    RequestScheduler scheduler(address("10.0.0.1"));
    SchedulerLimits limits;
    limits.maxRequestBodyBytes = 3000;

    const RequestPlan plan = scheduler.planNextRequest(queues, limits, drops);
    PHPVPN_CHECK_EQ(plan.records.size(), std::size_t(1));
    if (!plan.records.empty()) {
        PHPVPN_CHECK_EQ(plan.records[0].packets.size(), std::size_t(1));
        PHPVPN_CHECK_EQ(plan.records[0].packets[0].size(), std::size_t(100));
    }
    PHPVPN_CHECK_EQ(drops.totalForReason(DropReason::oversized_for_request), std::uint64_t(1));
    PHPVPN_CHECK_EQ(queues.statistics().itemCount, std::size_t(0));
}

PHPVPN_TEST(scheduler, rotates_the_starting_peer_between_requests) {
    DirectionQueues queues(1000000, 1000, 8);
    DropCounters drops;
    DropReason reason = DropReason::global_limit;

    for (int round = 0; round < 4; ++round) {
        PHPVPN_CHECK(queues.tryEnqueue(address("10.0.0.2"), packetOf(100), reason));
        PHPVPN_CHECK(queues.tryEnqueue(address("10.0.0.3"), packetOf(100), reason));
        PHPVPN_CHECK(queues.tryEnqueue(address("10.0.0.4"), packetOf(100), reason));
    }

    RequestScheduler scheduler(address("10.0.0.1"));
    SchedulerLimits limits;
    // Only one record fits per request, so the rotation becomes visible.
    limits.maxDataRecordsPerRequest = 1;

    std::vector<std::uint32_t> firstPeers;
    for (int request = 0; request < 3; ++request) {
        const RequestPlan plan = scheduler.planNextRequest(queues, limits, drops);
        PHPVPN_CHECK_EQ(plan.records.size(), std::size_t(1));
        if (!plan.records.empty()) {
            firstPeers.push_back(plan.records[0].peer.value());
        }
    }

    PHPVPN_CHECK_EQ(firstPeers.size(), std::size_t(3));
    if (firstPeers.size() == 3) {
        PHPVPN_CHECK_NE(firstPeers[0], firstPeers[1]);
        PHPVPN_CHECK_NE(firstPeers[1], firstPeers[2]);
    }
}

PHPVPN_TEST(scheduler, returns_an_empty_plan_for_empty_queues) {
    DirectionQueues queues(1000000, 1000, 8);
    DropCounters drops;

    RequestScheduler scheduler(address("10.0.0.1"));
    SchedulerLimits limits;

    const RequestPlan plan = scheduler.planNextRequest(queues, limits, drops);
    PHPVPN_CHECK(plan.empty());
}

}  // namespace phpvpn
