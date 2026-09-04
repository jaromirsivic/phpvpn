// The protocol side of the network worker role.
//
// This class owns every HTTP exchange and every cryptographic operation that
// belongs to one connection. It is used from the single network thread only, so
// it holds no lock of its own; the queues and the counters it touches carry
// their own synchronisation.
//
// The verification order is normative and is never shortened: a `health`
// timestamp is used only after the GMAC verified, an `info` plaintext only after
// the GCM tag verified, and a `data` response only after `data-mac` verified
// over the exact received bytes.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "core/base/logger.h"
#include "core/base/result.h"
#include "core/config/config.h"
#include "core/crypto/crypto_provider.h"
#include "core/crypto/key_derivation.h"
#include "core/protocol/ndjson_reader.h"
#include "core/protocol/response_parser.h"
#include "core/queues/packet_queues.h"
#include "core/queues/scheduler.h"
#include "core/runtime/time_sync.h"
#include "core/runtime/traffic_counters.h"
#include "platform/iface/clock.h"
#include "platform/iface/transport.h"

namespace phpvpn {

// Result of one `data` exchange, reduced to what the state machine needs.
struct DataExchangeOutcome {
    std::size_t recordsSent = 0;
    std::size_t packetsSent = 0;
    std::size_t packetsReceived = 0;
    std::size_t packetsDropped = 0;
    std::size_t batchesRejected = 0;
    std::size_t deferredRecords = 0;
    std::size_t decodedCiphertextBytes = 0;
    bool serverHadMore = false;
};

class NetworkWorker {
public:
    NetworkWorker(CryptoProvider& crypto, Transport& transport, Clock& clock);

    // Rebinds the worker to a new active configuration and derives the long
    // lived keys once per connection.
    Status configure(ClientConfigPtr config, TrafficCounters* counters = nullptr,
                     std::function<bool()> requestCancelled = {});

    const ClientConfigPtr& config() const noexcept { return config_; }
    const ServerLimits& serverLimits() const noexcept { return serverLimits_; }
    bool hasServerLimits() const noexcept { return hasServerLimits_; }

    TimeSync& timeSync() noexcept { return timeSync_; }
    const TimeSync& timeSync() const noexcept { return timeSync_; }

    // One verified `health` exchange. On success the time snapshot is replaced
    // atomically, so a failure keeps the previous verified time.
    Result<HealthResponse> performHealth(const std::function<void()>& onSyntaxUsable = {});

    // One verified `info` exchange. Also publishes the server limits used by the
    // scheduler and by the polling interval.
    Result<InfoResponse> performInfo();

    // Verifies that the configured ACCESS_KEY is accepted, by sending an empty
    // authenticated `data` request and requiring a verified response. Payload
    // that already waits on the server is delivered into `inbound` rather than
    // being thrown away.
    Status verifyAccessKey(DirectionQueues& inbound, DropCounters& inboundDrops);

    // Assembles one `data` request from the outbound queue, sends it and feeds
    // the verified payload into the inbound queue.
    Result<DataExchangeOutcome> exchangeData(DirectionQueues& outbound,
                                             DirectionQueues& inbound,
                                             DropCounters& outboundDrops,
                                             DropCounters& inboundDrops,
                                             bool allowSending);

    // Milliseconds the worker should wait before the next `data` request, kept
    // inside the intersection of the local and the server bounds.
    std::int64_t nextPollDelayMs(bool hadTraffic);

    // Discards the derived keys and the verified time.
    void reset();

private:
    Result<std::string> newRequestId();
    Result<std::string> currentTimestamp();

    // Builds the body of a `data` request from an already planned selection.
    Result<std::string> buildDataBody(const RequestPlan& plan,
                                      const std::string& requestId,
                                      const std::string& timestamp,
                                      std::vector<std::string>& messageIds);

    // Decrypts and validates one response record, then queues its packets.
    void ingestRecord(const DataResponseRecord& record,
                      DirectionQueues& inbound,
                      DropCounters& inboundDrops,
                      DataExchangeOutcome& outcome);

    CryptoProvider& crypto_;
    Transport& transport_;
    Clock& clock_;

    ClientConfigPtr config_;
    TrafficCounters* counters_ = nullptr;
    std::function<bool()> requestCancelled_;
    KeyDerivation keys_;
    std::string normalisedServerSecret_;
    SecureBuffer semiKey_;
    SecureBuffer fullAuthKey_;

    ServerLimits serverLimits_;
    bool hasServerLimits_ = false;

    TimeSync timeSync_;
    std::unique_ptr<RequestScheduler> scheduler_;

    // Adaptive polling state, client.md "Pollingovy interval".
    std::int64_t currentPollIntervalMs_ = 80;
};

}  // namespace phpvpn
