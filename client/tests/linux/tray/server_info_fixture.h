// DEVELOPMENT HARNESS ONLY. Synthetic, realistic get-server-info snapshots and
// large traffic counters shared by mock_server_info.cpp and the unit tests
// (tests/linux/unit/test_tray_linux_info.cpp). No real keys or hosts.
#pragma once
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

#include "core/protocol/info_extensions.h"
#include "core/protocol/iso8601.h"
#include "core/protocol/json.h"
#include "core/protocol/response_parser.h"
#include "core/runtime/traffic_counters.h"

namespace phpvpn::fixture {
inline constexpr char kServerInfoLocalAddress[] = "10.99.99.2";

// Deterministic lowercase 64-hex masked contract hash (splitmix64 stream).
inline std::string maskedHash(std::uint64_t seed) {
    static constexpr char digits[] = "0123456789abcdef";
    std::string hash;
    std::uint64_t state = seed * 0x9E3779B97F4A7C15ULL + 0xD1B54A32D192ED03ULL;
    while (hash.size() < 64) {
        std::uint64_t z = (state += 0x9E3779B97F4A7C15ULL);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
        z ^= z >> 31;
        for (int i = 0; i < 16 && hash.size() < 64; ++i, z >>= 4) hash.push_back(digits[z & 15]);
    }
    return hash;
}

struct ServerInfoOptions {
    std::size_t peers = 6;           // other clients, at most 252
    std::int64_t generatedAtMs = 0;  // server clock of the snapshot
    bool pretty = true;              // JSON_PRETTY_PRINT of the current server
    bool crlf = false;               // older CRLF plaintext
};

// Peer i (10.99.99.3 + i) cycles through: valid (seen 2 s ago, matching hash),
// invalid (seen 30 s ago, different hash), offline (seen 5 min ago), and every
// fourth online peer publishes no contract at all (invalid).
inline ServerInfoSnapshot serverInfoSnapshot(const ServerInfoOptions& options) {
    const auto local = *Ipv4Address::parse(kServerInfoLocalAddress);
    const auto now = options.generatedAtMs;
    JsonArray clients{JsonValue::makeObject({{"address", JsonValue::makeString(local.toString())},
                                             {"lastSeen", JsonValue::makeString(Iso8601::format(now))},
                                             {"maskedCommunicationContracts", JsonValue::makeArray({})}})};
    ServerInfoSnapshot snapshot;
    snapshot.localAddress = local;
    const auto peers = std::min<std::size_t>(options.peers, 252);
    for (std::size_t i = 0; i < peers; ++i) {
        const auto peer = Ipv4Address(local.value() + static_cast<std::uint32_t>(i) + 1);
        const int kind = static_cast<int>(i % 3);
        const std::int64_t age = kind == 0 ? 2000 : kind == 1 ? 30000 : 300000;
        const auto shared = maskedHash(i);
        std::vector<MaskedCommunicationContract> published;
        if (kind == 0 || i % 4 != 1) published.push_back({local, kind == 1 ? maskedHash(i + 1000) : shared});
        clients.push_back(JsonValue::makeObject({{"address", JsonValue::makeString(peer.toString())},
                                                 {"lastSeen", JsonValue::makeString(Iso8601::format(now - age))},
                                                 {"maskedCommunicationContracts", maskedContractsJson(published)}}));
        snapshot.localMaskedCommunicationContracts.push_back({peer, shared});
    }
    const auto document = JsonValue::makeObject(
        {{"serverVersion", JsonValue::makeString("1.0.0-synthetic")},
         {"generatedAt", JsonValue::makeString(Iso8601::format(now))},
         {"clients", JsonValue::makeArray(std::move(clients))},
         {"limits", JsonValue::makeObject({{"maxDataRecordsPerRequest", JsonValue::makeInteger(16)},
                                           {"maxRequestBodyBytes", JsonValue::makeInteger(983040)},
                                           {"maxQueueBytesPerClient", JsonValue::makeInteger(16777216)},
                                           {"queueTtlSeconds", JsonValue::makeInteger(60)},
                                           {"minDataPollIntervalMs", JsonValue::makeInteger(50)},
                                           {"maxDataPollIntervalMs", JsonValue::makeInteger(3200)}})}});
    snapshot.infoPlaintext = options.pretty ? Json::serializePretty(document, 4) : Json::serialize(document);
    if (options.crlf) {
        std::string converted;
        for (char c : snapshot.infoPlaintext) {
            if (c == '\n') converted += '\r';
            converted += c;
        }
        snapshot.infoPlaintext = std::move(converted);
    }
    return snapshot;
}

// Nine counters above 2^53 and around INT64_MAX / UINT64_MAX (client.md item 34);
// the polling invariant total == empty + withPayload holds.
inline TrafficCountersSnapshot largeCounters() {
    constexpr auto max = std::numeric_limits<std::uint64_t>::max();
    TrafficCountersSnapshot counters;
    counters.dataPollsEmpty = 9007199254740993ULL;  // 2^53 + 1
    counters.dataPollsWithPayload = max - counters.dataPollsEmpty;
    counters.dataPollsTotal = max;
    counters.serializedDataRequestBodyBytes = max;
    counters.serializedDataResponseBodyBytes = max - 1;  // the sum carries over UINT64_MAX
    counters.ipv4PacketsSent = 9223372036854775807ULL;   // INT64_MAX
    counters.ipv4PacketsReceived = 9223372036854775808ULL;
    counters.ipv4BytesSent = 999;
    counters.ipv4BytesReceived = 999995;
    return counters;
}
}  // namespace phpvpn::fixture
