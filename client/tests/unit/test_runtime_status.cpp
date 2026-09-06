#include "fake_config_files.h"
#include "core/config/config_defaults.h"
#include "core/wizard/probe_client.h"
#include "core/wizard/wizard_service.h"
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
    unsigned opens = 0;
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
        ++opens;
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
    std::function<void(HttpResponse&)> mutateResponse;
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
        if (action == HttpAction::health) {
            PHPVPN_CHECK_EQ(auth.find("type")->stringValue(), std::string("auth-none"));
            PHPVPN_CHECK_EQ(auth.objectValue().size(), std::size_t(2));
        }
        const auto id = auth.find("requestId")->stringValue();
        std::string body = action == HttpAction::health ? health(id) : action == HttpAction::info ? info(id) : data(id, request);
        const auto bytes = body.size();
        if (action == HttpAction::data) dataResponseBytes += bytes;
        HttpResponse response{200, std::move(body), httpActionContentType(action)};
        if (mutateResponse) mutateResponse(response);
        return {std::move(response), bytes};
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
        const std::string plaintext = badInfo ? "{}" : R"({"serverVersion":"1.0","generatedAt":"2026-09-04T10:00:00.000Z","clients":[],"limits":{"maxDataRecordsPerRequest":16,"maxRequestBodyBytes":4198400,"maxQueueBytesPerClient":16777216,"queueTtlSeconds":60,"minDataPollIntervalMs":80,"maxDataPollIntervalMs":1000}})";
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
    std::atomic<int> loads{0}, opens{0};
    bool loaded = false;
    ErrorCode failure = ErrorCode::none;
    Status loadAndVerify(const std::wstring&) override {
        ++loads;
        if (failure != ErrorCode::none) return Error(failure);
        loaded = true;
        return ok();
    }
    Result<TunnelAdapterInfo> createAdapter(const std::wstring&, const std::wstring&, const std::wstring&) override { return TunnelAdapterInfo{123, "fixture-adapter", true}; }
    Result<TunnelAdapterInfo> openAdapter(const std::wstring&) override {
        ++opens;
        if (!loaded) return Error(ErrorCode::tunnel_library_missing);
        return TunnelAdapterInfo{123, "fixture-adapter", false};
    }
    Result<std::shared_ptr<Tunnel>> startSession(std::uint32_t) override { ++starts; return std::shared_ptr<Tunnel>(new IdleTunnel); }
    void endSession() override {}
    void closeAdapter() override {}
    std::uint32_t runningDriverVersion() const override { return 1; }
};

class FakeNetConfig final : public NetConfig {
public:
    bool recoveryInterfacePresent = false;
    ErrorCode recoveryLookupFailure = ErrorCode::none;
    bool allowOwnedPhantomRemoval = false;
    int phantomRemovals = 0;
    Result<bool> interfaceExists(std::uint64_t) override {
        if (recoveryLookupFailure != ErrorCode::none) return Error(recoveryLookupFailure);
        return recoveryInterfacePresent;
    }
    Status removeOwnedPhantomInterface(const std::wstring&, const std::wstring&, std::uint64_t) override {
        if (!allowOwnedPhantomRemoval) return Error(ErrorCode::journal_ambiguous);
        ++phantomRemovals;
        recoveryInterfacePresent = false;
        return ok();
    }
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
    FakeConfigFiles* files = new FakeConfigFiles(*crypto.actual);
    ConfigStore store{*crypto.actual, std::unique_ptr<ConfigFiles>(files)};
    VpnEngine engine{crypto, transport, clock, driver, net,
                     EnginePaths{directory / "config.json", directory / "journal.json", directory.wstring()}, store};
    EngineFixture() {
        std::filesystem::create_directories(directory);
        files->bytes = kConfig;
    }
    ~EngineFixture() {
        engine.shutdown();
        std::filesystem::remove_all(directory);
    }
};

struct WizardServiceFixture {
    EngineFixture runtime;
    std::function<void()> diagnosticTask;
    std::deque<std::function<void()>> fileTasks;
    std::unique_ptr<WizardService> service;
    std::uint64_t sequence = 0;
    unsigned httpCalls = 0;
    WizardRequest request;
    WizardServiceFixture() {
        WizardScheduler scheduler;
        scheduler.file = [&](std::function<void()> task) {
            if (fileTasks.size() >= 4) return false;
            fileTasks.push_back(std::move(task)); return true;
        };
        scheduler.diagnostic = [&](std::function<void()> task) {
            if (diagnosticTask) return false;
            diagnosticTask = std::move(task); return true;
        };
        scheduler.transport = [&]() -> Result<std::unique_ptr<Transport>> {
            auto transport = std::make_unique<ScriptedTransport>(*runtime.crypto.actual);
            transport->hook = [&](HttpAction, std::string_view, int) -> std::optional<HttpExchangeResult> { ++httpCalls; return {}; };
            return std::unique_ptr<Transport>(std::move(transport));
        };
        request.requestId = runtime.crypto.actual->randomUuidV4().value();
        request.serviceInstanceId = runtime.crypto.actual->randomUuidV4().value();
        service = std::make_unique<WizardService>(runtime.crypto, runtime.clock, runtime.store, runtime.engine,
            std::move(scheduler), request.serviceInstanceId);
        request.command = WizardCommand::open;
        const auto opened = ask(request);
        const auto view = WizardIpc::parseView(opened.result).value();
        request.sessionId = view.sessionId;
        request.fields = *view.fields;
    }
    WizardReply ask(const WizardRequest& input) {
        const auto encoded = WizardIpc::buildRequest(input).value();
        const auto parsed = WizardIpc::parseRequest(encoded).value();
        const auto response = service->handle(parsed);
        auto reply = WizardIpc::parseReply(input.command, input.requestId, response);
        if (!reply) throw std::runtime_error("Wizard reply failed strict schema validation: " + response);
        return std::move(reply).value();
    }
    WizardReply call(WizardCommand command) { request.command = command; return ask(request); }
    void runDiagnostic() { auto task = std::move(diagnosticTask); diagnosticTask = {}; if (task) task(); }
    void runFile() { auto task = std::move(fileTasks.front()); fileTasks.pop_front(); task(); }
    void check(WizardCommand command) {
        request.command = command;
        request.attemptSequence = ++sequence;
        request.clientAttemptId = runtime.crypto.actual->randomUuidV4().value();
        const auto started = ask(request);
        if (started.error) throw std::runtime_error(started.error->code);
        request.operationId = started.result.find("operationId")->stringValue();
        runDiagnostic();
        const auto completed = call(WizardCommand::get_operation);
        if (completed.error || completed.result.find("operationState")->stringValue() != "succeeded")
            throw std::runtime_error("probe did not succeed");
    }
    void allChecks() { check(WizardCommand::check_url); check(WizardCommand::check_secret); check(WizardCommand::check_access); }
    WizardReply beginFinish() {
        request.commitId = runtime.crypto.actual->randomUuidV4().value();
        request.createDefaultContracts = false;
        return call(WizardCommand::finish);
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

PHPVPN_TEST(probe, url_requires_only_fresh_health_syntax_then_secret_verifies_cryptography) {
    ObservedCrypto crypto;
    ScriptedTransport transport(*crypto.actual);
    TestClock clock;
    const auto document = analyzeWizardDocument(kConfig).value();
    transport.badHealthTag = true;
    ProbeClient url(crypto, transport, clock);
    PHPVPN_REQUIRE_OK(url.checkUrl(document.context, document.fields));
    PHPVPN_CHECK_EQ(crypto.opens, 0u);
    PHPVPN_CHECK_EQ(transport.healthCalls.load(), 1);
    PHPVPN_CHECK_EQ(transport.infoCalls.load(), 0);
    ProbeClient secret(crypto, transport, clock);
    PHPVPN_CHECK(!secret.checkSecret(document.context, document.fields));
    PHPVPN_CHECK_EQ(transport.healthCalls.load(), 2);
    PHPVPN_CHECK_EQ(transport.infoCalls.load(), 0);
    auto fields = document.fields;
    fields.serverUrl = std::string(kWizardPlaceholderUrl);
    ProbeClient placeholder(crypto, transport, clock);
    PHPVPN_REQUIRE_ERROR(placeholder.checkUrl(document.context, fields), ErrorCode::config_invalid_url);
    PHPVPN_CHECK_EQ(transport.healthCalls.load(), 2);
}

PHPVPN_TEST(probe, access_discards_invalid_payload_without_opening_it_and_uses_one_empty_request) {
    ObservedCrypto crypto;
    ScriptedTransport transport(*crypto.actual);
    TestClock clock;
    const auto document = analyzeWizardDocument(kConfig).value();
    ProbeClient secret(crypto, transport, clock);
    PHPVPN_REQUIRE_OK(secret.checkSecret(document.context, document.fields));
    const auto opensForHealthInfo = crypto.opens;
    crypto.opens = 0;
    transport.incoming = {packet(100, true)};
    transport.badPayloadTag = true;
    transport.hook = [](HttpAction action, std::string_view body, int) -> std::optional<HttpExchangeResult> {
        if (action == HttpAction::data) {
            PHPVPN_CHECK(body.ends_with('\n'));
            PHPVPN_CHECK(body.find('\n') == body.size() - 1);
            PHPVPN_CHECK(Json::parse(body.substr(0, body.size() - 1))->find("type")->stringValue() == "auth-full");
        }
        return {};
    };
    ProbeClient access(crypto, transport, clock);
    const auto result = access.checkAccess(document, document.fields);
    PHPVPN_REQUIRE_OK(result);
    PHPVPN_CHECK_EQ(result.value(), 1u);
    PHPVPN_CHECK_EQ(crypto.opens, opensForHealthInfo);
    PHPVPN_CHECK_EQ(transport.healthCalls.load(), 2);
    PHPVPN_CHECK_EQ(transport.infoCalls.load(), 2);
    PHPVPN_CHECK_EQ(transport.dataCalls.load(), 1);
}

PHPVPN_TEST(probe, access_never_retries_401_and_rejects_bad_mac_or_local_self_contract) {
    for (const auto failing : {HttpAction::health, HttpAction::info, HttpAction::data}) {
        ObservedCrypto crypto;
        ScriptedTransport transport(*crypto.actual);
        TestClock clock;
        const auto document = analyzeWizardDocument(kConfig).value();
        transport.hook = [failing](HttpAction action, std::string_view, int) -> std::optional<HttpExchangeResult> {
            if (action == failing) return HttpExchangeResult{HttpResponse{401, "{}", httpActionContentType(action)}, 2};
            return {};
        };
        ProbeClient probe(crypto, transport, clock);
        PHPVPN_REQUIRE_ERROR(probe.checkAccess(document, document.fields), ErrorCode::server_unauthorized);
        PHPVPN_CHECK_EQ(transport.healthCalls.load(), 1);
        PHPVPN_CHECK_EQ(transport.infoCalls.load(), failing == HttpAction::health ? 0 : 1);
        PHPVPN_CHECK_EQ(transport.dataCalls.load(), failing == HttpAction::data ? 1 : 0);
    }
    ObservedCrypto crypto;
    ScriptedTransport transport(*crypto.actual);
    TestClock clock;
    const auto document = analyzeWizardDocument(kConfig).value();
    auto fields = document.fields;
    fields.virtualIp = "10.99.99.3";
    ProbeClient conflict(crypto, transport, clock);
    PHPVPN_REQUIRE_ERROR(conflict.checkAccess(document, fields), ErrorCode::wizard_contract_self_conflict);
    PHPVPN_CHECK_EQ(transport.healthCalls.load(), 0);
    transport.badMac = true;
    ProbeClient badMac(crypto, transport, clock);
    PHPVPN_REQUIRE_ERROR(badMac.checkAccess(document, document.fields), ErrorCode::response_mac_invalid);
}

PHPVPN_TEST(probe, validates_content_type_routes_limits_and_cancellation) {
    ObservedCrypto crypto;
    ScriptedTransport transport(*crypto.actual);
    TestClock clock;
    auto document = analyzeWizardDocument(kConfig).value();
    transport.mutateResponse = [](HttpResponse& response) { response.contentType = "text/html"; };
    ProbeClient wrongType(crypto, transport, clock);
    PHPVPN_REQUIRE_ERROR(wrongType.checkUrl(document.context, document.fields), ErrorCode::response_invalid);
    transport.mutateResponse = [](HttpResponse& response) {
        const auto destination = response.body.find("\"dst\":\"10.99.99.2\"");
        if (destination != std::string::npos) response.body.replace(destination, std::string_view("\"dst\":\"10.99.99.2\"").size(), "\"dst\":\"10.99.99.4\"");
    };
    transport.incoming = {packet(100, true)};
    ProbeClient routes(crypto, transport, clock);
    PHPVPN_REQUIRE_ERROR(routes.checkAccess(document, document.fields), ErrorCode::response_invalid);
    transport.mutateResponse = {};
    document.context.runtime.maxResponseBodyBytes = 1;
    ProbeClient limit(crypto, transport, clock);
    PHPVPN_REQUIRE_ERROR(limit.checkUrl(document.context, document.fields), ErrorCode::transport_response_too_large);
    const auto calls = transport.healthCalls.load();
    ProbeClient cancelled(crypto, transport, clock);
    cancelled.cancel();
    PHPVPN_REQUIRE_ERROR(cancelled.checkUrl(document.context, document.fields), ErrorCode::cancelled);
    PHPVPN_CHECK_EQ(transport.healthCalls.load(), calls);
}

PHPVPN_TEST(runtime, actions_read_current_file_and_disconnect_never_reads_it) {
    EngineFixture fixture;
    PHPVPN_REQUIRE_OK(fixture.engine.start());
    PHPVPN_CHECK_EQ(fixture.files->reads, 0u);
    PHPVPN_REQUIRE_OK(fixture.engine.storedConfigJson());
    fixture.files->bytes = "{ broken external edit";
    PHPVPN_REQUIRE_ERROR(fixture.engine.connect(), ErrorCode::config_invalid_json);
    PHPVPN_CHECK_EQ(fixture.transport.healthCalls.load(), 0);
    const auto before = fixture.files->reads;
    PHPVPN_REQUIRE_OK(fixture.engine.disconnect());
    PHPVPN_CHECK_EQ(fixture.files->reads, before);
    fixture.files->bytes.reset();
    PHPVPN_REQUIRE_ERROR(fixture.engine.connect(), ErrorCode::config_not_found);
    PHPVPN_CHECK_EQ(fixture.files->writes, 0u);
}

PHPVPN_TEST(wizard_service, leases_expire_without_disk_http_or_global_disconnect) {
    WizardServiceFixture fixture;
    const auto reads = fixture.runtime.files->reads;
    for (int i = 1; i < 8; ++i) PHPVPN_CHECK(!fixture.call(WizardCommand::open).error);
    auto full = fixture.call(WizardCommand::open);
    PHPVPN_CHECK(full.error && full.error->code == "busy");
    fixture.runtime.clock.now = 59999;
    PHPVPN_CHECK(!fixture.call(WizardCommand::keepalive).error);
    fixture.runtime.clock.now = 60000;
    fixture.service->tick();
    PHPVPN_CHECK(!fixture.call(WizardCommand::keepalive).error);
    fixture.runtime.clock.now = 120000;
    fixture.service->tick();
    const auto expired = fixture.call(WizardCommand::keepalive);
    PHPVPN_CHECK(expired.error && expired.error->code == "wizard_session_expired");
    PHPVPN_CHECK_EQ(fixture.httpCalls, 0u);
    PHPVPN_CHECK_EQ(fixture.runtime.files->writes, 0u);
    PHPVPN_CHECK(fixture.runtime.files->reads >= reads);
    PHPVPN_CHECK(fixture.runtime.engine.status().state == ConnectionState::disconnected);
}
PHPVPN_TEST(wizard_service, cancelled_attempt_prevents_delayed_start_and_busy_sequence_never_replays) {
    WizardServiceFixture fixture;
    fixture.request.attemptSequence = 4;
    fixture.request.clientAttemptId = fixture.runtime.crypto.actual->randomUuidV4().value();
    PHPVPN_CHECK(!fixture.call(WizardCommand::cancel_attempt).error);
    auto delayed = fixture.call(WizardCommand::check_url);
    PHPVPN_CHECK(delayed.error && delayed.error->code == "wizard_attempt_closed");
    fixture.request.attemptSequence = 5;
    auto first = fixture.call(WizardCommand::check_url);
    PHPVPN_CHECK(!first.error);
    fixture.request.operationId = first.result.find("operationId")->stringValue();
    fixture.request.attemptSequence = 6;
    auto busy = fixture.call(WizardCommand::check_url);
    PHPVPN_CHECK(busy.error && busy.error->code == "busy");
    fixture.runDiagnostic();
    auto replay = fixture.call(WizardCommand::check_url);
    PHPVPN_CHECK(replay.error && replay.error->code == "wizard_attempt_closed");
    PHPVPN_CHECK_EQ(fixture.httpCalls, 1u);
    PHPVPN_CHECK(!fixture.call(WizardCommand::reset).error);
    auto afterReset = fixture.call(WizardCommand::check_url);
    PHPVPN_CHECK(afterReset.error && afterReset.error->code == "wizard_attempt_closed");
}
PHPVPN_TEST(wizard_service, finish_requires_exact_evidence_is_idempotent_and_last_complete_save_wins) {
    WizardServiceFixture fixture;
    auto premature = fixture.beginFinish();
    PHPVPN_CHECK(premature.error && premature.error->code == "wizard_prerequisite_failed");
    fixture.allChecks();
    auto original = fixture.request.fields;
    fixture.request.fields.serverSecret += "changed";
    auto changed = fixture.beginFinish();
    PHPVPN_CHECK(changed.error && changed.error->code == "wizard_prerequisite_failed");
    fixture.request.fields = original;
    fixture.runtime.files->bytes = "external invalid replacement";
    auto started = fixture.beginFinish();
    PHPVPN_CHECK(!started.error);
    fixture.request.operationId = started.result.find("operationId")->stringValue();
    const auto commitId = fixture.request.commitId;
    fixture.runFile();
    auto committed = fixture.call(WizardCommand::get_commit);
    PHPVPN_CHECK_EQ(committed.result.find("commitState")->stringValue(), "confirmed");
    PHPVPN_CHECK_EQ(fixture.runtime.files->writes, 1u);
    auto stored = analyzeWizardDocument(*fixture.runtime.files->bytes);
    PHPVPN_REQUIRE_OK(stored);
    PHPVPN_CHECK_EQ(stored->fields, original);
    PHPVPN_CHECK(!fixture.call(WizardCommand::finish).error);
    PHPVPN_CHECK(fixture.fileTasks.empty());
    PHPVPN_CHECK_EQ(fixture.runtime.files->writes, 1u);
    fixture.request.fields.accessKey[0] = 'f';
    auto mismatch = fixture.call(WizardCommand::finish);
    PHPVPN_CHECK(mismatch.error && mismatch.error->code == "invalid_argument");
    fixture.request.fields = original;
    PHPVPN_CHECK(!fixture.call(WizardCommand::close).error);
    auto receiptAfterClose = fixture.call(WizardCommand::get_commit);
    PHPVPN_CHECK_EQ(receiptAfterClose.result.find("receipt")->find("commitId")->stringValue(), commitId);
    auto noResurrection = fixture.call(WizardCommand::finish);
    PHPVPN_CHECK(noResurrection.error && noResurrection.error->code == "wizard_session_expired");
}
PHPVPN_TEST(wizard_service, cancelling_later_checkpoint_preserves_exact_earlier_evidence_for_manual_retry) {
    WizardServiceFixture fixture;
    fixture.check(WizardCommand::check_url);
    fixture.check(WizardCommand::check_secret);
    fixture.request.attemptSequence = ++fixture.sequence;
    fixture.request.clientAttemptId = fixture.runtime.crypto.actual->randomUuidV4().value();
    auto started = fixture.call(WizardCommand::check_access);
    PHPVPN_CHECK(!started.error);
    PHPVPN_CHECK(!fixture.call(WizardCommand::cancel_attempt).error);
    fixture.runDiagnostic();
    const auto calls = fixture.httpCalls;
    fixture.check(WizardCommand::check_access);
    PHPVPN_CHECK_EQ(fixture.httpCalls, calls + 3);
    PHPVPN_CHECK(!fixture.beginFinish().error);
    fixture.runFile();
    PHPVPN_CHECK_EQ(fixture.runtime.files->writes, 1u);
}
PHPVPN_TEST(wizard_service, idle_keepalive_detects_foreign_epoch_and_resume_accepts_acknowledged_epoch) {
    WizardServiceFixture fixture;
    PHPVPN_REQUIRE_OK(fixture.runtime.engine.start());
    fixture.check(WizardCommand::check_url);
    PHPVPN_REQUIRE_OK(fixture.runtime.engine.connect());
    PHPVPN_REQUIRE_OK(fixture.runtime.engine.disconnect());
    PHPVPN_CHECK(eventually([&] { return fixture.runtime.engine.status().state == ConnectionState::disconnected; }));
    const auto interrupted = fixture.call(WizardCommand::keepalive);
    PHPVPN_CHECK(interrupted.error && interrupted.error->code == "wizard_connection_interrupted");
    auto latest = fixture.call(WizardCommand::get_operation);
    PHPVPN_CHECK_EQ(latest.result.find("operationState")->stringValue(), std::string("interrupted"));
    PHPVPN_CHECK_EQ(jsonUnsignedOrZero(latest.result.find("connectionEpoch")), fixture.runtime.engine.connectionIdentity().epoch);
    fixture.request.connectionEpoch = 900;
    PHPVPN_CHECK(fixture.call(WizardCommand::resume).error.has_value());
    fixture.request.connectionEpoch = 0;
    auto resumed = fixture.call(WizardCommand::resume);
    PHPVPN_CHECK(!resumed.error);
    PHPVPN_CHECK_EQ(jsonUnsignedOrZero(resumed.result.find("connectionEpoch")), fixture.runtime.engine.connectionIdentity().epoch);
    auto noOldProof = fixture.beginFinish();
    PHPVPN_CHECK(noOldProof.error && noOldProof.error->code == "wizard_prerequisite_failed");
}
PHPVPN_TEST(wizard_service, expiration_during_commit_keeps_confirmed_receipt_and_unknown_never_retries) {
    WizardServiceFixture fixture;
    fixture.allChecks();
    fixture.runtime.files->beforePublish = [&] { fixture.runtime.clock.now += 60001; fixture.service->tick(); };
    PHPVPN_CHECK(!fixture.beginFinish().error);
    fixture.runFile();
    auto receipt = fixture.call(WizardCommand::get_commit);
    PHPVPN_CHECK_EQ(receipt.result.find("commitState")->stringValue(), "confirmed");
    auto expired = fixture.call(WizardCommand::keepalive);
    PHPVPN_CHECK(expired.error && expired.error->code == "wizard_session_expired");
    WizardServiceFixture unknown;
    unknown.allChecks();
    unknown.runtime.files->outcome = ConfigPublishState::unknown;
    PHPVPN_CHECK(!unknown.beginFinish().error);
    unknown.runFile();
    auto unresolved = unknown.call(WizardCommand::get_commit);
    PHPVPN_CHECK_EQ(unresolved.result.find("commitState")->stringValue(), "unknown");
    PHPVPN_CHECK(!unknown.call(WizardCommand::finish).error);
    PHPVPN_CHECK(unknown.fileTasks.empty());
    PHPVPN_CHECK_EQ(unknown.runtime.files->writes, 1u);
}
PHPVPN_TEST(wizard_service, receipt_capacity_rejects_before_write_and_retains_closed_session_results) {
    WizardServiceFixture fixture;
    fixture.allChecks();
    for (std::size_t i = 0; i < kWizardMaxReceipts; ++i) {
        PHPVPN_CHECK(!fixture.beginFinish().error);
        fixture.runFile();
    }
    const auto full = fixture.beginFinish();
    PHPVPN_CHECK(full.error && full.error->code == "busy");
    PHPVPN_CHECK_EQ(fixture.runtime.files->writes, static_cast<unsigned>(kWizardMaxReceipts));
}
PHPVPN_TEST(wizard_service, connect_before_queued_finish_invalidates_commit_even_after_disconnect) {
    WizardServiceFixture fixture;
    PHPVPN_REQUIRE_OK(fixture.runtime.engine.start());
    fixture.allChecks();
    PHPVPN_CHECK(!fixture.beginFinish().error);
    PHPVPN_REQUIRE_OK(fixture.runtime.engine.connect());
    PHPVPN_REQUIRE_OK(fixture.runtime.engine.disconnect());
    PHPVPN_CHECK(eventually([&] { return fixture.runtime.engine.status().state == ConnectionState::disconnected; }));
    fixture.runFile();
    auto failed = fixture.call(WizardCommand::get_commit);
    PHPVPN_CHECK_EQ(failed.result.find("commitState")->stringValue(), "failed");
    PHPVPN_CHECK_EQ(failed.result.find("error")->find("code")->stringValue(), "wizard_connection_interrupted");
    PHPVPN_CHECK_EQ(fixture.runtime.files->writes, 0u);
    fixture.request.connectionEpoch = fixture.runtime.engine.connectionIdentity().epoch;
    PHPVPN_CHECK(!fixture.call(WizardCommand::resume).error);
    auto noOldProof = fixture.beginFinish();
    PHPVPN_CHECK(noOldProof.error && noOldProof.error->code == "wizard_prerequisite_failed");
}

PHPVPN_TEST(runtime, recovery_retains_accepted_config_after_save_and_external_delete) {
    EngineFixture fixture;
    Gate data;
    fixture.transport.hook = [&](HttpAction action, std::string_view, int call) -> std::optional<HttpExchangeResult> {
        if (action == HttpAction::health && call == 1)
            return HttpExchangeResult{Error(ErrorCode::transport_timeout), 0};
        if (action == HttpAction::data && call == 2) data.block();
        return {};
    };
    fixture.transport.cancel = [&] { data.release(); };
    PHPVPN_REQUIRE_OK(fixture.engine.start());
    PHPVPN_REQUIRE_OK(fixture.engine.connect());
    PHPVPN_CHECK(eventually([&] { return fixture.engine.status().state == ConnectionState::recovering; }));
    PHPVPN_REQUIRE_OK(fixture.engine.setConfigJson(makeDefaultEditorDocument()));
    fixture.files->bytes.reset();
    fixture.clock.now += 10000;
    PHPVPN_CHECK(data.await());
    // Scripted relay still authenticates with the original secret and key.
    PHPVPN_CHECK(fixture.engine.status().state == ConnectionState::connected);
    const auto reads = fixture.files->reads;
    PHPVPN_REQUIRE_OK(fixture.engine.connect());
    PHPVPN_CHECK_EQ(fixture.files->reads, reads);
    PHPVPN_REQUIRE_OK(fixture.engine.disconnect());
    PHPVPN_CHECK(eventually([&] { return fixture.engine.status().state == ConnectionState::disconnected; }));
    PHPVPN_REQUIRE_ERROR(fixture.engine.connect(), ErrorCode::config_not_found);
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
PHPVPN_TEST(runtime, ownership_recovery_never_opens_adapter_and_blocks_unresolved_identity) {
    EngineFixture fixture;
    TunnelSessionSettings settings;
    settings.recoveryWaitMs = 0;
    settings.journalPath = fixture.directory / "journal.json";
    settings.installDirectory = fixture.directory.wstring();
    OwnershipJournal journal;
    journal.adapterName = "PHPVPN";
    journal.adapterGuid = "{7d1b4d2f-1f34-4d0e-9a3e-2f6c9b5a41d8}";
    journal.interfaceLuid = 123;
    journal.adapterCreated = true;
    journal.address = *Ipv4Address::parse("10.99.99.2");
    journal.prefixLength = 24;
    journal.routeDestination = *Ipv4Cidr::parse("10.99.99.0/24");
    { std::ofstream file(settings.journalPath, std::ios::binary); file << journal.serialize(); }
    TunnelSession session(fixture.driver, fixture.net);
    fixture.driver.failure = ErrorCode::tunnel_library_missing;
    fixture.net.recoveryInterfacePresent = true;
    const auto failed = session.recoverPreviousRun(settings);
    PHPVPN_CHECK(!failed && failed.error().code == ErrorCode::journal_ambiguous);
    PHPVPN_CHECK(std::filesystem::exists(settings.journalPath));
    PHPVPN_CHECK_EQ(fixture.driver.loads.load(), 0);
    PHPVPN_CHECK_EQ(fixture.driver.opens.load(), 0);
    fixture.net.recoveryLookupFailure = ErrorCode::journal_ambiguous;
    const auto blockedStart = fixture.engine.start();
    PHPVPN_CHECK(!blockedStart && blockedStart.error().code == ErrorCode::journal_ambiguous);
    const auto blockedConnect = fixture.engine.connect();
    PHPVPN_CHECK(!blockedConnect && blockedConnect.error().code == ErrorCode::service_not_running);
    PHPVPN_CHECK(std::filesystem::exists(settings.journalPath));
    fixture.net.recoveryInterfacePresent = false;
    fixture.net.recoveryLookupFailure = ErrorCode::none;
    settings.adapterName = L"different-installation";
    const auto wrongIdentity = session.recoverPreviousRun(settings);
    PHPVPN_CHECK(!wrongIdentity && wrongIdentity.error().code == ErrorCode::journal_ambiguous);
    PHPVPN_CHECK(std::filesystem::exists(settings.journalPath));
    settings.adapterName = L"PHPVPN";
    fixture.net.recoveryInterfacePresent = true;
    fixture.net.allowOwnedPhantomRemoval = true;
    PHPVPN_REQUIRE_OK(session.recoverPreviousRun(settings));
    PHPVPN_CHECK_EQ(fixture.net.phantomRemovals, 1);
    PHPVPN_CHECK_EQ(fixture.driver.loads.load(), 0);
    PHPVPN_CHECK_EQ(fixture.driver.opens.load(), 0);
    PHPVPN_CHECK(!std::filesystem::exists(settings.journalPath));
}
}  // namespace phpvpn
