#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <functional>
#include <mutex>
#include <thread>

#include "core/crypto/canonical.h"
#include "core/ipc/ipc_protocol.h"
#include "core/protocol/base64url.h"
#include "core/protocol/data_record.h"
#include "core/protocol/iso8601.h"
#include "core/protocol/json.h"
#include "core/runtime/vpn_engine.h"
#include "harness.h"

namespace phpvpn {
struct VpnEngineTestAccess {
    static void startBehindBarrier(VpnEngine& engine, std::function<void()> barrier) {
        engine.running_.store(true);
        engine.thread_ = std::thread([&engine, barrier] { barrier(); engine.runNetworkThread(); });
    }
    static std::size_t queuedCommands(VpnEngine& engine) {
        std::lock_guard<std::mutex> lock(engine.commandMutex_);
        return engine.commands_.size();
    }
};
namespace {
constexpr const char* kConfig = R"({"masterSetup":{"serverUrl":"https://runtime.invalid/secret-path","serverSecret":"runtime-fixture-secret","vpnCidr":"10.99.99.0/24","virtualIp":"10.99.99.2","accessKey":"0102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f20"},"communicationContracts":{"10.99.99.3":{"payloadKey":"2122232425262728292a2b2c2d2e2f303132333435363738393a3b3c3d3e3f40"}}})";
constexpr const char* kMessageId = "1f4d8c1a-3b2e-4a51-8c7d-9e0f1a2b3c4d";
constexpr const char* kTimestamp = "2026-09-04T10:00:00.000Z";

ClientConfigPtr fixtureConfig() {
    ConfigWarnings warnings;
    return parseAndValidateConfig(kConfig, warnings).value();
}

class TestClock final : public Clock {
public:
    std::atomic<std::int64_t> now{0};
    std::int64_t monotonicMs() override { return now.load(); }
    std::int64_t systemUtcMs() override { return *Iso8601::parse(kTimestamp) + now.load(); }
};

class Gate {
public:
    void block() {
        std::unique_lock<std::mutex> lock(mutex_);
        entered_ = true;
        signal_.notify_all();
        signal_.wait(lock, [&] { return released_; });
    }
    bool await() {
        std::unique_lock<std::mutex> lock(mutex_);
        return signal_.wait_for(lock, std::chrono::seconds(5), [&] { return entered_; });
    }
    void release() {
        std::lock_guard<std::mutex> lock(mutex_);
        released_ = true;
        signal_.notify_all();
    }
private:
    std::mutex mutex_;
    std::condition_variable signal_;
    bool entered_ = false;
    bool released_ = false;
};

bool eventually(const std::function<bool()>& predicate) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    do {
        if (predicate()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    } while (std::chrono::steady_clock::now() < end);
    return false;
}

class ObservedCrypto final : public CryptoProvider {
public:
    std::unique_ptr<CryptoProvider> actual = std::move(createPlatformCryptoProvider()).value();
    std::function<void()> beforeHealthCrypto;
    bool failRandom = false;
    Result<Sha256Digest> sha256(std::span<const std::uint8_t> data) override {
        if (beforeHealthCrypto && toString(data).find("health") != std::string::npos) beforeHealthCrypto();
        return actual->sha256(data);
    }
    Result<Sha256Digest> hmacSha256(std::span<const std::uint8_t> key,
                                    std::span<const std::uint8_t> data) override {
        // The health HKDF starts here, before its GMAC. Configuration derives
        // different keys and does not use this canonical purpose string.
        const auto text = toString(data);
        if (beforeHealthCrypto && text.find("health") != std::string::npos) beforeHealthCrypto();
        return actual->hmacSha256(key, data);
    }
    Status aesGcmSeal(std::span<const std::uint8_t> key, std::span<const std::uint8_t> iv,
                      std::span<const std::uint8_t> aad, std::span<const std::uint8_t> plaintext,
                      std::vector<std::uint8_t>& ciphertext, GcmTag& tag) override {
        return actual->aesGcmSeal(key, iv, aad, plaintext, ciphertext, tag);
    }
    Result<SecureBuffer> aesGcmOpen(std::span<const std::uint8_t> key,
                                    std::span<const std::uint8_t> iv,
                                    std::span<const std::uint8_t> aad,
                                    std::span<const std::uint8_t> ciphertext,
                                    std::span<const std::uint8_t> tag) override {
        return actual->aesGcmOpen(key, iv, aad, ciphertext, tag);
    }
    Status randomBytes(std::span<std::uint8_t> out) override {
        return failRandom ? Status(Error(ErrorCode::crypto_failed)) : actual->randomBytes(out);
    }
};

PacketBytes packet(std::size_t size, bool incoming) {
    PacketBytes result(size, 0);
    result[0] = 0x45;
    result[2] = static_cast<std::uint8_t>(size >> 8);
    result[3] = static_cast<std::uint8_t>(size);
    result[8] = 64;
    result[9] = 253;
    Ipv4Address::parse(incoming ? "10.99.99.3" : "10.99.99.2")->toNetworkBytes(result.data() + 12);
    Ipv4Address::parse(incoming ? "10.99.99.2" : "10.99.99.3")->toNetworkBytes(result.data() + 16);
    const auto checksum = ipv4HeaderChecksum(std::span<const std::uint8_t>(result.data(), 20));
    result[10] = static_cast<std::uint8_t>(checksum >> 8);
    result[11] = static_cast<std::uint8_t>(checksum);
    return result;
}

class ScriptedTransport final : public Transport {
public:
    explicit ScriptedTransport(CryptoProvider& crypto) : crypto_(crypto), config_(fixtureConfig()) {}
    std::function<std::optional<HttpExchangeResult>(HttpAction, std::string_view, int)> hook;
    std::function<void()> cancel;
    std::atomic<int> healthCalls{0}, infoCalls{0}, dataCalls{0};
    std::atomic<std::uint64_t> dataRequestBytes{0}, dataResponseBytes{0};
    bool badHealthTag = false, badInfo = false, badMac = false, badPacket = false, badPayloadTag = false;
    bool defer = false;
    std::vector<PacketBytes> incoming;
    Status configure(const TransportSettings&) override { return ok(); }
    void cancelCurrentRequest() override { if (cancel) cancel(); }
    HttpExchangeResult post(HttpAction action, std::string_view request) override {
        const int call = action == HttpAction::health ? ++healthCalls : action == HttpAction::info ? ++infoCalls : ++dataCalls;
        if (action == HttpAction::data) dataRequestBytes += request.size();
        if (hook) {
            auto result = hook(action, request, call);
            if (result) {
                if (action == HttpAction::data) dataResponseBytes += result->responseBodyBytesRead;
                return std::move(*result);
            }
        }
        const auto auth = Json::parse(request.substr(0, request.find('\n'))).value();
        const auto id = auth.find("requestId")->stringValue();
        std::string body = action == HttpAction::health ? health(id) : action == HttpAction::info ? info(id) : data(id, request);
        const auto bytes = body.size();
        if (action == HttpAction::data) dataResponseBytes += bytes;
        return {HttpResponse{200, std::move(body)}, bytes};
    }
private:
    std::string health(const std::string& id) {
        KeyDerivation keys(crypto_);
        auto key = keys.healthKey(config_->serverSecret, id).value();
        std::vector<std::uint8_t> iv(12, 3), ciphertext;
        const auto ivText = Base64Url::encode(iv);
        GcmTag tag{};
        const auto aad = healthAad(id, "1.0", "running", "ok", kTimestamp, ivText);
        (void)crypto_.aesGcmSeal(key.span(), iv, asBytes(aad), {}, ciphertext, tag);
        if (badHealthTag) tag[0] ^= 1;
        return "{\"serverVersion\":\"1.0\",\"status\":\"running\",\"description\":\"ok\",\"timestamp\":\"" +
               std::string(kTimestamp) + "\",\"requestId\":\"" + id + "\",\"iv\":\"" + ivText + "\",\"tag\":\"" + Base64Url::encode(tag) + "\"}";
    }
    std::string info(const std::string& id) {
        const std::string plaintext = badInfo ? "{}" : R"({"serverVersion":"1.0","generatedAt":"2026-09-04T10:00:00.000Z","limits":{"maxDataRecordsPerRequest":16,"maxRequestBodyBytes":4198400,"maxQueueBytesPerClient":16777216,"queueTtlSeconds":60,"minDataPollIntervalMs":80,"maxDataPollIntervalMs":1000}})";
        std::vector<std::uint8_t> salt(32, 4), iv(12, 5), ciphertext;
        KeyDerivation keys(crypto_);
        auto key = keys.infoKey(config_->serverSecret, salt, id).value();
        const auto ivText = Base64Url::encode(iv), saltText = Base64Url::encode(salt);
        const auto aad = infoAad(id, saltText, ivText);
        GcmTag tag{};
        (void)crypto_.aesGcmSeal(key.span(), iv, asBytes(aad), asBytes(plaintext), ciphertext, tag);
        return "{\"type\":\"info\",\"requestId\":\"" + id + "\",\"salt\":\"" + saltText + "\",\"iv\":\"" + ivText +
               "\",\"ciphertext\":\"" + Base64Url::encode(ciphertext) + "\",\"tag\":\"" + Base64Url::encode(tag) + "\"}";
    }
    std::string data(const std::string& id, std::string_view request) {
        JsonArray accepted, deferred;
        auto offset = request.find('\n') + 1;
        while (offset < request.size()) {
            const auto end = request.find('\n', offset);
            const auto line = Json::parse(request.substr(offset, end - offset)).value();
            (defer ? deferred : accepted).push_back(*line.find("messageId"));
            offset = end + 1;
        }
        const auto result = "{\"type\":\"data-result\",\"requestId\":\"" + id + "\",\"acceptedMessageIds\":" +
            Json::serialize(JsonValue::makeArray(accepted)) + ",\"deferredMessageIds\":" +
            Json::serialize(JsonValue::makeArray(deferred)) + ",\"acceptedRecords\":" + std::to_string(accepted.size()) + ",\"acceptedBytes\":0}";
        std::vector<std::pair<std::string, std::string>> macPairs;
        std::string records;
        if (!incoming.empty()) {
            auto payload = PayloadBatch::encode(incoming).value();
            if (badPacket) payload.back() ^= 1;
            // Change the embedded IPv4 header checksum, preserving a valid GCM.
            if (badPacket) payload[16] ^= 1;
            std::vector<std::uint8_t> iv(12, 8), ciphertext;
            const auto ivText = Base64Url::encode(iv);
            const auto aad = dataAad(kMessageId, "10.99.99.3", "10.99.99.2", ivText);
            GcmTag tag{};
            (void)crypto_.aesGcmSeal(config_->contracts.front().payloadKey.span(), iv,
                                   asBytes(aad), payload, ciphertext, tag);
            if (badPayloadTag) tag[0] ^= 1;
            const auto tagText = Base64Url::encode(tag);
            records = DataRecord::build(kMessageId, "10.99.99.3", "10.99.99.2", ivText, tagText, Base64Url::encode(ciphertext)) + "\n";
            macPairs.emplace_back(kMessageId, tagText);
        }
        KeyDerivation keys(crypto_);
        auto key = keys.fullAuthKey(config_->serverSecret, config_->accessKey.span(), "10.99.99.2").value();
        const auto hash = crypto_.sha256(asBytes(result)).value();
        const auto canonical = dataMacCanonical(id, macPairs, sha256Hex(hash));
        auto mac = crypto_.hmacSha256(key.span(), asBytes(canonical)).value();
        if (badMac) mac[0] ^= 1;
        return records + result + "\n{\"type\":\"data-mac\",\"requestId\":\"" + id + "\",\"tag\":\"" + Base64Url::encode(mac) + "\"}\n";
    }
    CryptoProvider& crypto_;
    ClientConfigPtr config_;
};

class IdleTunnel final : public Tunnel {
public:
    Result<TunnelReadResult> receive(std::span<std::uint8_t>, std::int64_t timeoutMs) override {
        std::unique_lock<std::mutex> lock(mutex_);
        signal_.wait_for(lock, std::chrono::milliseconds(timeoutMs), [&] { return stopped_; });
        return TunnelReadResult{stopped_ ? TunnelReadStatus::stopped : TunnelReadStatus::empty, 0};
    }
    Status send(std::span<const std::uint8_t>) override { return ok(); }
    void signalStop() override {
        std::lock_guard<std::mutex> lock(mutex_);
        stopped_ = true;
        signal_.notify_all();
    }
    std::uint64_t oversizedReceiveCount() const override { return 0; }
private:
    std::mutex mutex_;
    std::condition_variable signal_;
    bool stopped_ = false;
};

class FakeDriver final : public TunnelDriver {
public:
    std::atomic<int> starts{0};
    ErrorCode failure = ErrorCode::none;
    Status loadAndVerify(const std::wstring&) override { return failure == ErrorCode::none ? ok() : Status(Error(failure)); }
    Result<TunnelAdapterInfo> createAdapter(const std::wstring&, const std::wstring&, const std::wstring&) override { return TunnelAdapterInfo{123, "fixture-adapter", true}; }
    Result<TunnelAdapterInfo> openAdapter(const std::wstring&) override { return TunnelAdapterInfo{123, "fixture-adapter", false}; }
    Result<std::shared_ptr<Tunnel>> startSession(std::uint32_t) override { ++starts; return std::shared_ptr<Tunnel>(new IdleTunnel); }
    void endSession() override {}
    void closeAdapter() override {}
    std::uint32_t runningDriverVersion() const override { return 1; }
};

class FakeNetConfig final : public NetConfig {
public:
    Result<NetworkSnapshot> snapshot() override { return NetworkSnapshot{}; }
    Status addUnicastAddress(const UnicastAddressEntry&) override { return ok(); }
    Status removeUnicastAddress(const UnicastAddressEntry&) override { return ok(); }
    Status addRoute(const RouteEntry&) override { return ok(); }
    Status removeRoute(const RouteEntry&) override { return ok(); }
    Result<std::uint64_t> interfaceLuidForAlias(const std::wstring&) override { return std::uint64_t(123); }
    Result<bool> isHostReachableOutsideInterface(const std::string&, std::uint64_t) override { return true; }
};

struct EngineFixture {
    ObservedCrypto crypto;
    TestClock clock;
    ScriptedTransport transport{*crypto.actual};
    FakeDriver driver;
    FakeNetConfig net;
    std::filesystem::path directory = std::filesystem::temp_directory_path() /
        ("phpvpn-runtime-" + crypto.actual->randomUuidV4().value());
    VpnEngine engine{crypto, transport, clock, driver, net,
                     EnginePaths{directory / "config.json", directory / "journal.json", directory.wstring()}};
    EngineFixture() {
        std::filesystem::create_directories(directory);
        std::ofstream(directory / "config.json") << kConfig;
    }
    ~EngineFixture() {
        engine.shutdown();
        std::filesystem::remove_all(directory);
    }
};

bool allChecks(const StatusSnapshot& status, CheckpointResult url,
               CheckpointResult secret, CheckpointResult access) {
    return status.serverUrlCheck == url && status.serverSecretCheck == secret && status.accessKeyCheck == access;
}
}  // namespace

PHPVPN_TEST(runtime, worker_counts_handoff_retry_and_verified_queue_admission) {
    ObservedCrypto crypto;
    ScriptedTransport transport(*crypto.actual);
    TestClock clock;
    NetworkWorker worker(crypto, transport, clock);
    TrafficCounters counters;
    PHPVPN_REQUIRE_OK(worker.configure(fixtureConfig(), &counters));
    PHPVPN_REQUIRE_OK(worker.performHealth());
    PHPVPN_REQUIRE_OK(worker.performInfo());
    PHPVPN_CHECK_EQ(counters.snapshot().serializedDataRequestBodyBytes, std::uint64_t(0));
    DirectionQueues outgoing(100000, 0, 1), incoming(100000, 0, 1);
    DropCounters outDrops, inDrops;
    PHPVPN_REQUIRE_OK(worker.verifyAccessKey(incoming, inDrops));
    PHPVPN_CHECK_EQ(counters.snapshot().dataPollsEmpty, std::uint64_t(1));
    DropReason reason{};
    const auto peer = *Ipv4Address::parse("10.99.99.3");
    PHPVPN_CHECK(outgoing.tryEnqueue(peer, packet(100, false), reason));
    transport.defer = true;
    PHPVPN_REQUIRE_OK(worker.exchangeData(outgoing, incoming, outDrops, inDrops, true));
    transport.defer = false;
    transport.incoming = {packet(40, true), packet(100, true), packet(1500, true)};
    PHPVPN_REQUIRE_OK(worker.exchangeData(outgoing, incoming, outDrops, inDrops, true));
    auto value = counters.snapshot();
    PHPVPN_CHECK_EQ(value.dataPollsTotal, std::uint64_t(3));
    PHPVPN_CHECK_EQ(value.dataPollsWithPayload, std::uint64_t(2));
    PHPVPN_CHECK_EQ(value.ipv4PacketsSent, std::uint64_t(2));
    PHPVPN_CHECK_EQ(value.ipv4BytesSent, std::uint64_t(200));
    PHPVPN_CHECK_EQ(value.ipv4PacketsReceived, std::uint64_t(3));
    PHPVPN_CHECK_EQ(value.ipv4BytesReceived, std::uint64_t(1640));
    PHPVPN_CHECK_EQ(value.serializedDataRequestBodyBytes, transport.dataRequestBytes.load());
    PHPVPN_CHECK_EQ(value.serializedDataResponseBodyBytes, transport.dataResponseBytes.load());
    // All validation rejection paths, followed by queue backpressure.
    transport.badMac = true;
    PHPVPN_CHECK(!worker.exchangeData(outgoing, incoming, outDrops, inDrops, true).ok());
    transport.badMac = false;
    transport.badPayloadTag = true;
    PHPVPN_REQUIRE_OK(worker.exchangeData(outgoing, incoming, outDrops, inDrops, true));
    transport.badPayloadTag = false;
    transport.badPacket = true;
    PHPVPN_REQUIRE_OK(worker.exchangeData(outgoing, incoming, outDrops, inDrops, true));
    transport.badPacket = false;
    DirectionQueues full(0, 0, 1);
    PHPVPN_REQUIRE_OK(worker.exchangeData(outgoing, full, outDrops, inDrops, true));
    PHPVPN_CHECK_EQ(counters.snapshot().ipv4PacketsReceived, std::uint64_t(3));
    PHPVPN_CHECK_EQ(counters.snapshot().serializedDataResponseBodyBytes, transport.dataResponseBytes.load());
    const auto countBeforeBuildFailure = counters.snapshot().dataPollsTotal;
    crypto.failRandom = true;
    PHPVPN_CHECK(!worker.exchangeData(outgoing, incoming, outDrops, inDrops, true).ok());
    PHPVPN_CHECK_EQ(counters.snapshot().dataPollsTotal, countBeforeBuildFailure);
}

PHPVPN_TEST(runtime, worker_accounts_partial_bodies_once_on_every_error) {
    ObservedCrypto crypto;
    ScriptedTransport transport(*crypto.actual);
    TestClock clock;
    NetworkWorker worker(crypto, transport, clock);
    TrafficCounters counters;
    PHPVPN_REQUIRE_OK(worker.configure(fixtureConfig(), &counters));
    PHPVPN_REQUIRE_OK(worker.performHealth());
    DirectionQueues outgoing(10000, 0, 1), incoming(10000, 0, 1);
    DropCounters outDrops, inDrops;
    std::uint64_t expected = 0;
    for (auto code : {ErrorCode::transport_timeout, ErrorCode::cancelled,
                      ErrorCode::transport_receive_failed, ErrorCode::transport_response_too_large}) {
        transport.hook = [code](HttpAction, std::string_view, int) -> std::optional<HttpExchangeResult> {
            return HttpExchangeResult{Error(code, "private raw error"), 123};
        };
        PHPVPN_CHECK(!worker.exchangeData(outgoing, incoming, outDrops, inDrops, true).ok());
        expected += 123;
        PHPVPN_CHECK_EQ(counters.snapshot().serializedDataResponseBodyBytes, expected);
    }
    transport.hook = [](HttpAction, std::string_view, int) -> std::optional<HttpExchangeResult> {
        return HttpExchangeResult{HttpResponse{503, "unavailable"}, 11};
    };
    PHPVPN_CHECK(!worker.exchangeData(outgoing, incoming, outDrops, inDrops, true).ok());
    PHPVPN_CHECK_EQ(counters.snapshot().serializedDataResponseBodyBytes, expected + 11);
}

PHPVPN_TEST(runtime, cancelled_before_transport_handoff_does_not_count_a_request) {
    ObservedCrypto crypto;
    ScriptedTransport transport(*crypto.actual);
    TestClock clock;
    NetworkWorker worker(crypto, transport, clock);
    TrafficCounters counters;
    bool cancelled = false;
    PHPVPN_REQUIRE_OK(worker.configure(fixtureConfig(), &counters, [&] { return cancelled; }));
    PHPVPN_REQUIRE_OK(worker.performHealth());
    cancelled = true;
    DirectionQueues outgoing(10000, 0, 1), incoming(10000, 0, 1);
    DropCounters outDrops, inDrops;
    DropReason reason{};
    PHPVPN_CHECK(outgoing.tryEnqueue(*Ipv4Address::parse("10.99.99.3"), packet(100, false), reason));
    PHPVPN_CHECK(!worker.exchangeData(outgoing, incoming, outDrops, inDrops, true).ok());
    PHPVPN_CHECK_EQ(transport.dataCalls.load(), 0);
    PHPVPN_CHECK_EQ(counters.snapshot().dataPollsTotal, std::uint64_t(0));
    PHPVPN_CHECK_EQ(outgoing.statistics().itemCount, std::size_t(1));
}

PHPVPN_TEST(runtime, simultaneous_pending_connects_share_acceptance_and_one_failed_attempt) {
    EngineFixture fixture;
    Gate networkStart, health;
    fixture.transport.hook = [&](HttpAction action, std::string_view, int) -> std::optional<HttpExchangeResult> {
        if (action == HttpAction::health) {
            health.block();
            return HttpExchangeResult{Error(ErrorCode::transport_dns_failed), 0};
        }
        return {};
    };
    fixture.transport.cancel = [&] { networkStart.release(); health.release(); };
    VpnEngineTestAccess::startBehindBarrier(fixture.engine, [&] { networkStart.block(); });
    PHPVPN_CHECK(networkStart.await());
    std::atomic<int> entered{0}, returned{0};
    std::atomic<bool> accepted{true};
    std::vector<std::thread> callers;
    for (int i = 0; i < 8; ++i) callers.emplace_back([&] {
        ++entered;
        if (!fixture.engine.connect().ok()) accepted.store(false);
        ++returned;
    });
    PHPVPN_CHECK(eventually([&] { return entered.load() == 8 && VpnEngineTestAccess::queuedCommands(fixture.engine) != 0; }));
    // The worker barrier makes the pending window deterministic. No caller
    // may acknowledge a stale disconnected snapshot before it is released.
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    PHPVPN_CHECK_EQ(returned.load(), 0);
    PHPVPN_CHECK_EQ(VpnEngineTestAccess::queuedCommands(fixture.engine), std::size_t(1));
    networkStart.release();
    PHPVPN_CHECK(health.await());
    for (auto& caller : callers) caller.join();
    PHPVPN_CHECK(accepted.load());
    PHPVPN_CHECK(fixture.engine.status().state == ConnectionState::connecting);
    health.release();
    PHPVPN_CHECK(eventually([&] { return fixture.engine.status().state == ConnectionState::recovering; }));
    PHPVPN_CHECK_EQ(fixture.transport.healthCalls.load(), 1);
}

PHPVPN_TEST(runtime, simultaneous_status_reads_do_not_block_live_verified_packet_traffic) {
    EngineFixture fixture;
    Gate startTraffic, finishTraffic;
    fixture.transport.incoming = {packet(100, true)};
    fixture.transport.hook = [&](HttpAction action, std::string_view, int call) -> std::optional<HttpExchangeResult> {
        if (action == HttpAction::data) {
            if (call == 2) startTraffic.block();
            if (call == 200) finishTraffic.block();
            fixture.clock.now += 1000;
        }
        return {};
    };
    fixture.transport.cancel = [&] { startTraffic.release(); finishTraffic.release(); };
    PHPVPN_REQUIRE_OK(fixture.engine.start());
    PHPVPN_REQUIRE_OK(fixture.engine.connect());
    PHPVPN_CHECK(startTraffic.await());
    std::atomic<bool> stop{false}, valid{true};
    std::atomic<std::uint64_t> samples{0};
    std::vector<std::thread> readers;
    for (int i = 0; i < 3; ++i) readers.emplace_back([&] {
        while (!stop.load()) {
            const auto status = fixture.engine.status();
            if (status.counters.dataPollsTotal != status.counters.dataPollsEmpty + status.counters.dataPollsWithPayload ||
                status.counters.ipv4BytesReceived != status.counters.ipv4PacketsReceived * 100)
                valid.store(false);
            ++samples;
        }
    });
    startTraffic.release();
    const bool advanced = finishTraffic.await();
    stop.store(true);
    for (auto& reader : readers) reader.join();
    PHPVPN_CHECK(advanced);
    PHPVPN_CHECK(valid.load());
    PHPVPN_CHECK(samples.load() > 0);
    const auto status = fixture.engine.status();
    PHPVPN_CHECK_EQ(status.counters.dataPollsTotal, std::uint64_t(200));
    PHPVPN_CHECK_EQ(status.counters.ipv4PacketsReceived, std::uint64_t(199));
    PHPVPN_CHECK_EQ(fixture.driver.starts.load(), 1);
    PHPVPN_CHECK_EQ(fixture.transport.healthCalls.load(), 1);
    PHPVPN_REQUIRE_OK(fixture.engine.disconnect());
    PHPVPN_CHECK(eventually([&] { return fixture.engine.status().state == ConnectionState::disconnected; }));
}

PHPVPN_TEST(runtime, health_syntax_is_published_before_key_derivation) {
    EngineFixture fixture;
    Gate cryptoGate;
    fixture.crypto.beforeHealthCrypto = [&] { cryptoGate.block(); };
    fixture.driver.failure = ErrorCode::tunnel_library_invalid;
    PHPVPN_REQUIRE_OK(fixture.engine.start());
    PHPVPN_REQUIRE_OK(fixture.engine.connect());
    const bool reached = cryptoGate.await();
    const auto during = fixture.engine.status();
    PHPVPN_CHECK(reached);
    PHPVPN_CHECK(allChecks(during, CheckpointResult::success, CheckpointResult::pending, CheckpointResult::pending));
    PHPVPN_CHECK(during.state == ConnectionState::connecting);
    for (int i = 0; i < 10; ++i) PHPVPN_CHECK(fixture.engine.connect().ok());
    cryptoGate.release();
    PHPVPN_CHECK(eventually([&] { return fixture.engine.status().state == ConnectionState::error; }));
    const auto ended = fixture.engine.status();
    PHPVPN_CHECK(allChecks(ended, CheckpointResult::success, CheckpointResult::success, CheckpointResult::success));
    PHPVPN_CHECK_EQ(ended.lastError->code, std::string("tunnel_start_failed"));
    PHPVPN_CHECK_EQ(fixture.transport.healthCalls.load(), 1);
}

PHPVPN_TEST(runtime, bootstrap_errors_classify_actual_operation_and_checkpoint) {
    struct Case { HttpAction action; ErrorCode error; int http; bool badHealth; bool badInfo; const char* code; CheckpointResult url; CheckpointResult secret; CheckpointResult access; bool recoverable; };
    const auto p = CheckpointResult::pending, s = CheckpointResult::success, f = CheckpointResult::failed;
    const Case cases[] = {
        {HttpAction::health, ErrorCode::transport_timeout, 0, false, false, "communication_timeout", f,p,p,true},
        {HttpAction::health, ErrorCode::transport_dns_failed, 0, false, false, "endpoint_unavailable_or_invalid", f,p,p,true},
        {HttpAction::health, ErrorCode::none, 200, false, false, "endpoint_unavailable_or_invalid", f,p,p,true},
        {HttpAction::health, ErrorCode::none, 0, true, false, "server_verification_failed", s,f,p,false},
        {HttpAction::info, ErrorCode::transport_timeout, 0, false, false, "communication_timeout", s,f,p,true},
        {HttpAction::info, ErrorCode::none, 500, false, false, "communication_failed", s,f,p,true},
        {HttpAction::info, ErrorCode::none, 0, false, true, "server_verification_failed", s,f,p,false},
        {HttpAction::data, ErrorCode::transport_timeout, 0, false, false, "communication_timeout", s,s,f,true},
        {HttpAction::data, ErrorCode::none, 401, false, false, "access_key_rejected", s,s,f,false},
        {HttpAction::data, ErrorCode::none, 503, false, false, "communication_failed", s,s,f,true},
    };
    for (const auto& item : cases) {
        EngineFixture fixture;
        fixture.transport.badHealthTag = item.badHealth;
        fixture.transport.badInfo = item.badInfo;
        fixture.transport.hook = [item](HttpAction action, std::string_view, int) -> std::optional<HttpExchangeResult> {
            if (action != item.action) return {};
            if (item.error != ErrorCode::none) return HttpExchangeResult{Error(item.error, "private endpoint details"), 17};
            if (item.http) return HttpExchangeResult{HttpResponse{item.http, "bad"}, 3};
            return {};
        };
        PHPVPN_REQUIRE_OK(fixture.engine.start());
        PHPVPN_REQUIRE_OK(fixture.engine.connect());
        PHPVPN_CHECK(eventually([&] { const auto state = fixture.engine.status().state; return state == ConnectionState::error || state == ConnectionState::recovering; }));
        const auto status = fixture.engine.status();
        PHPVPN_CHECK(allChecks(status, item.url, item.secret, item.access));
        PHPVPN_CHECK(status.lastError.has_value());
        if (status.lastError) {
            PHPVPN_CHECK_EQ(status.lastError->code, std::string(item.code));
            PHPVPN_CHECK_EQ(status.lastError->recoverable, item.recoverable);
            const auto diagnostics = fixture.engine.diagnostics();
            PHPVPN_CHECK_EQ(diagnostics.lastError->description, status.lastError->description);
            for (const auto& output : {
                    IpcProtocol::buildStatusResponse(kMessageId, status),
                    IpcProtocol::buildDiagnosticsResponse(kMessageId, fixture.engine.diagnostics())}) {
                for (const auto* forbidden : {"private endpoint details", "runtime.invalid", "secret-path",
                                              "runtime-fixture-secret", "010203040506", "212223242526"})
                    PHPVPN_CHECK(output.find(forbidden) == std::string::npos);
            }
        }
    }
}

PHPVPN_TEST(runtime, recovery_retains_failed_bootstrap_and_idempotent_connect) {
    EngineFixture fixture;
    Gate recoveryHealth;
    fixture.transport.hook = [&](HttpAction action, std::string_view, int call) -> std::optional<HttpExchangeResult> {
        if (action == HttpAction::data && call == 1) return HttpExchangeResult{Error(ErrorCode::transport_timeout), 17};
        if (action == HttpAction::health && call == 2) recoveryHealth.block();
        return {};
    };
    fixture.transport.cancel = [&] { recoveryHealth.release(); };
    PHPVPN_REQUIRE_OK(fixture.engine.start());
    PHPVPN_REQUIRE_OK(fixture.engine.connect());
    PHPVPN_CHECK(eventually([&] { return fixture.engine.status().state == ConnectionState::recovering; }));
    const auto failed = fixture.engine.status();
    PHPVPN_CHECK_EQ(failed.counters.dataPollsTotal, std::uint64_t(1));
    PHPVPN_REQUIRE_OK(fixture.engine.connect());
    PHPVPN_CHECK_EQ(fixture.engine.status().counters.dataPollsTotal, std::uint64_t(1));
    fixture.clock.now.store(10000);
    PHPVPN_CHECK(recoveryHealth.await());
    PHPVPN_CHECK(allChecks(fixture.engine.status(), CheckpointResult::success, CheckpointResult::success, CheckpointResult::failed));
    PHPVPN_REQUIRE_OK(fixture.engine.connect());
    recoveryHealth.release();
    PHPVPN_CHECK(eventually([&] { return fixture.engine.status().state == ConnectionState::connected; }));
    PHPVPN_CHECK(allChecks(fixture.engine.status(), CheckpointResult::success, CheckpointResult::success, CheckpointResult::failed));
    PHPVPN_CHECK(fixture.engine.status().counters.dataPollsTotal >= 2);
    PHPVPN_REQUIRE_OK(fixture.engine.disconnect());
    PHPVPN_CHECK(eventually([&] { return fixture.engine.status().state == ConnectionState::disconnected; }));
    PHPVPN_CHECK_EQ(fixture.engine.status().counters.dataPollsTotal, std::uint64_t(0));
    PHPVPN_CHECK(allChecks(fixture.engine.status(), CheckpointResult::pending, CheckpointResult::pending, CheckpointResult::pending));
}

PHPVPN_TEST(runtime, connected_failure_keeps_successes_and_disconnect_closes_accounting) {
    EngineFixture fixture;
    Gate dataGate;
    fixture.transport.hook = [&](HttpAction action, std::string_view, int call) -> std::optional<HttpExchangeResult> {
        if (action == HttpAction::data && call == 2) {
            dataGate.block();
            return HttpExchangeResult{Error(ErrorCode::cancelled), 73};
        }
        return {};
    };
    fixture.transport.cancel = [&] { dataGate.release(); };
    PHPVPN_REQUIRE_OK(fixture.engine.start());
    PHPVPN_REQUIRE_OK(fixture.engine.connect());
    PHPVPN_CHECK(dataGate.await());
    const auto before = fixture.engine.status();
    PHPVPN_CHECK_EQ(before.counters.dataPollsTotal, std::uint64_t(2));
    PHPVPN_CHECK_EQ(before.counters.serializedDataResponseBodyBytes, fixture.transport.dataResponseBytes.load());
    PHPVPN_REQUIRE_OK(fixture.engine.connect());
    std::atomic<bool> readsValid{true};
    std::vector<std::thread> readers;
    for (int index = 0; index < 3; ++index) readers.emplace_back([&] {
        for (int i = 0; i < 2000; ++i) {
            auto value = fixture.engine.status();
            if (value.counters.dataPollsTotal != 2 || value.state != ConnectionState::connected ||
                value.counters.dataPollsTotal != value.counters.dataPollsEmpty + value.counters.dataPollsWithPayload)
                readsValid.store(false);
        }
    });
    for (auto& reader : readers) reader.join();
    PHPVPN_CHECK(readsValid.load());
    PHPVPN_CHECK_EQ(fixture.transport.dataCalls.load(), 2);
    dataGate.release();
    PHPVPN_CHECK(eventually([&] { return fixture.engine.status().state == ConnectionState::recovering; }));
    const auto failed = fixture.engine.status();
    PHPVPN_CHECK(allChecks(failed, CheckpointResult::success, CheckpointResult::success, CheckpointResult::success));
    PHPVPN_CHECK_EQ(failed.lastError->code, std::string("communication_failed"));
    PHPVPN_CHECK_EQ(failed.counters.serializedDataResponseBodyBytes, before.counters.serializedDataResponseBodyBytes + 73);
    PHPVPN_REQUIRE_OK(fixture.engine.disconnect());
    PHPVPN_CHECK(eventually([&] { return fixture.engine.status().state == ConnectionState::disconnected; }));
    PHPVPN_CHECK_EQ(fixture.engine.status().counters.serializedDataResponseBodyBytes, std::uint64_t(0));
    PHPVPN_CHECK_EQ(fixture.engine.diagnostics().counters.dataPollsTotal, std::uint64_t(0));
}

PHPVPN_TEST(runtime, disconnect_cancels_inflight_bootstrap_and_new_connect_resets) {
    EngineFixture fixture;
    Gate partial;
    fixture.transport.hook = [&](HttpAction action, std::string_view, int call) -> std::optional<HttpExchangeResult> {
        if (action == HttpAction::data && call == 1) {
            partial.block();
            return HttpExchangeResult{Error(ErrorCode::cancelled), 777};
        }
        return {};
    };
    fixture.transport.cancel = [&] { partial.release(); };
    PHPVPN_REQUIRE_OK(fixture.engine.start());
    PHPVPN_REQUIRE_OK(fixture.engine.connect());
    PHPVPN_CHECK(partial.await());
    PHPVPN_REQUIRE_OK(fixture.engine.disconnect());
    PHPVPN_CHECK(eventually([&] { return fixture.engine.status().state == ConnectionState::disconnected; }));
    for (int i = 0; i < 100; ++i) PHPVPN_CHECK_EQ(fixture.engine.status().counters.serializedDataResponseBodyBytes, std::uint64_t(0));
    fixture.transport.hook = [](HttpAction action, std::string_view, int) -> std::optional<HttpExchangeResult> {
        if (action == HttpAction::data) return HttpExchangeResult{HttpResponse{401, "bad"}, 3};
        return {};
    };
    PHPVPN_REQUIRE_OK(fixture.engine.connect());
    PHPVPN_CHECK(eventually([&] { return fixture.engine.status().state == ConnectionState::error; }));
    PHPVPN_CHECK_EQ(fixture.engine.status().counters.serializedDataResponseBodyBytes, std::uint64_t(3));
    PHPVPN_REQUIRE_OK(fixture.engine.connect());
    PHPVPN_CHECK(eventually([&] { return fixture.transport.dataCalls.load() == 3 && fixture.engine.status().state == ConnectionState::error; }));
    PHPVPN_CHECK_EQ(fixture.engine.status().counters.dataPollsTotal, std::uint64_t(1));
    PHPVPN_CHECK_EQ(fixture.engine.status().counters.serializedDataResponseBodyBytes, std::uint64_t(3));
    fixture.engine.shutdown();
    PHPVPN_CHECK_EQ(fixture.engine.status().counters.dataPollsTotal, std::uint64_t(0));
    EngineFixture restarted;
    PHPVPN_CHECK_EQ(restarted.engine.status().counters.dataPollsTotal, std::uint64_t(0));
}
}  // namespace phpvpn
