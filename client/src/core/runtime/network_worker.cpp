#include "core/runtime/network_worker.h"

#include <algorithm>
#include <utility>

#include "core/crypto/canonical.h"
#include "core/protocol/base64url.h"
#include "core/protocol/data_record.h"
#include "core/protocol/iso8601.h"
#include "core/protocol/request_builder.h"

namespace phpvpn {
namespace {

constexpr const char* kComponent = "network";

// The idle interval doubles until it reaches the upper bound, so a quiet link
// stops hammering the server without adding latency once traffic returns.
constexpr std::int64_t kPollBackoffFactor = 2;

std::int64_t clampToRange(std::int64_t value, std::int64_t low, std::int64_t high) noexcept {
    if (low > high) {
        return low;
    }
    return value < low ? low : (value > high ? high : value);
}

}  // namespace

NetworkWorker::NetworkWorker(CryptoProvider& crypto, Transport& transport, Clock& clock)
    : crypto_(crypto), transport_(transport), clock_(clock), keys_(crypto) {}

Status NetworkWorker::configure(ClientConfigPtr config, TrafficCounters* counters,
                                 std::function<bool()> requestCancelled) {
    if (!config) {
        return Error(ErrorCode::config_missing, "no active configuration");
    }

    reset();
    counters_ = counters;
    requestCancelled_ = std::move(requestCancelled);
    config_ = std::move(config);
    normalisedServerSecret_ = normalizeServerSecret(config_->serverSecret);

    PHPVPN_TRY_ASSIGN(semiKey_, keys_.semiKey(normalisedServerSecret_));
    PHPVPN_TRY_ASSIGN(fullAuthKey_,
                      keys_.fullAuthKey(normalisedServerSecret_,
                                        config_->accessKey.span(),
                                        config_->virtualIp.toString()));

    scheduler_ = std::make_unique<RequestScheduler>(config_->virtualIp);

    TransportSettings settings;
    settings.serverUrl = config_->serverUrl.original;
    settings.httpHeaders = config_->httpHeaders;
    settings.trustAnyTlsCertificate = config_->trustAnyTlsCertificate;
    settings.responseTimeoutSeconds = config_->runtime.responseTimeoutSeconds;
    settings.maxResponseBodyBytes = config_->runtime.maxResponseBodyBytes;
    PHPVPN_TRY(transport_.configure(settings));

    currentPollIntervalMs_ =
        clampToRange(serverLimits_.minDataPollIntervalMs, kMinPollIntervalMs, kMaxPollIntervalMs);
    return Status::success();
}

void NetworkWorker::reset() {
    semiKey_.clear();
    fullAuthKey_.clear();
    timeSync_.reset();
    hasServerLimits_ = false;
    serverLimits_ = ServerLimits{};
    lastPacketAtMs_.reset();
    currentPollIntervalMs_ = clampToRange(serverLimits_.minDataPollIntervalMs, kMinPollIntervalMs, kMaxPollIntervalMs);
    scheduler_.reset();
}

Result<std::string> NetworkWorker::newRequestId() {
    return crypto_.randomUuidV4();
}

Result<std::string> NetworkWorker::currentTimestamp() {
    return timeSync_.requestTimestamp(clock_.monotonicMs());
}

Result<HealthResponse> NetworkWorker::performHealth(const std::function<void()>& onSyntaxUsable,
        std::optional<HttpDiagnostic>* failedDiagnostic, bool* rejectedResponse) {
    if (failedDiagnostic) failedDiagnostic->reset();
    if (rejectedResponse) *rejectedResponse = false;
    if (!config_) {
        return Error(ErrorCode::config_missing, "no active configuration");
    }

    std::string requestId;
    PHPVPN_TRY_ASSIGN(requestId, newRequestId());

    // Every health request is auth-none, including refresh and recovery.
    const std::string body = RequestBuilder::buildAuthNone(requestId);

    const std::int64_t sendMono = clock_.monotonicMs();
    if (requestCancelled_ && requestCancelled_()) return Error(ErrorCode::cancelled);
    HttpResponse response;
    auto exchange = failedDiagnostic ? transport_.postDiagnostic(HttpAction::health, body) : transport_.post(HttpAction::health, body);
    if (requestCancelled_ && requestCancelled_()) return Error(ErrorCode::cancelled);
    if (rejectedResponse) *rejectedResponse = exchange.ok() || exchange.responseProcessingFailed;
    if (failedDiagnostic && (exchange.ok() || exchange.responseProcessingFailed)) *failedDiagnostic = std::move(exchange.diagnostic);
    if (!exchange.ok()) return exchange.error();
    response = std::move(exchange).value();
    const std::int64_t receiveMono = clock_.monotonicMs();
    const std::int64_t receiveSystemUtc = clock_.systemUtcMs();

    if (response.statusCode != 200) {
        return Error(ResponseParser::mapServerError(response.statusCode, response.body),
                     "the server rejected the health request");
    }

    HealthResponse health;
    PHPVPN_TRY_ASSIGN(health, ResponseParser::parseHealth(crypto_, keys_, normalisedServerSecret_,
                                                          requestId, response.body, onSyntaxUsable));

    // The timestamp is authenticated at this point, so it may enter the clock.
    TimeSnapshot snapshot;
    PHPVPN_TRY_ASSIGN(snapshot, TimeSync::makeSnapshot(health.timestampMs, sendMono, receiveMono,
                                                       receiveSystemUtc));
    timeSync_.publish(snapshot);
    if (failedDiagnostic) failedDiagnostic->reset();
    if (rejectedResponse) *rejectedResponse = false;

    PHPVPN_LOG_INFO(kComponent, "health response verified and server time accepted");
    return health;
}

Result<InfoResponse> NetworkWorker::performInfo(std::optional<HttpDiagnostic>* failedDiagnostic, bool* rejectedResponse) {
    if (failedDiagnostic) failedDiagnostic->reset();
    if (rejectedResponse) *rejectedResponse = false;
    if (!config_) {
        return Error(ErrorCode::config_missing, "no active configuration");
    }
    if (!timeSync_.hasSnapshot()) {
        return Error(ErrorCode::time_not_synchronised, "no verified server time is available");
    }

    std::string requestId;
    PHPVPN_TRY_ASSIGN(requestId, newRequestId());
    std::string timestamp;
    PHPVPN_TRY_ASSIGN(timestamp, currentTimestamp());

    std::string body;
    PHPVPN_TRY_ASSIGN(body, RequestBuilder::buildAuthSemi(crypto_, semiKey_, timestamp, requestId));

    HttpResponse response;
    if (requestCancelled_ && requestCancelled_()) return Error(ErrorCode::cancelled);
    auto exchange = failedDiagnostic ? transport_.postDiagnostic(HttpAction::info, body) : transport_.post(HttpAction::info, body);
    if (requestCancelled_ && requestCancelled_()) return Error(ErrorCode::cancelled);
    if (rejectedResponse) *rejectedResponse = exchange.ok() || exchange.responseProcessingFailed;
    if (failedDiagnostic && (exchange.ok() || exchange.responseProcessingFailed)) *failedDiagnostic = std::move(exchange.diagnostic);
    if (!exchange.ok()) return exchange.error();
    response = std::move(exchange).value();
    if (response.statusCode != 200) {
        return Error(ResponseParser::mapServerError(response.statusCode, response.body),
                     "the server rejected the info request");
    }

    InfoResponse info;
    PHPVPN_TRY_ASSIGN(info, ResponseParser::parseInfo(crypto_, keys_, normalisedServerSecret_,
                                                      requestId, response.body));

    PHPVPN_TRY(publishLimits(info.limits));
    if (failedDiagnostic) failedDiagnostic->reset();
    if (rejectedResponse) *rejectedResponse = false;

    PHPVPN_LOG_INFO(kComponent, "info response verified and server limits published");
    return info;
}

Result<std::string> NetworkWorker::buildDataBody(const RequestPlan& plan,
                                                 const std::string& requestId,
                                                 const std::string& timestamp,
                                                 std::vector<std::string>& messageIds) {
    const std::string src = config_->virtualIp.toString();

    std::string records;
    records.reserve(plan.bodyBytes);
    messageIds.clear();
    messageIds.reserve(plan.records.size());

    for (const PlannedRecord& planned : plan.records) {
        const CommunicationContract* contract = config_->findContract(planned.peer);
        if (contract == nullptr) {
            // The scheduler only serves peers that had a contract when the
            // packet was admitted, so this can only follow a reload.
            return Error(ErrorCode::contract_missing, "no communication contract for the target");
        }

        std::string messageId;
        PHPVPN_TRY_ASSIGN(messageId, newRequestId());
        const std::string dst = planned.peer.toString();

        std::vector<std::uint8_t> iv;
        PHPVPN_TRY_ASSIGN(iv, crypto_.randomVector(kGcmIvSize));
        const std::string ivText = Base64Url::encode(iv);

        std::vector<std::uint8_t> plaintext;
        PHPVPN_TRY_ASSIGN(plaintext, PayloadBatch::encode(planned.packets));

        const std::string aad = dataAad(messageId, src, dst, ivText);
        std::vector<std::uint8_t> ciphertext;
        GcmTag tag{};
        auto sealed = crypto_.aesGcmSeal(contract->payloadKey.span(), iv, asBytes(aad), plaintext,
                                         ciphertext, tag);
        secureWipe(plaintext.data(), plaintext.size());
        PHPVPN_TRY(sealed);

        records += DataRecord::build(messageId, src, dst, ivText, Base64Url::encode(tag),
                                     Base64Url::encode(ciphertext));
        records += '\n';
        messageIds.push_back(std::move(messageId));
    }

    std::string authLine;
    PHPVPN_TRY_ASSIGN(authLine, RequestBuilder::buildAuthFull(crypto_, fullAuthKey_, src, timestamp,
                                                              requestId, records));

    std::string body;
    body.reserve(authLine.size() + 1 + records.size());
    body += authLine;
    body += '\n';
    body += records;
    return body;
}

void NetworkWorker::ingestRecord(const DataResponseRecord& record,
                                 DirectionQueues& inbound,
                                 DropCounters& inboundDrops,
                                 DataExchangeOutcome& outcome) {
    const auto sourceResult = Ipv4Address::parse(record.src);
    const auto destinationResult = Ipv4Address::parse(record.dst);
    if (!sourceResult.has_value() || !destinationResult.has_value()) {
        ++outcome.batchesRejected;
        return;
    }
    const Ipv4Address source = *sourceResult;
    const Ipv4Address destination = *destinationResult;

    // A record that is not addressed to this client is discarded without any
    // cryptographic work, client.md "Prijem dat".
    if (destination != config_->virtualIp) {
        inboundDrops.record(source, DropReason::foreign_destination);
        ++outcome.batchesRejected;
        return;
    }

    const CommunicationContract* contract = config_->findContract(source);
    if (contract == nullptr) {
        inboundDrops.record(source, DropReason::no_contract);
        ++outcome.batchesRejected;
        return;
    }

    const std::string aad = dataAad(record.messageId, record.src, record.dst, record.ivText);
    auto opened = crypto_.aesGcmOpen(contract->payloadKey.span(), record.iv, asBytes(aad),
                                     record.ciphertext, record.tag);
    if (!opened.ok()) {
        inboundDrops.record(source, DropReason::payload_tag_invalid);
        ++outcome.batchesRejected;
        return;
    }

    SecureBuffer plaintext = std::move(opened).value();
    auto packets = PayloadBatch::decodeAndValidate(plaintext.span(), source, destination);
    plaintext.clear();
    if (!packets.ok()) {
        inboundDrops.record(source, DropReason::batch_invalid);
        ++outcome.batchesRejected;
        return;
    }

    for (PacketBytes& packet : packets.value()) {
        const auto packetBytes = packet.size();
        DropReason reason = DropReason::global_limit;
        if (inbound.tryEnqueue(source, std::move(packet), reason)) {
            ++outcome.packetsReceived;
            if (counters_) counters_->recordAcceptedPacket(packetBytes);
        } else {
            inboundDrops.record(source, reason);
            ++outcome.packetsDropped;
        }
    }
}

Result<DataExchangeOutcome> NetworkWorker::exchangeData(DirectionQueues& outbound,
                                                       DirectionQueues& inbound,
                                                       DropCounters& outboundDrops,
                                                       DropCounters& inboundDrops,
                                                       bool allowSending) {
    if (!config_ || !scheduler_) {
        return Error(ErrorCode::config_missing, "no active configuration");
    }
    if (!timeSync_.hasSnapshot()) {
        return Error(ErrorCode::time_not_synchronised, "no verified server time is available");
    }

    SchedulerLimits limits;
    limits.maxRequestBodyBytes = serverLimits_.maxRequestBodyBytes;
    limits.maxDataRecordsPerRequest = serverLimits_.maxDataRecordsPerRequest;

    RequestPlan plan;
    if (allowSending) {
        plan = scheduler_->planNextRequest(outbound, limits, outboundDrops);
    }

    // Returns every planned packet to the head of its queue, in the original
    // order. Used whenever the exchange did not produce a verified acceptance.
    const auto returnEverything = [&plan, &outbound]() {
        for (PlannedRecord& planned : plan.records) {
            outbound.pushFront(planned.peer, std::move(planned.packets));
        }
        plan.records.clear();
    };

    std::string requestId;
    auto requestIdResult = newRequestId();
    if (!requestIdResult.ok()) {
        returnEverything();
        return requestIdResult.error();
    }
    requestId = std::move(requestIdResult).value();

    auto timestampResult = currentTimestamp();
    if (!timestampResult.ok()) {
        returnEverything();
        return timestampResult.error();
    }
    const std::string timestamp = std::move(timestampResult).value();

    std::vector<std::string> messageIds;
    auto bodyResult = buildDataBody(plan, requestId, timestamp, messageIds);
    if (!bodyResult.ok()) {
        returnEverything();
        return bodyResult.error();
    }

    if (requestCancelled_ && requestCancelled_()) {
        returnEverything();
        return Error(ErrorCode::cancelled);
    }
    if (counters_) {
        std::uint64_t packets = 0;
        std::uint64_t packetBytes = 0;
        for (const auto& record : plan.records) {
            packets += record.packets.size();
            for (const auto& packet : record.packets) packetBytes += packet.size();
        }
        counters_->recordRequest(!plan.records.empty(), bodyResult.value().size(), packets, packetBytes);
    }
    const auto handedOffAtMs = clock_.monotonicMs();
    const bool sentPayload = !plan.records.empty();
    auto response = transport_.post(HttpAction::data, bodyResult.value());
    if (counters_) counters_->recordResponseBody(response.responseBodyBytesRead);
    if (!response.ok()) {
        returnEverything();
        return response.error();
    }
    if (response.value().statusCode != 200) {
        const ErrorCode code =
            ResponseParser::mapServerError(response.value().statusCode, response.value().body);
        returnEverything();
        return Error(code, "the server rejected the data request");
    }

    NdjsonLimits ndjsonLimits;
    ndjsonLimits.maxNdjsonLineBytes = config_->runtime.maxNdjsonLineBytes;
    ndjsonLimits.maxDataRecordsPerResponse = config_->runtime.maxDataRecordsPerResponse;

    auto parsed = NdjsonResponseReader::parseAndVerify(crypto_, fullAuthKey_, requestId,
                                                       response.value().body, ndjsonLimits);
    if (!parsed.ok()) {
        // An unverified response removes nothing from the outbound queue and
        // forwards no payload.
        returnEverything();
        return parsed.error();
    }

    const DataResponse& data = parsed.value();

    DataExchangeOutcome outcome;
    if (sentPayload) outcome.lastPacketAtMs = handedOffAtMs;
    outcome.decodedCiphertextBytes = data.decodedCiphertextBytes;

    // Only a messageId the server confirmed as accepted may leave the queue.
    for (std::size_t index = 0; index < plan.records.size(); ++index) {
        const bool accepted =
            index < messageIds.size() &&
            std::find(data.acceptedMessageIds.begin(), data.acceptedMessageIds.end(),
                      messageIds[index]) != data.acceptedMessageIds.end();
        if (accepted) {
            outcome.packetsSent += plan.records[index].packets.size();
            ++outcome.recordsSent;
        } else {
            ++outcome.deferredRecords;
            outbound.pushFront(plan.records[index].peer, std::move(plan.records[index].packets));
        }
    }

    for (const DataResponseRecord& record : data.records) {
        const auto previous = outcome.packetsReceived;
        ingestRecord(record, inbound, inboundDrops, outcome);
        if (outcome.packetsReceived > previous) outcome.lastPacketAtMs = clock_.monotonicMs();
    }

    outcome.serverHadMore = !data.records.empty();
    outcome.completedAtMs = clock_.monotonicMs();
    return outcome;
}

void NetworkWorker::beginDataSession() {
    lastPacketAtMs_ = clock_.monotonicMs();
    currentPollIntervalMs_ = std::max<std::int64_t>(kMinPollIntervalMs, serverLimits_.minDataPollIntervalMs);
}

Status NetworkWorker::publishLimits(const ServerLimits& limits) {
    const auto low = std::max<std::int64_t>(kMinPollIntervalMs, limits.minDataPollIntervalMs);
    const auto high = std::min<std::int64_t>(kMaxPollIntervalMs, limits.maxDataPollIntervalMs);
    if (limits.minDataPollIntervalMs <= 0 || limits.maxDataPollIntervalMs < limits.minDataPollIntervalMs || low > high)
        return Error(ErrorCode::response_invalid, "unusable server polling limits");
    serverLimits_ = limits;
    hasServerLimits_ = true;
    const auto now = clock_.monotonicMs();
    const bool fast = lastPacketAtMs_ && (now < *lastPacketAtMs_ ||
        static_cast<std::uint64_t>(now) - static_cast<std::uint64_t>(*lastPacketAtMs_) < 5000);
    currentPollIntervalMs_ = fast ? low : clampToRange(currentPollIntervalMs_, low, high);
    return ok();
}

std::int64_t NetworkWorker::nextPollDelayMs(const DataExchangeOutcome& exchange) {
    const std::int64_t low =
        std::max<std::int64_t>(kMinPollIntervalMs,
                               serverLimits_.minDataPollIntervalMs);
    const std::int64_t high =
        std::min<std::int64_t>(kMaxPollIntervalMs,
                               serverLimits_.maxDataPollIntervalMs);

    if (exchange.lastPacketAtMs) lastPacketAtMs_ = exchange.lastPacketAtMs;
    const bool fast = lastPacketAtMs_ && (exchange.completedAtMs < *lastPacketAtMs_ ||
        static_cast<std::uint64_t>(exchange.completedAtMs) - static_cast<std::uint64_t>(*lastPacketAtMs_) < 5000);
    if (exchange.lastPacketAtMs || fast) {
        currentPollIntervalMs_ = low;
    } else if (currentPollIntervalMs_ < high) {
        currentPollIntervalMs_ = currentPollIntervalMs_ > high / kPollBackoffFactor
            ? high : currentPollIntervalMs_ * kPollBackoffFactor;
    }
    currentPollIntervalMs_ = clampToRange(currentPollIntervalMs_, low, high);
    return currentPollIntervalMs_;
}

Result<InfoResponse> NetworkWorker::performExtendedInfo(const InfoRequestOptions& options, std::optional<HttpDiagnostic>* failedDiagnostic, bool* rejectedResponse) {
    if (failedDiagnostic) failedDiagnostic->reset();
    if (rejectedResponse) *rejectedResponse = false;
    if (!config_) {
        return Error(ErrorCode::config_missing, "no active configuration");
    }
    if (!timeSync_.hasSnapshot()) {
        return Error(ErrorCode::time_not_synchronised, "no verified server time is available");
    }

    std::string requestId;
    PHPVPN_TRY_ASSIGN(requestId, newRequestId());
    std::string timestamp;
    PHPVPN_TRY_ASSIGN(timestamp, currentTimestamp());

    std::string body;
    PHPVPN_TRY_ASSIGN(body, RequestBuilder::buildInfoFull(crypto_, fullAuthKey_, config_->virtualIp, timestamp, requestId, options));

    HttpResponse response;
    if (requestCancelled_ && requestCancelled_()) return Error(ErrorCode::cancelled);
    auto exchange = failedDiagnostic ? transport_.postDiagnostic(HttpAction::info, body) : transport_.post(HttpAction::info, body);
    if (requestCancelled_ && requestCancelled_()) return Error(ErrorCode::cancelled);
    if (rejectedResponse) *rejectedResponse = exchange.ok() || exchange.responseProcessingFailed;
    if (failedDiagnostic && (exchange.ok() || exchange.responseProcessingFailed)) *failedDiagnostic = std::move(exchange.diagnostic);
    if (!exchange.ok()) return exchange.error();
    response = std::move(exchange).value();
    if (response.statusCode != 200) {
        return Error(ResponseParser::mapServerError(response.statusCode, response.body),
                     "the server rejected the info request");
    }

    InfoResponse info;
    PHPVPN_TRY_ASSIGN(info, ResponseParser::parseInfo(crypto_, keys_, normalisedServerSecret_,
                                                      requestId, response.body, (options.includeMaskedCommunicationContracts.value_or(false) ? std::optional<Ipv4Address>(config_->virtualIp) : std::nullopt)));

    PHPVPN_TRY(publishLimits(info.limits));
    if (failedDiagnostic) failedDiagnostic->reset();
    if (rejectedResponse) *rejectedResponse = false;

    PHPVPN_LOG_INFO(kComponent, "info response verified and server limits published");
    return info;
}

Status NetworkWorker::verifyAccessKey(DirectionQueues& inbound, DropCounters& inboundDrops) {
    // An empty authenticated request is enough: a wrong ACCESS_KEY cannot pass
    // the auth-full tag, and a verified `data-mac` proves the server accepted it.
    DirectionQueues noOutbound(0, 0, 1);
    DropCounters ignoredOutbound;

    auto outcome = exchangeData(noOutbound, inbound, ignoredOutbound, inboundDrops,
                                /*allowSending=*/false);
    if (!outcome.ok()) {
        return outcome.error();
    }
    return Status::success();
}

}  // namespace phpvpn
