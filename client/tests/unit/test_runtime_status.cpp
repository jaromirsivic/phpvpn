#include "fake_config_files.h"
#include "startup_connect.h"
#include "core/config/config_defaults.h"
#include "core/wizard/probe_client.h"
#include "core/wizard/wizard_service.h"
#include "../../apps/tray/wizard_window.h"
#include "../../apps/tray/progress_window.h"
#include "../../apps/tray/window_utils.h"
#include "platform/windows/ipc_pipe.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <mutex>
#include <thread>

#include "core/crypto/canonical.h"
#include "core/ipc/ipc_protocol.h"
#include "core/ipc/ipc_dispatcher.h"
#include "core/protocol/base64url.h"
#include "core/protocol/data_record.h"
#include "core/protocol/iso8601.h"
#include "core/protocol/server_version.h"
#include "core/protocol/json.h"
#include "core/runtime/vpn_engine.h"
#include "harness.h"
#include "queue_test_support.h"
#include "recording_tunnel.h"

namespace phpvpn {
struct VpnEngineTestAccess {
    static Status limits(NetworkWorker& worker, std::int64_t low, std::int64_t high) {
        ServerLimits limits;
        limits.minDataPollIntervalMs = low; limits.maxDataPollIntervalMs = high;
        return worker.publishLimits(limits);
    }
    static Status requestLimits(NetworkWorker& worker, std::int64_t bytes) {
        auto limits=worker.serverLimits_;
        limits.maxRequestBodyBytes=bytes;
        limits.maxDataRecordsPerRequest=16;
        limits.minDataPollIntervalMs=80;limits.maxDataPollIntervalMs=1000;
        return worker.publishLimits(limits);
    }
    static std::int64_t pollInterval(VpnEngine& engine) { return engine.network_.currentPollIntervalMs_; }
    static void setPollInterval(VpnEngine& engine, std::int64_t ms) { engine.network_.currentPollIntervalMs_ = ms; }
    static void outboundPacketQueued(VpnEngine& engine) { engine.onOutboundPacketQueued(); }
    static bool waitingForPoll(VpnEngine& engine) {
        std::lock_guard<std::mutex> lock(engine.commandMutex_);
        return engine.pollDeadlineMs_.has_value();
    }
    static void enqueue(VpnEngine& engine, PacketBytes bytes) {
        DropReason reason{};
        PHPVPN_CHECK(engine.outbound_->tryEnqueue(*Ipv4Address::parse("10.99.99.3"), std::move(bytes), reason).valueOr(false));
    }
    static void startBehindBarrier(VpnEngine& engine, std::function<void()> barrier) {
        engine.flowHasher_ = std::move(HmacFlowHasher::create(engine.crypto_)).value();
        engine.running_.store(true);
        engine.thread_ = std::thread([&engine, barrier] { barrier(); engine.runNetworkThread(); });
    }
    static std::uint64_t queueAge(VpnEngine& engine) {
        std::lock_guard lock(engine.stateMutex_);
        return engine.outbound_ ? engine.outbound_->maxQueuedPacketAgeMs() : 0;
    }
    static const void* flowHasherIdentity(VpnEngine& engine) { return engine.flowHasher_.get(); }
    static std::size_t queuedCommands(VpnEngine& engine) {
        std::lock_guard<std::mutex> lock(engine.commandMutex_);
        return engine.commands_.size();
    }
    static std::thread::id networkThread(VpnEngine& engine) {
        return engine.thread_.get_id();
    }
    // Call only before Connect, behind a transport gate, after published
    // Disconnect, or after shutdown. The network writer owns these pointers.
    static unsigned tunnelRoles(VpnEngine& engine) {
        return unsigned(bool(engine.inputWorker_)) + unsigned(bool(engine.outputWorker_));
    }
    // Soft recovery budget. 0 disables soft recovery, call before start().
    static void softRecoveryGrace(VpnEngine& engine, std::int64_t ms) { engine.softRecoveryGraceMs_ = ms; }
};
namespace {
constexpr const char* kConfig = R"({"masterSetup":{"httpHeaders":"X-Test: fixture","serverUrl":"https://runtime.invalid/secret-path","serverSecret":"runtime-fixture-secret","vpnCidr":"10.99.99.0/24","virtualIp":"10.99.99.2","accessKey":"0102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f20"},"communicationContracts":{"10.99.99.3":{"payloadKey":"2122232425262728292a2b2c2d2e2f303132333435363738393a3b3c3d3e3f40"}}})";
constexpr const char* kMessageId = "1f4d8c1a-3b2e-4a51-8c7d-9e0f1a2b3c4d";
constexpr const char* kTimestamp = "2026-09-04T10:00:00.000Z";

// Server versions relative to this build, so a MAJOR release keeps the fixtures valid.
std::string compatibleServerVersion() { return PHPVPN_CLIENT_VERSION; }
std::string incompatibleServerVersion() { return std::to_string(*semverMajor(PHPVPN_CLIENT_VERSION) + 1) + ".0.0"; }

ClientConfigPtr fixtureConfig() {
    ConfigWarnings warnings;
    return parseAndValidateConfig(kConfig, warnings).value();
}

class TestClock final : public Clock {
public:
    bool realWall = false;
    std::atomic<std::int64_t> now{0};
    std::atomic<unsigned> reads{0};
    std::int64_t monotonicMs() override { ++reads; return now.load(); }
    std::int64_t systemUtcMs() override {
        if (realWall) return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        return *Iso8601::parse(kTimestamp) + now.load();
    }
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
    bool failRandom = false, failSeal = false;
    unsigned randomCalls=0, failRandomAt=0;
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
        if(failSeal) return Error(ErrorCode::crypto_failed);
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
        ++randomCalls;
        return failRandom || (failRandomAt && randomCalls==failRandomAt) ? Status(Error(ErrorCode::crypto_failed)) : actual->randomBytes(out);
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
    explicit ScriptedTransport(CryptoProvider& crypto, ClientConfigPtr config = fixtureConfig()) : crypto_(crypto), config_(std::move(config)) {}
    std::function<std::optional<HttpExchangeResult>(HttpAction, std::string_view, int)> hook;
    std::function<void()> cancel;
    std::function<void(HttpResponse&)> mutateResponse;
    std::atomic<int> healthCalls{0}, infoCalls{0}, dataCalls{0}, extendedCalls{0};
    std::atomic<std::uint64_t> dataRequestBytes{0}, dataResponseBytes{0};
    bool badHealthTag = false, badInfo = false, badMac = false, badPacket = false, badPayloadTag = false;
    // A server of another MAJOR version in the authenticated health or info.
    std::atomic<bool> incompatibleHealth{false}, incompatibleInfo{false};
    bool defer = false;
    std::function<bool(std::string_view)> acceptTarget;
    bool swappedPayloadAad = false;
    std::vector<PacketBytes> incoming;
    bool captureDiagnostics = false;
    struct HeaderObservation { HttpAction action; std::string headers; std::string request; };
    std::mutex headerMutex;
    std::string configuredHeaders;
    std::vector<HeaderObservation> headerObservations;
    std::vector<HeaderObservation> observedHeaders() {
        std::lock_guard lock(headerMutex);
        return headerObservations;
    }
    bool suppressDiagnostic = false;
    std::atomic<int> diagnosticCalls{0};
    HttpExchangeResult postDiagnostic(HttpAction action, std::string_view request) override {
        ++diagnosticCalls;
        const bool previous = captureDiagnostics;
        captureDiagnostics = true;
        auto result = post(action, request);
        captureDiagnostics = previous;
        return result;
    }
    Status configure(const TransportSettings& settings) override {
        captureDiagnostics = settings.captureWizardHttpDiagnostics;
        std::lock_guard lock(headerMutex);
        configuredHeaders = settings.httpHeaders;
        return ok();
    }
    HttpExchangeResult capture(HttpAction action, std::string_view request, HttpExchangeResult result) {
        if (captureDiagnostics && !suppressDiagnostic && action != HttpAction::data && result.ok()) {
            HttpDiagnostic d; d.action = action; d.responseComplete = true;
            BoundedHttpCapture req, resp;
            req.append(std::string("POST http://fixture/?action=") + httpActionName(action) + " HTTP/?\r\n\r\n"); req.append(request);
            resp.append("HTTP/? " + std::to_string(result.value().statusCode) + "\r\nContent-Type: " + result.value().contentType + "\r\n\r\n"); resp.append(result.value().body);
            d.request = req.finish(); d.response = resp.finish(); result.diagnostic = std::move(d);
        }
        return result;
    }
    void cancelCurrentRequest() override { if (cancel) cancel(); }
    HttpExchangeResult post(HttpAction action, std::string_view request) override {
        {
            std::lock_guard lock(headerMutex);
            headerObservations.push_back({action, configuredHeaders, std::string(request)});
        }
        const int call = action == HttpAction::health ? ++healthCalls : action == HttpAction::info ? ++infoCalls : ++dataCalls;
        if (action == HttpAction::data) dataRequestBytes += request.size();
        if (hook) {
            auto result = hook(action, request, call);
            if (result) {
                if (action == HttpAction::data) dataResponseBytes += result->responseBodyBytesRead;
                return capture(action, request, std::move(*result));
            }
        }
        const auto auth = Json::parse(request.substr(0, request.find('\n'))).value();
        if (action == HttpAction::health) {
            PHPVPN_CHECK_EQ(auth.find("type")->stringValue(), std::string("auth-none"));
            PHPVPN_CHECK_EQ(auth.objectValue().size(), std::size_t(2));
        }
        const auto id = auth.find("requestId")->stringValue();
        std::string body = action == HttpAction::health ? health(id) : action == HttpAction::info ? info(id, auth.find("includeMaskedCommunicationContracts") != nullptr) : data(id, request);
        const auto bytes = body.size();
        if (action == HttpAction::data) dataResponseBytes += bytes;
        HttpResponse response{200, std::move(body), httpActionContentType(action)};
        if (mutateResponse) mutateResponse(response);
        return capture(action, request, {std::move(response), bytes});
    }
private:
    std::string health(const std::string& id) {
        KeyDerivation keys(crypto_);
        auto key = keys.healthKey(config_->serverSecret, id).value();
        std::vector<std::uint8_t> iv(12, 3), ciphertext;
        const auto ivText = Base64Url::encode(iv);
        GcmTag tag{};
        const auto version = incompatibleHealth.load() ? incompatibleServerVersion() : compatibleServerVersion();
        const auto aad = healthAad(id, version, "running", "ok", kTimestamp, ivText);
        (void)crypto_.aesGcmSeal(key.span(), iv, asBytes(aad), {}, ciphertext, tag);
        if (badHealthTag) tag[0] ^= 1;
        return "{\"serverVersion\":\"" + version + "\",\"status\":\"running\",\"description\":\"ok\",\"timestamp\":\"" +
               std::string(kTimestamp) + "\",\"requestId\":\"" + id + "\",\"iv\":\"" + ivText + "\",\"tag\":\"" + Base64Url::encode(tag) + "\"}";
    }
    std::string info(const std::string& id, bool extended) {
        std::string plaintext = badInfo ? "{}" : R"({"serverVersion":")" +
            (incompatibleInfo.load() ? incompatibleServerVersion() : compatibleServerVersion()) + R"(","generatedAt":"2026-09-04T10:00:00.000Z","clients":[],"limits":{"maxDataRecordsPerRequest":16,"maxRequestBodyBytes":4198400,"maxQueueBytesPerClient":16777216,"queueTtlSeconds":60,"minDataPollIntervalMs":80,"maxDataPollIntervalMs":1000}})";
        if (extended) {
            ++extendedCalls;
            const auto at = plaintext.find("\"clients\":[]");
            if (at != std::string::npos) plaintext.replace(at, 12,
                "\"clients\":[{\"address\":\"" + config_->virtualIp.toString() + "\",\"lastSeen\":\"2026-09-04T10:00:00.000Z\",\"maskedCommunicationContracts\":[]}]");
        }
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
            (defer || (acceptTarget && !acceptTarget(line.find("dst")->stringValue())) ? deferred : accepted).push_back(*line.find("messageId"));
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
            const auto aad = swappedPayloadAad
                ? dataAad(kMessageId, "10.99.99.2", "10.99.99.3", ivText)
                : dataAad(kMessageId, "10.99.99.3", "10.99.99.2", ivText);
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

struct TunnelActivity {
    std::atomic<int> readers{0}, writers{0}, stopped{0}, destroyed{0};
    std::atomic<bool> distinctThreads{true};
};

class IdleTunnel final : public Tunnel {
public:
    explicit IdleTunnel(TunnelActivity& activity)
        : activity_(activity), networkThread_(std::this_thread::get_id()) {}
    ~IdleTunnel() override { ++activity_.destroyed; }
    Result<TunnelReadResult> receive(std::span<std::uint8_t>, std::int64_t timeoutMs) override {
        std::unique_lock<std::mutex> lock(mutex_);
        if (reader_ == std::thread::id{}) {
            reader_ = std::this_thread::get_id();
            ++activity_.readers;
        }
        if (reader_ != std::this_thread::get_id() || reader_ == networkThread_ || reader_ == writer_)
            activity_.distinctThreads.store(false);
        signal_.wait_for(lock, std::chrono::milliseconds(timeoutMs), [&] { return stopped_; });
        return TunnelReadResult{stopped_ ? TunnelReadStatus::stopped : TunnelReadStatus::empty, 0};
    }
    Status send(std::span<const std::uint8_t>) override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (writer_ == std::thread::id{}) {
            writer_ = std::this_thread::get_id();
            ++activity_.writers;
        }
        if (writer_ != std::this_thread::get_id() || writer_ == networkThread_ || writer_ == reader_)
            activity_.distinctThreads.store(false);
        return ok();
    }
    void signalStop() override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!stopped_) ++activity_.stopped;
        stopped_ = true;
        signal_.notify_all();
    }
    std::uint64_t oversizedReceiveCount() const override { return 0; }
private:
    TunnelActivity& activity_;
    const std::thread::id networkThread_;
    std::thread::id reader_, writer_;
    std::mutex mutex_;
    std::condition_variable signal_;
    bool stopped_ = false;
};

class FakeDriver final : public TunnelDriver {
public:
    std::atomic<int> starts{0};
    std::atomic<int> loads{0}, opens{0};
    std::atomic<int> creates{0}, ends{0}, closes{0};
    TunnelActivity activity;
    bool loaded = false;
    ErrorCode failure = ErrorCode::none;
    Status loadAndVerify(const std::wstring&) override {
        ++loads;
        if (failure != ErrorCode::none) return Error(failure);
        loaded = true;
        return ok();
    }
    Result<TunnelAdapterInfo> createAdapter(const std::wstring&, const std::wstring&, const std::wstring&) override { ++creates; return TunnelAdapterInfo{123, "fixture-adapter", true}; }
    Result<TunnelAdapterInfo> openAdapter(const std::wstring&) override {
        ++opens;
        if (!loaded) return Error(ErrorCode::tunnel_library_missing);
        return TunnelAdapterInfo{123, "fixture-adapter", false};
    }
    Result<std::shared_ptr<Tunnel>> startSession(std::uint32_t) override { ++starts; return std::shared_ptr<Tunnel>(new IdleTunnel(activity)); }
    void endSession() override { ++ends; }
    void closeAdapter() override { ++closes; }
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

void setStartupPreference(EngineFixture& f, bool enabled) {
    auto dom = Json::parse(*f.files->bytes).value();
    for (auto& [key, master] : dom.objectValue()) if (key == "masterSetup")
        master.objectValue().emplace_back("autoConnectOnStartup", JsonValue::makeBool(enabled));
    f.files->bytes = Json::serialize(dom);
}

struct WizardServiceFixture {
    EngineFixture runtime;
    ClientConfigPtr relayConfig = fixtureConfig();
    std::function<void()> diagnosticTask;
    std::deque<std::function<void()>> fileTasks;
    std::unique_ptr<WizardService> service;
    std::uint64_t sequence = 0;
    unsigned httpCalls = 0;
    std::vector<ScriptedTransport::HeaderObservation> probeHeaders;
    std::function<void(ScriptedTransport&)> configureProbe;
    bool platformProbe = false;
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
            if (platformProbe) return createPlatformTransport();
            auto transport = std::make_unique<ScriptedTransport>(*runtime.crypto.actual, relayConfig);
            transport->hook = [&, observed = transport.get()](HttpAction, std::string_view, int) -> std::optional<HttpExchangeResult> {
                ++httpCalls;
                probeHeaders.push_back(observed->observedHeaders().back());
                return {};
            };
            if (configureProbe) configureProbe(*transport);
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
        if (!reply) throw std::runtime_error("Wizard reply failed strict schema validation");
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
    DropCounters outDrops, inDrops;
    test::FixedFlowHasher flowHasher;
    DirectionQueues outgoing(100000, 0, 1, clock, flowHasher, outDrops), incoming(100000, 0, 1, clock, flowHasher, inDrops);
    PHPVPN_REQUIRE_OK(worker.verifyAccessKey(incoming, inDrops));
    PHPVPN_CHECK_EQ(counters.snapshot().dataPollsEmpty, std::uint64_t(1));
    DropReason reason{};
    const auto peer = *Ipv4Address::parse("10.99.99.3");
    PHPVPN_CHECK(outgoing.tryEnqueue(peer, packet(100, false), reason).valueOr(false));
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
    DirectionQueues full(0, 0, 1, clock, flowHasher, inDrops);
    PHPVPN_REQUIRE_OK(worker.exchangeData(outgoing, full, outDrops, inDrops, true));
    PHPVPN_CHECK_EQ(counters.snapshot().ipv4PacketsReceived, std::uint64_t(3));
    PHPVPN_CHECK_EQ(counters.snapshot().serializedDataResponseBodyBytes, transport.dataResponseBytes.load());
    const auto countBeforeBuildFailure = counters.snapshot().dataPollsTotal;
    crypto.failRandom = true;
    PHPVPN_CHECK(!worker.exchangeData(outgoing, incoming, outDrops, inDrops, true).ok());
    PHPVPN_CHECK_EQ(counters.snapshot().dataPollsTotal, countBeforeBuildFailure);
}

PHPVPN_TEST(runtime, worker_rejects_whole_batch_when_second_packet_has_wrong_addresses) {
    ObservedCrypto crypto;
    ScriptedTransport transport(*crypto.actual);
    TestClock clock;
    NetworkWorker worker(crypto, transport, clock);
    TrafficCounters counters;
    PHPVPN_REQUIRE_OK(worker.configure(fixtureConfig(), &counters));
    PHPVPN_REQUIRE_OK(worker.performHealth());
    DropCounters outDrops, inDrops;
    test::FixedFlowHasher flowHasher;
    DirectionQueues outgoing(10000, 0, 1, clock, flowHasher, outDrops), incoming(10000, 0, 1, clock, flowHasher, inDrops);
    // Both headers/checksums are valid. Only the second packet's addresses
    // disagree with the authenticated incoming record (.3 -> .2).
    transport.incoming = {packet(40, true), packet(100, false)};
    for (const auto& bytes : transport.incoming) {
        Ipv4PacketView view;
        PHPVPN_CHECK(validateIpv4Packet(bytes, view));
    }
    auto result = worker.exchangeData(outgoing, incoming, outDrops, inDrops, false);
    PHPVPN_REQUIRE_OK(result);
    PHPVPN_CHECK_EQ(result.value().batchesRejected, std::size_t(1));
    PHPVPN_CHECK_EQ(result.value().packetsReceived, std::size_t(0));
    PHPVPN_CHECK_EQ(incoming.statistics().itemCount, std::size_t(0));
    PHPVPN_CHECK_EQ(incoming.statistics().occupiedBytes, std::int64_t(0));
    PHPVPN_CHECK_EQ(counters.snapshot().ipv4PacketsReceived, std::uint64_t(0));
    PHPVPN_CHECK_EQ(counters.snapshot().ipv4BytesReceived, std::uint64_t(0));
    PHPVPN_CHECK_EQ(inDrops.total(), std::uint64_t(1));
    PHPVPN_CHECK_EQ(inDrops.totalForReason(DropReason::batch_invalid), std::uint64_t(1));
}

PHPVPN_TEST(runtime, worker_uses_one_peer_key_in_both_directions_and_binds_aad) {
    ObservedCrypto crypto;
    const auto config = fixtureConfig();
    ScriptedTransport transport(*crypto.actual, config);
    TestClock clock;
    NetworkWorker worker(crypto, transport, clock);
    TrafficCounters counters;
    PHPVPN_REQUIRE_OK(worker.configure(config, &counters));
    PHPVPN_REQUIRE_OK(worker.performHealth());
    DropCounters outDrops, inDrops;
    test::FixedFlowHasher flowHasher;
    DirectionQueues outgoing(10000, 0, 1, clock, flowHasher, outDrops), incoming(10000, 0, 1, clock, flowHasher, inDrops);
    const auto peer = *Ipv4Address::parse("10.99.99.3");
    DropReason reason{};
    const auto sent = packet(100, false);
    const auto received = packet(40, true);
    PHPVPN_CHECK(outgoing.tryEnqueue(peer, sent, reason).valueOr(false));
    transport.incoming = {received};
    unsigned verifiedOutgoing = 0;
    const auto verifyRequest = [&](std::string_view request) {
        const auto start = request.find('\n') + 1;
        auto parsed = DataRecord::parse(request.substr(start, request.find('\n', start) - start));
        PHPVPN_REQUIRE_OK(parsed);
        const auto& record = parsed.value();
        PHPVPN_CHECK_EQ(record.src, std::string_view("10.99.99.2"));
        PHPVPN_CHECK_EQ(record.dst, std::string_view("10.99.99.3"));
        std::vector<std::uint8_t> iv, tag, ciphertext;
        PHPVPN_CHECK(Base64Url::decode(record.iv, iv));
        PHPVPN_CHECK(Base64Url::decode(record.tag, tag));
        PHPVPN_CHECK(Base64Url::decode(record.ciphertext, ciphertext));
        // Spell out the wire AAD independently of the production builder.
        const auto prefix = std::string("PHPVPN-DATA\ndata\n") + std::string(record.messageId);
        const auto aad = prefix + "\n10.99.99.2\n10.99.99.3\n" + std::string(record.iv);
        const auto swapped = prefix + "\n10.99.99.3\n10.99.99.2\n" + std::string(record.iv);
        const auto key = config->contracts.front().payloadKey.span();
        auto opened = crypto.actual->aesGcmOpen(key, iv, asBytes(aad), ciphertext, tag);
        PHPVPN_REQUIRE_OK(opened);
        auto packets = PayloadBatch::decodeAndValidate(opened.value().span(), config->virtualIp, peer);
        PHPVPN_REQUIRE_OK(packets);
        PHPVPN_CHECK_EQ(packets.value(), std::vector<PacketBytes>{sent});
        PHPVPN_CHECK(!crypto.actual->aesGcmOpen(key, iv, asBytes(swapped), ciphertext, tag).ok());
        ++verifiedOutgoing;
    };
    transport.hook = [&](HttpAction action, std::string_view request, int) -> std::optional<HttpExchangeResult> {
        if (action == HttpAction::data) verifyRequest(request);
        return {};
    };
    auto result = worker.exchangeData(outgoing, incoming, outDrops, inDrops, true);
    PHPVPN_REQUIRE_OK(result);
    PHPVPN_CHECK_EQ(verifiedOutgoing, 1u);
    PHPVPN_CHECK_EQ(result.value().packetsSent, std::size_t(1));
    PHPVPN_CHECK_EQ(result.value().packetsReceived, std::size_t(1));
    PHPVPN_CHECK_EQ(incoming.takePacket(peer), std::optional<PacketBytes>(received));

    transport.hook = {};
    transport.swappedPayloadAad = true;
    // Routing and outer data-mac remain valid, only payload AAD is reversed.
    result = worker.exchangeData(outgoing, incoming, outDrops, inDrops, false);
    PHPVPN_REQUIRE_OK(result);
    PHPVPN_CHECK_EQ(result.value().batchesRejected, std::size_t(1));
    PHPVPN_CHECK_EQ(result.value().packetsReceived, std::size_t(0));
    PHPVPN_CHECK_EQ(incoming.statistics().itemCount, std::size_t(0));
    PHPVPN_CHECK_EQ(counters.snapshot().ipv4PacketsReceived, std::uint64_t(1));
    PHPVPN_CHECK_EQ(counters.snapshot().ipv4BytesReceived, std::uint64_t(40));
    PHPVPN_CHECK_EQ(inDrops.total(), std::uint64_t(1));
    PHPVPN_CHECK_EQ(inDrops.totalForReason(DropReason::payload_tag_invalid), std::uint64_t(1));
}

PHPVPN_TEST(runtime, worker_accounts_partial_bodies_once_on_every_error) {
    ObservedCrypto crypto;
    ScriptedTransport transport(*crypto.actual);
    TestClock clock;
    NetworkWorker worker(crypto, transport, clock);
    TrafficCounters counters;
    PHPVPN_REQUIRE_OK(worker.configure(fixtureConfig(), &counters));
    PHPVPN_REQUIRE_OK(worker.performHealth());
    DropCounters outDrops, inDrops;
    test::FixedFlowHasher flowHasher;
    DirectionQueues outgoing(10000, 0, 1, clock, flowHasher, outDrops), incoming(10000, 0, 1, clock, flowHasher, inDrops);
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
    DropCounters outDrops, inDrops;
    test::FixedFlowHasher flowHasher;
    DirectionQueues outgoing(10000, 0, 1, clock, flowHasher, outDrops), incoming(10000, 0, 1, clock, flowHasher, inDrops);
    DropReason reason{};
    PHPVPN_CHECK(outgoing.tryEnqueue(*Ipv4Address::parse("10.99.99.3"), packet(100, false), reason).valueOr(false));
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
PHPVPN_TEST(probe, diagnostics_belong_only_to_the_failing_received_exchange) {
    const auto document = analyzeWizardDocument(kConfig).value();
    for (int scenario = 0; scenario < 10; ++scenario) {
        ObservedCrypto crypto; ScriptedTransport transport(*crypto.actual); TestClock clock;
        ProbeClient probe(crypto, transport, clock); probe.captureWizardHttpDiagnostics(true);
        std::string sent, received;
        if (scenario == 0 || scenario == 1) transport.badHealthTag = true;
        if (scenario == 2) transport.badInfo = true;
        if (scenario == 3 || scenario == 4 || scenario == 5 || scenario == 9) {
            transport.hook = [&](HttpAction action, std::string_view request, int) -> std::optional<HttpExchangeResult> {
                if (scenario == 5 && action != HttpAction::info) return {};
                sent = std::string(request);
                received = scenario == 3 ? "" : "<script>inert-fixture</script>";
                return HttpExchangeResult{HttpResponse{scenario == 3 ? 503 : 200, received, scenario == 4 ? "text/html" : "application/json"}, received.size()};
            };
        }
        if (scenario == 6) transport.hook = [](HttpAction action, std::string_view, int) -> std::optional<HttpExchangeResult> {
            if (action == HttpAction::info) return HttpExchangeResult{Error(ErrorCode::transport_tls_failed)};
            return {};
        };
        if (scenario == 7) crypto.beforeHealthCrypto = [&] { crypto.failRandom = true; };
        if (scenario == 8) probe.cancel();
        const bool url = scenario == 0 || scenario == 3 || scenario == 4 || scenario == 9;
        if (url) { auto result = probe.checkUrl(document.context, document.fields); PHPVPN_CHECK_EQ(result.ok(), scenario == 0); }
        else { auto result = probe.checkSecret(document.context, document.fields); PHPVPN_CHECK(!result); }
        auto d = probe.takeFailedHttpDiagnostic();
        const bool expected = scenario >= 1 && scenario <= 5 || scenario == 9;
        PHPVPN_CHECK_EQ(d.has_value(), expected);
        if (d) {
            PHPVPN_CHECK_EQ(d->action, scenario == 2 || scenario == 5 ? HttpAction::info : HttpAction::health);
            if (!sent.empty()) PHPVPN_CHECK(d->request.prefix.ends_with(sent));
            if (scenario == 3 || scenario == 4 || scenario == 5 || scenario == 9) PHPVPN_CHECK(d->response.prefix.ends_with("\r\n\r\n" + received));
        }
        PHPVPN_CHECK(!probe.takeFailedHttpDiagnostic());
        if (scenario == 6 || scenario == 7) PHPVPN_CHECK(!probe.lastHttpStatus());
        if (scenario == 1) PHPVPN_CHECK_EQ(transport.infoCalls.load(), 0);
    }
    ObservedCrypto crypto; ScriptedTransport transport(*crypto.actual); TestClock clock;
    transport.badHealthTag = true;
    ProbeClient normal(crypto, transport, clock);
    PHPVPN_CHECK(!normal.checkSecret(document.context, document.fields)); PHPVPN_CHECK(!normal.takeFailedHttpDiagnostic());
    ProbeClient access(crypto, transport, clock); access.captureWizardHttpDiagnostics(true);
    PHPVPN_CHECK(!access.checkAccess(document, document.fields)); PHPVPN_CHECK(!access.takeFailedHttpDiagnostic());
}
PHPVPN_TEST(wizard_service, diagnostic_retention_and_late_cancel_are_attempt_scoped) {
    for (int cleanup = 0; cleanup < 7; ++cleanup) {
        WizardServiceFixture fixture;
        fixture.request.fields.serverUrl = "http://localhost/" + std::string(100000, 'a');
        fixture.configureProbe = [](ScriptedTransport& transport) {
            transport.mutateResponse = [](HttpResponse& response) { response.body = "<html>fixture</html>"; };
        };
        const auto startFailed = [&] {
            fixture.request.attemptSequence = ++fixture.sequence;
            fixture.request.clientAttemptId = fixture.runtime.crypto.actual->randomUuidV4().value();
            const auto started = fixture.call(WizardCommand::check_url);
            PHPVPN_CHECK(!started.error);
            fixture.request.operationId = started.result.find("operationId")->stringValue();
            fixture.runDiagnostic();
        };
        startFailed();
        auto reply = fixture.call(WizardCommand::get_operation);
        auto result = WizardIpc::parseOperation(reply.result); PHPVPN_REQUIRE_OK(result);
        PHPVPN_CHECK(result->httpDiagnostic);
        PHPVPN_CHECK(!result->error->serverUrl && !result->error->configPath);
        auto old = fixture.request;
        if (cleanup == 0) { const auto cancelled = fixture.call(WizardCommand::cancel_attempt); PHPVPN_CHECK(!cancelled.error); }
        if (cleanup == 1) { const auto cancelled = fixture.call(WizardCommand::cancel_operation); PHPVPN_CHECK(!cancelled.error); }
        if (cleanup == 2) { const auto reset = fixture.call(WizardCommand::reset); PHPVPN_CHECK(!reset.error); }
        if (cleanup == 3) { const auto closed = fixture.call(WizardCommand::close); PHPVPN_CHECK(!closed.error); }
        if (cleanup == 4) { fixture.runtime.clock.now = kWizardLeaseMs; fixture.service->tick(); }
        if (cleanup == 5) { PHPVPN_REQUIRE_OK(fixture.service->interruptForConnect()); }
        if (cleanup == 6) {
            startFailed(); old.command = WizardCommand::cancel_attempt;
            const auto late = fixture.ask(old); PHPVPN_CHECK(!late.error);
        }
        reply = fixture.call(WizardCommand::get_operation);
        if (cleanup == 0 || cleanup == 1 || cleanup == 5 || cleanup == 6) {
            result = WizardIpc::parseOperation(reply.result); PHPVPN_REQUIRE_OK(result);
            PHPVPN_CHECK_EQ(result->httpDiagnostic.has_value(), cleanup == 6);
        } else PHPVPN_CHECK(reply.error.has_value());
    }
}
PHPVPN_TEST(wizard_service, diagnostic_never_publishes_after_cancel_or_closed_worker) {
    for (int invalidation = 0; invalidation < 4; ++invalidation) {
        WizardServiceFixture fixture;
        fixture.request.fields.serverUrl = "http://localhost/";
        fixture.configureProbe = [&](ScriptedTransport& transport) {
            transport.mutateResponse = [&](HttpResponse& response) {
                response.body = "<html>fixture</html>";
                if (invalidation == 0) { const auto reply = fixture.call(WizardCommand::cancel_attempt); PHPVPN_CHECK(!reply.error); }
                if (invalidation == 1) { const auto reply = fixture.call(WizardCommand::close); PHPVPN_CHECK(!reply.error); }
                if (invalidation == 2) { fixture.runtime.clock.now = kWizardLeaseMs; fixture.service->tick(); }
                if (invalidation == 3) fixture.service->shutdown();
            };
        };
        fixture.request.attemptSequence = 1; fixture.request.clientAttemptId = fixture.runtime.crypto.actual->randomUuidV4().value();
        auto started = fixture.call(WizardCommand::check_url); PHPVPN_CHECK(!started.error);
        fixture.request.operationId = started.result.find("operationId")->stringValue(); fixture.runDiagnostic();
        auto result = fixture.call(WizardCommand::get_operation);
        if (!result.error) PHPVPN_CHECK(!result.result.find("httpDiagnostic"));
    }
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

PHPVPN_TEST(runtime, cidr_gate_blocks_set_config_and_connect_before_network_or_tunnel) {
    EngineFixture fixture;
    PHPVPN_REQUIRE_OK(fixture.engine.start());
    IpcDispatcher dispatcher(fixture.engine);
    const auto id = fixture.crypto.actual->randomUuidV4().value();
    for (const auto prefix : {16, 17, 18, 19, 20, 21, 22, 23, 30, 31, 32}) {
        auto invalid = std::string(kConfig);
        invalid.replace(invalid.find("10.99.99.0/24"), 13, "10.99.0.0/" + std::to_string(prefix));
        const auto request = IpcProtocol::buildRequest(IpcCommand::set_config, id, invalid);
        const auto reply = Json::parse(dispatcher.handle(request));
        PHPVPN_REQUIRE_OK(reply);
        PHPVPN_CHECK_EQ(reply->find("error")->find("code")->stringValue(), "config_invalid_cidr");
        PHPVPN_CHECK_EQ(*fixture.files->bytes, kConfig);
        PHPVPN_CHECK_EQ(fixture.files->writes, 0u);
        fixture.files->bytes = invalid;
        PHPVPN_REQUIRE_ERROR(fixture.engine.connect(), ErrorCode::config_invalid_cidr);
        PHPVPN_CHECK_EQ(fixture.transport.healthCalls.load(), 0);
        PHPVPN_CHECK_EQ(fixture.driver.loads.load(), 0);
        PHPVPN_CHECK_EQ(fixture.driver.opens.load(), 0);
        PHPVPN_CHECK_EQ(fixture.driver.starts.load(), 0);
        fixture.files->bytes = kConfig;
    }
    for (const auto prefix : {24, 29}) {
        auto valid = std::string(kConfig);
        valid.replace(valid.find("/24"), 3, "/" + std::to_string(prefix));
        const auto reply = Json::parse(dispatcher.handle(IpcProtocol::buildRequest(IpcCommand::set_config, id, valid)));
        PHPVPN_REQUIRE_OK(reply);
        PHPVPN_CHECK_EQ(reply->find("result")->stringValue(), "ok");
        PHPVPN_CHECK_EQ(*fixture.files->bytes, valid);
    }
    PHPVPN_CHECK_EQ(fixture.files->writes, 2u);
    PHPVPN_CHECK_EQ(fixture.transport.healthCalls.load(), 0);
}

PHPVPN_TEST(wizard_service, cidr_invalid_source_cannot_finish_and_supported_edges_publish) {
    WizardServiceFixture fixture;
    for (const auto prefix : {16, 17, 18, 19, 20, 21, 22, 23, 24, 29, 30, 31, 32}) {
        PHPVPN_CHECK(!fixture.call(WizardCommand::close).error);
        auto text = std::string(kConfig);
        const auto network = prefix >= 24 ? "10.99.99.0/" : "10.99.0.0/";
        text.replace(text.find("10.99.99.0/24"), 13, network + std::to_string(prefix));
        fixture.runtime.files->bytes = text;
        const auto opened = fixture.call(WizardCommand::open);
        PHPVPN_CHECK(!opened.error);
        const auto view = WizardIpc::parseView(opened.result);
        PHPVPN_REQUIRE_OK(view);
        fixture.request.sessionId = view->sessionId;
        const auto writes = fixture.runtime.files->writes;
        const auto calls = fixture.httpCalls;
        if (prefix == 24 || prefix == 29) {
            PHPVPN_CHECK_EQ(view->availability, ConfigAvailability::valid);
            fixture.request.fields = *view->fields;
            fixture.allChecks();
            const auto started = fixture.beginFinish();
            PHPVPN_CHECK(!started.error);
            if (started.error) return;
            fixture.runFile();
            const auto committed = fixture.call(WizardCommand::get_commit);
            PHPVPN_CHECK_EQ(committed.result.find("commitState")->stringValue(), "confirmed");
            PHPVPN_CHECK_EQ(fixture.runtime.files->writes, writes + 1);
            const auto saved = fixture.runtime.store.readValid();
            PHPVPN_REQUIRE_OK(saved);
            PHPVPN_CHECK_EQ(saved.value()->vpnCidr.prefixLength(), prefix);
        } else {
            PHPVPN_CHECK_EQ(view->availability, ConfigAvailability::invalid);
            PHPVPN_CHECK(!view->context && !view->fields);
            const auto finish = fixture.beginFinish();
            PHPVPN_CHECK(finish.error && finish.error->code == "wizard_prerequisite_failed");
            PHPVPN_CHECK(fixture.fileTasks.empty());
            PHPVPN_CHECK_EQ(fixture.runtime.files->writes, writes);
            PHPVPN_CHECK_EQ(fixture.httpCalls, calls);
            PHPVPN_CHECK_EQ(*fixture.runtime.files->bytes, text);
        }
    }
}

PHPVPN_TEST(wizard_service, create_default_preserves_directory_security_error_and_config_path) {
    WizardServiceFixture fixture;
    fixture.runtime.files->bytes.reset();
    fixture.runtime.files->publishError = ErrorCode::config_directory_security_invalid;
    const auto reply = fixture.call(WizardCommand::create_default_config);
    PHPVPN_CHECK(reply.error.has_value());
    PHPVPN_CHECK_EQ(reply.error->code, std::string("config_directory_security_invalid"));
    PHPVPN_CHECK_EQ(*reply.error->configPath, fixture.runtime.files->fixed.generic_string());
    PHPVPN_CHECK(reply.error->suggestedAction.find("Next") == std::string::npos);
    PHPVPN_CHECK(reply.error->description.find("PRIVATE") == std::string::npos);
    PHPVPN_CHECK(!fixture.runtime.files->bytes);
    PHPVPN_CHECK_EQ(fixture.httpCalls, 0u);
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
PHPVPN_TEST(wizard_service, native_lost_start_reply_cancels_attempt_before_delayed_start_without_http) {
    // Removing WizardWindow's cancel-attempt on IPC failure or the service's
    // sequence tombstone must break this UI -> pipe -> runtime regression.
    for (const bool acceptedBeforeReplyLoss : {false, true}) {
        WizardServiceFixture fixture;
        std::mutex mutex;
        std::optional<WizardRequest> delayed, cancellation;
        unsigned starts = 0, cancels = 0;
        const auto pipe = L"\\\\.\\pipe\\phpvpn-tt13-" +
            phpvpn::tray::widen(fixture.runtime.crypto.actual->randomUuidV4().value());
        IpcPipeServerOptions options; options.pipeName = pipe;
        IpcPipeServer server([&](std::string_view bytes) {
            const auto request = WizardIpc::parseRequest(bytes);
            if (!request) return std::string{};
            std::lock_guard lock(mutex);
            if (request->command == WizardCommand::check_url && ++starts == 1) {
                delayed = *request;
                // Exercise both an undelivered start and a queued operation
                // whose acknowledgement is lost before the UI learns its ID.
                if (acceptedBeforeReplyLoss) fixture.service->handle(*request);
                return std::string{};
            }
            if (request->command == WizardCommand::cancel_attempt) {
                ++cancels; cancellation = *request;
            }
            return fixture.service->handle(*request);
        }, options);
        PHPVPN_REQUIRE_OK(server.start());
        phpvpn::tray::WizardWindow wizard(pipe);
        PHPVPN_CHECK(wizard.open(GetModuleHandleW(nullptr)));
        const auto pumpUntil = [&](const std::function<bool()>& predicate) {
            const auto deadline = GetTickCount64() + 7000;
            do {
                MSG message{};
                while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
                    if (message.message != WM_QUIT && !wizard.translate(message)) {
                        TranslateMessage(&message); DispatchMessageW(&message);
                    }
                }
                if (predicate()) return true;
                Sleep(1);
            } while (GetTickCount64() < deadline);
            return false;
        };
        HWND window = nullptr;
        EnumThreadWindows(GetCurrentThreadId(), [](HWND hwnd, LPARAM value) -> BOOL {
            wchar_t name[80]{}; GetClassNameW(hwnd, name, 80);
            if (std::wstring_view(name) == L"PhpVpnConfigurationWizard")
                *reinterpret_cast<HWND*>(value) = hwnd;
            return TRUE;
        }, reinterpret_cast<LPARAM>(&window));
        const auto child = [&](int id) {
            struct Search { int id; HWND found = nullptr; } search{id};
            EnumChildWindows(window, [](HWND hwnd, LPARAM value) -> BOOL {
                auto& search = *reinterpret_cast<Search*>(value);
                if (GetDlgCtrlID(hwnd) == search.id) { search.found = hwnd; return FALSE; }
                return TRUE;
            }, reinterpret_cast<LPARAM>(&search));
            return search.found;
        };
        PHPVPN_CHECK(window != nullptr);
        const bool ready = pumpUntil([&] { return child(710) && IsWindowEnabled(child(701)); });
        PHPVPN_CHECK(ready);
        if (!ready) { wizard.endSession(); return; }
        SendMessageW(child(701), BM_CLICK, 0, 0);
        const bool cancelled = pumpUntil([&] {
            std::lock_guard lock(mutex); return cancellation.has_value();
        });
        PHPVPN_CHECK(cancelled);
        if (!cancelled) { wizard.endSession(); return; }
        {
            std::lock_guard lock(mutex);
            PHPVPN_CHECK(delayed.has_value());
            PHPVPN_CHECK_EQ(starts, 1u);
            PHPVPN_CHECK_EQ(cancels, 1u);
            PHPVPN_CHECK(cancellation->operationId.empty());
            PHPVPN_CHECK_EQ(cancellation->sessionId, delayed->sessionId);
            PHPVPN_CHECK_EQ(cancellation->serviceInstanceId, delayed->serviceInstanceId);
            PHPVPN_CHECK_EQ(cancellation->clientAttemptId, delayed->clientAttemptId);
            PHPVPN_CHECK_EQ(cancellation->attemptSequence, delayed->attemptSequence);
            fixture.runDiagnostic(); // queued cancelled work must never reach HTTP
            const auto reply = fixture.ask(*delayed);
            PHPVPN_CHECK(reply.error && reply.error->code == "wizard_attempt_closed");
            fixture.runDiagnostic(); // would expose an incorrectly accepted late start
            PHPVPN_CHECK_EQ(fixture.httpCalls, 0u);
            PHPVPN_CHECK_EQ(fixture.runtime.files->writes, 0u);
        }
        PHPVPN_CHECK(pumpUntil([&] { return child(710) && IsWindowEnabled(child(701)); }));
        PHPVPN_CHECK(!child(711));
        // A new explicit UI action is the only way to start fresh HTTP work.
        SendMessageW(child(701), BM_CLICK, 0, 0);
        PHPVPN_CHECK(pumpUntil([&] { std::lock_guard lock(mutex); return starts == 2; }));
        {
            std::lock_guard lock(mutex);
            fixture.runDiagnostic();
            PHPVPN_CHECK_EQ(fixture.httpCalls, 1u);
        }
        PHPVPN_CHECK(pumpUntil([&] { return child(711) != nullptr; }));
        wizard.endSession();
        server.stop();
    }
}

// Explicit opt-in only. The caller owns a separate PHP fixture endpoint and
// its control file, and restores the environment in finally. Never logs HTTP.
void runNativeHttpDiagnosticIntegration(bool external) {
    wchar_t endpointBuffer[2048]{}, controlBuffer[2048]{};
    if (!GetEnvironmentVariableW(external ? L"PHPVPN_DIAGNOSTIC_EXTERNAL" : L"PHPVPN_DIAGNOSTIC_ENDPOINT", endpointBuffer, 2048) ||
        (!external && !GetEnvironmentVariableW(L"PHPVPN_DIAGNOSTIC_CONTROL", controlBuffer, 2048))) {
        std::cout << "[ NOT RUN ] isolated HTTP diagnostic endpoint not configured\n";
        return;
    }
    const auto setMode = [&](std::string_view mode) {
        if (external) return;
        std::ofstream out(std::filesystem::path(controlBuffer), std::ios::binary | std::ios::trunc);
        out.write(mode.data(), static_cast<std::streamsize>(mode.size()));
        if (!out) throw std::runtime_error("fixture control write failed");
    };
    const auto narrowUrl = phpvpn::tray::narrow(endpointBuffer);
    const std::vector<std::string> scenarios = external ? std::vector<std::string>{"external"} :
        std::vector<std::string>{"health", "secret-health", "info", "single", "size65535", "size65536", "size65537", "empty", "noresponse"};
    for (const std::string& scenario : scenarios) {
        WizardServiceFixture fixture; fixture.platformProbe = true; fixture.runtime.clock.realWall = true;
        const bool secret = scenario == "secret-health" || scenario == "info";
        std::string mode = scenario == "secret-health" ? "health" : scenario;
        std::uint64_t targetSize = 0;
        if (scenario.starts_with("size")) {
            targetSize = std::stoull(scenario.substr(4));
            setMode("body65000");
            auto transport = createPlatformTransport().value(); TransportSettings settings; settings.httpHeaders = "X-Test: fixture";
            settings.serverUrl = narrowUrl; settings.captureWizardHttpDiagnostics = true;
            PHPVPN_REQUIRE_OK(transport->configure(settings));
            const auto calibrated = transport->post(HttpAction::health, "{}");
            if (!calibrated.diagnostic) throw std::runtime_error("fixture calibration received no diagnostic");
            const auto headerSize = calibrated.diagnostic->response.prefix.find("\r\n\r\n") + 4;
            if (headerSize < 4 || headerSize > 10000) throw std::runtime_error("fixture header calibration failed");
            mode = "body" + std::to_string(targetSize - headerSize);
        }
        setMode(secret ? "valid" : mode);
        std::mutex mutex;
        std::vector<std::jthread> workers;
        unsigned starts = 0, cancellations = 0;
        std::optional<WizardOperationResult> observed;
        const auto pipe = L"\\\\.\\pipe\\phpvpn-http-diagnostic-" + phpvpn::tray::widen(fixture.runtime.crypto.actual->randomUuidV4().value());
        IpcPipeServerOptions options; options.pipeName = pipe;
        IpcPipeServer server([&](std::string_view bytes) {
            const auto request = WizardIpc::parseRequest(bytes); if (!request) return std::string{};
            std::lock_guard lock(mutex);
            auto reply = fixture.service->handle(*request);
            if (request->command == WizardCommand::check_url || request->command == WizardCommand::check_secret) {
                ++starts;
                auto task = std::move(fixture.diagnosticTask); fixture.diagnosticTask = {};
                if (task) workers.emplace_back(std::move(task));
            }
            if (request->command == WizardCommand::cancel_attempt) ++cancellations;
            if (request->command == WizardCommand::get_operation) {
                auto parsed = WizardIpc::parseReply(request->command, request->requestId, reply);
                if (parsed && !parsed->error) observed = WizardIpc::parseOperation(parsed->result).value();
            }
            return reply;
        }, options);
        PHPVPN_REQUIRE_OK(server.start());
        phpvpn::tray::WizardWindow wizard(pipe);
        if (!wizard.open(GetModuleHandleW(nullptr))) throw std::runtime_error("integration wizard failed to open");
        const auto top = [](const wchar_t* type) {
            struct Search { const wchar_t* type; HWND found = nullptr; } search{type};
            EnumThreadWindows(GetCurrentThreadId(), [](HWND h, LPARAM p) -> BOOL {
                auto& s = *reinterpret_cast<Search*>(p); wchar_t name[100]{}; GetClassNameW(h, name, 100);
                if (std::wstring_view(name) == s.type) s.found = h; return TRUE;
            }, reinterpret_cast<LPARAM>(&search)); return search.found;
        };
        const auto window = top(L"PhpVpnConfigurationWizard");
        const auto child = [&](int id) {
            struct Search { int id; HWND found = nullptr; } search{id};
            EnumChildWindows(window, [](HWND h, LPARAM p) -> BOOL {
                auto& s = *reinterpret_cast<Search*>(p); if (GetDlgCtrlID(h) == s.id) { s.found = h; return FALSE; } return TRUE;
            }, reinterpret_cast<LPARAM>(&search)); return search.found;
        };
        const auto until = [&](const std::function<bool()>& predicate) {
            const auto deadline = GetTickCount64() + (external ? 65000 : 12000);
            do {
                MSG message{}; while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
                    if (message.message != WM_QUIT && !wizard.translate(message)) { TranslateMessage(&message); DispatchMessageW(&message); }
                if (predicate()) return true; Sleep(1);
            } while (GetTickCount64() < deadline);
            return false;
        };
        const auto require = [&](bool value) { if (!value) { wizard.endSession(); throw std::runtime_error("native HTTP diagnostic integration assertion failed"); } };
        require(until([&] { return child(710) && IsWindowEnabled(child(701)); }));
        SetWindowTextW(child(710), scenario == "noresponse" ? L"http://127.0.0.1:1/phpvpn.php" : endpointBuffer);
        SendMessageW(child(701), BM_CLICK, 0, 0);
        if (secret) {
            require(until([&] { return child(711) && IsWindowEnabled(child(701)); }));
            setMode(mode); SendMessageW(child(701), BM_CLICK, 0, 0);
        }
        require(until([&] { std::lock_guard lock(mutex); return observed &&
            (observed->state == WizardOperationState::failed || (external && observed->state == WizardOperationState::succeeded)) && IsWindowEnabled(child(701)); }));
        if (external && observed->state == WizardOperationState::succeeded) {
            require(!IsWindowVisible(child(705))); wizard.endSession(); server.stop();
            std::cout << "[ NOT RUN ] external malformed response unavailable, valid URL probe succeeded without detail\n";
            continue;
        }
        std::optional<HttpDiagnostic> diagnostic;
        { std::lock_guard lock(mutex); diagnostic = observed->httpDiagnostic; }
        const bool noResponse = external ? !diagnostic : scenario == "noresponse";
        require(diagnostic.has_value() != noResponse);
        require((IsWindowVisible(child(705)) != FALSE) != noResponse);
        HWND detail = nullptr;
        if (diagnostic) {
            require(diagnostic->action == (scenario == "info" ? HttpAction::info : HttpAction::health));
            if (!external) {
                require(diagnostic->responseComplete);
                require(diagnostic->response.prefix.find("X-Diagnostic-Duplicate: first\r\nX-Diagnostic-Duplicate: second") != std::string::npos);
            }
            if (targetSize) {
                require(diagnostic->response.originalSizeBytes == targetSize);
                require(diagnostic->response.prefix.size() == (targetSize <= 65536 ? targetSize : 64512));
                require(diagnostic->response.suffix.size() == (targetSize <= 65536 ? 0 : 1024));
            }
            SendMessageW(child(705), BM_CLICK, 0, 0); detail = top(L"PhpVpnHttpErrorDetail");
            require(detail && GetWindow(detail, GW_OWNER) == window);
            const auto readText = [](HWND h) { std::wstring value(GetWindowTextLengthW(h) + 1, L'\0'); GetWindowTextW(h, value.data(), static_cast<int>(value.size())); value.resize(value.size() - 1); return value; };
            require(readText(GetDlgItem(detail, 781)) == diagnosticDisplayText(diagnostic->request).text);
            require(readText(GetDlgItem(detail, 782)) == diagnosticDisplayText(diagnostic->response).text);
            if (scenario == "health" || scenario == "secret-health" || scenario == "info") {
                require(diagnostic->response.prefix.find("<script>fixture") != std::string::npos);
                require(diagnostic->response.prefix.ends_with("</script>tail"));
            }
        }
        if (external) {
            const auto code = observed->error ? observed->error->code : "missing-error";
            wizard.endSession(); server.stop();
            std::cout << (noResponse ? "[ NOT RUN ] external received-response scenario unavailable, negative wizard result PASS: " : "[ PASS ] external received-error detail: ") << code << "\n";
            continue;
        }
        unsigned before = 0; { std::lock_guard lock(mutex); before = starts; }
        setMode("empty"); SendMessageW(child(701), BM_CLICK, 0, 0);
        require(!detail || !IsWindow(detail));
        require(until([&] { std::lock_guard lock(mutex); return starts > before && IsWindowEnabled(child(701)); }));
        wizard.endSession(); server.stop();
        require(!detail || !IsWindow(detail));
        std::cout << "[ PASS ] local HTTP diagnostic " << scenario << "\n";
    }
}
PHPVPN_TEST(wizard_service, native_http_diagnostic_local_integration) { runNativeHttpDiagnosticIntegration(false); }
PHPVPN_TEST(wizard_service, native_http_diagnostic_external_best_effort) { runNativeHttpDiagnosticIntegration(true); }

void runConnectHttpDiagnosticIntegration(bool external, bool infoOnly = false) {
    wchar_t endpoint[2048]{}, control[2048]{};
    if (!GetEnvironmentVariableW(external ? L"PHPVPN_DIAGNOSTIC_EXTERNAL" : L"PHPVPN_DIAGNOSTIC_ENDPOINT", endpoint, 2048) ||
        (!external && !GetEnvironmentVariableW(L"PHPVPN_DIAGNOSTIC_CONTROL", control, 2048))) {
        std::cout << "[ NOT RUN ] Connect HTTP integration endpoint not configured\n"; return;
    }
    const std::vector<std::string> modes = external ? std::vector<std::string>{"external"} : infoOnly ?
        std::vector<std::string>{"info"} : std::vector<std::string>{"health", "info", "noresponse"};
    for (const auto& mode : modes) {
        if (!external) { std::ofstream out(std::filesystem::path(control), std::ios::binary | std::ios::trunc); out << mode; if (!out) throw std::runtime_error("control write failed"); }
        EngineFixture f; f.clock.realWall = true;
        auto config = Json::parse(kConfig).value();
        for (auto& [name, value] : config.objectValue()) if (name == "masterSetup") {
            for (auto& [key, setting] : value.objectValue()) if (key == "serverUrl")
                setting = JsonValue::makeString(mode == "noresponse" ? "http://127.0.0.1:1/phpvpn.php" : tray::narrow(endpoint));
            value.objectValue().emplace_back("trustAnyTlsCertificate", JsonValue::makeBool(false));
        }
        f.files->bytes = Json::serialize(config);
        auto transport = createPlatformTransport().value();
        struct BootstrapOnly final : Transport {
            Transport& transport;
            explicit BootstrapOnly(Transport& value) : transport(value) {}
            Status configure(const TransportSettings& settings) override { return transport.configure(settings); }
            HttpExchangeResult post(HttpAction action, std::string_view body) override {
                if (action == HttpAction::data) return Error(ErrorCode::cancelled);
                return transport.post(action, body);
            }
            HttpExchangeResult postDiagnostic(HttpAction action, std::string_view body) override {
                if (action == HttpAction::data) return Error(ErrorCode::cancelled);
                return transport.postDiagnostic(action, body);
            }
            void cancelCurrentRequest() override { transport.cancelCurrentRequest(); }
        } bootstrap(*transport);
        VpnEngine engine(f.crypto, bootstrap, f.clock, f.driver, f.net,
            EnginePaths{f.directory / "config.json", f.directory / "journal.json", f.directory.wstring()}, f.store);
        IpcDispatcher dispatcher(engine);
        const auto pipe = L"\\\\.\\pipe\\phpvpn-connect-http-" + tray::widen(f.crypto.actual->randomUuidV4().value());
        IpcPipeServerOptions options; options.pipeName = pipe;
        IpcPipeServer server([&](std::string_view bytes) { return dispatcher.handle(bytes); }, options);
        PHPVPN_REQUIRE_OK(engine.start()); PHPVPN_REQUIRE_OK(server.start());
        auto reply = dispatcher.handle(IpcProtocol::buildRequest(IpcCommand::connect, kMessageId));
        auto accepted = tray::parseStatusResponse(reply); PHPVPN_CHECK(accepted.valid);
        tray::ProgressWindow progress(pipe); progress.open(GetModuleHandleW(nullptr)); progress.onSnapshot(accepted);
        const auto until = [&](const std::function<bool()>& condition) {
            const auto deadline = GetTickCount64() + (external ? 45000 : 12000);
            do {
                MSG message{}; while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
                    if (message.message != WM_QUIT && !progress.translate(message)) { TranslateMessage(&message); DispatchMessageW(&message); }
                if (condition()) return true; Sleep(1);
            } while (GetTickCount64() < deadline);
            return false;
        };
        PHPVPN_CHECK(until([&] { return progress.model().view().stage == tray::ProgressStage::failed; }));
        const auto status = engine.status();
        const bool available = status.httpDiagnostic.has_value();
        if (!external) PHPVPN_CHECK_EQ(available, mode != "noresponse");
        const auto button = GetDlgItem(progress.window(), 412);
        PHPVPN_CHECK_EQ(IsWindowVisible(button) != FALSE, available);
        if (available) {
            PHPVPN_CHECK_EQ(status.httpDiagnostic->action, mode == "info" ? HttpAction::info : HttpAction::health);
            SendMessageW(button, BM_CLICK, 0, 0);
            struct Search { HWND owner; HWND found = nullptr; } search{progress.window()};
            EnumThreadWindows(GetCurrentThreadId(), [](HWND window, LPARAM value) -> BOOL {
                auto& result = *reinterpret_cast<Search*>(value); wchar_t name[64]{};
                GetClassNameW(window, name, 64);
                if (GetWindow(window, GW_OWNER) == result.owner && std::wstring_view(name) == L"PhpVpnHttpErrorDetail") result.found = window;
                return TRUE;
            }, reinterpret_cast<LPARAM>(&search));
            HWND detail = search.found;
            PHPVPN_CHECK(detail && GetWindow(detail, GW_OWNER) == progress.window());
            if (detail) {
                for (const auto& [id, expected] : std::array<std::pair<int, std::wstring>, 2>{
                    std::pair{781, diagnosticDisplayText(status.httpDiagnostic->request).text},
                    std::pair{782, diagnosticDisplayText(status.httpDiagnostic->response).text}}) {
                    const auto edit = GetDlgItem(detail, id); std::wstring actual(GetWindowTextLengthW(edit) + 1, L'\0');
                    GetWindowTextW(edit, actual.data(), static_cast<int>(actual.size())); actual.resize(actual.size() - 1);
                    PHPVPN_CHECK_EQ(actual, expected);
                }
            }
        }
        SendMessageW(progress.window(), WM_CLOSE, 0, 0);
        if (available) PHPVPN_CHECK(until([&] { return !engine.status().httpDiagnostic; }));
        PHPVPN_CHECK_EQ(f.driver.starts.load(), 0); // Real HTTP, isolated fake tunnel and no installed service.
        engine.shutdown(); server.stop();
        std::cout << (external && !available ? "[ NOT RUN ] external received-response case unavailable: " : "[ PASS ] Connect HTTP detail ") << mode << "\n";
    }
}
PHPVPN_TEST(runtime, connect_http_local_integration) { runConnectHttpDiagnosticIntegration(false); }
PHPVPN_TEST(runtime, connect_http_info_local_integration) { runConnectHttpDiagnosticIntegration(false, true); }
PHPVPN_TEST(runtime, connect_http_external_best_effort) { runConnectHttpDiagnosticIntegration(true); }

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
    auto saved=Json::parse(makeDefaultEditorDocument()).value();
    saved.objectValue().emplace_back("runtime",JsonValue::makeObject({{"maxQueuedPacketAgeMs",JsonValue::makeInteger(1234)}}));
    const auto* originalHasher=VpnEngineTestAccess::flowHasherIdentity(fixture.engine);
    PHPVPN_REQUIRE_OK(fixture.engine.setConfigJson(Json::serialize(saved)));
    fixture.files->bytes.reset();
    fixture.clock.now += 10000;
    PHPVPN_CHECK(data.await());
    PHPVPN_CHECK_EQ(VpnEngineTestAccess::queueAge(fixture.engine),std::uint64_t(5000));
    PHPVPN_CHECK_EQ(VpnEngineTestAccess::flowHasherIdentity(fixture.engine),originalHasher);
    // Scripted relay still authenticates with the original secret and key.
    PHPVPN_CHECK(fixture.engine.status().state == ConnectionState::connected);
    for (const auto& request : fixture.transport.observedHeaders()) PHPVPN_CHECK_EQ(request.headers, "X-Test: fixture");
    const auto reads = fixture.files->reads;
    PHPVPN_REQUIRE_OK(fixture.engine.connect());
    PHPVPN_CHECK_EQ(fixture.files->reads, reads);
    PHPVPN_REQUIRE_OK(fixture.engine.disconnect());
    PHPVPN_CHECK(eventually([&] { return fixture.engine.status().state == ConnectionState::disconnected; }));
    PHPVPN_REQUIRE_ERROR(fixture.engine.connect(), ErrorCode::config_not_found);
}

PHPVPN_TEST(runtime, polling_monotonic_window_bounds_and_lifecycle) {
    ObservedCrypto crypto;
    TestClock clock;
    ScriptedTransport transport(*crypto.actual);
    NetworkWorker worker(crypto, transport, clock);
    PHPVPN_REQUIRE_OK(worker.configure(fixtureConfig()));
    PHPVPN_REQUIRE_OK(VpnEngineTestAccess::limits(worker, 50, 3200));
    worker.beginDataSession();
    auto step = [&](std::int64_t at, bool packet = false) {
        clock.now = at;
        DataExchangeOutcome result;
        result.completedAtMs = at;
        if (packet) result.lastPacketAtMs = at;
        return worker.nextPollDelayMs(result);
    };
    for (int i = 0; i < 200; ++i) PHPVPN_CHECK_EQ(step(0), std::int64_t(50));
    PHPVPN_CHECK_EQ(step(4999), std::int64_t(50));
    PHPVPN_CHECK_EQ(step(5000), std::int64_t(100));
    for (auto expected : {200, 400, 800, 1600, 3200, 3200})
        PHPVPN_CHECK_EQ(step(5001), std::int64_t(expected));
    PHPVPN_CHECK_EQ(step(5002, true), std::int64_t(50));
    PHPVPN_CHECK_EQ(step(10000), std::int64_t(50));
    PHPVPN_CHECK_EQ(step(10001, true), std::int64_t(50));
    PHPVPN_CHECK_EQ(step(15000), std::int64_t(50));
    PHPVPN_CHECK_EQ(step(15001), std::int64_t(100));
    PHPVPN_REQUIRE_OK(VpnEngineTestAccess::limits(worker, 50, 750));
    for (auto expected : {200, 400, 750, 750}) PHPVPN_CHECK_EQ(step(15002), std::int64_t(expected));
    PHPVPN_CHECK_EQ(step(16000, true), std::int64_t(50));
    PHPVPN_REQUIRE_OK(VpnEngineTestAccess::limits(worker, 100, 750));
    PHPVPN_CHECK_EQ(step(20999), std::int64_t(100));
    PHPVPN_CHECK_EQ(step(21000), std::int64_t(200));
    PHPVPN_REQUIRE_OK(VpnEngineTestAccess::limits(worker, 20, 20));
    PHPVPN_CHECK_EQ(step(99999), std::int64_t(20));
    PHPVPN_REQUIRE_OK(VpnEngineTestAccess::limits(worker, 1, INT64_MAX));
    for (int i = 0; i < 15; ++i) step(100000);
    PHPVPN_CHECK_EQ(step(100000), std::int64_t(5000));
    for (auto bounds : {std::pair{0LL, 50LL}, {100LL, 50LL}, {5001LL, 6000LL}, {1LL, 19LL}})
        PHPVPN_REQUIRE_ERROR(VpnEngineTestAccess::limits(worker, bounds.first, bounds.second), ErrorCode::response_invalid);
    PHPVPN_REQUIRE_OK(worker.configure(fixtureConfig()));
    PHPVPN_REQUIRE_OK(VpnEngineTestAccess::limits(worker, 50, 3200));
    clock.now = 1000000; // Slow bootstrap does not consume the new window.
    worker.beginDataSession();
    PHPVPN_CHECK_EQ(step(1004999), std::int64_t(50));
    PHPVPN_CHECK_EQ(step(1005000), std::int64_t(100));
    PHPVPN_CHECK_EQ(step(INT64_MAX, true), std::int64_t(50));
    PHPVPN_CHECK_EQ(step(INT64_MIN), std::int64_t(50)); // Fake clock rollback never overflows.
    worker.reset();
    clock.now = 0;
    PHPVPN_REQUIRE_OK(worker.configure(fixtureConfig()));
    worker.beginDataSession();
    PHPVPN_CHECK_EQ(step(4999), std::int64_t(50));
}

PHPVPN_TEST(runtime, polling_packet_handoff_rx_and_control_exchanges) {
    ObservedCrypto crypto;
    TestClock clock;
    ScriptedTransport transport(*crypto.actual);
    NetworkWorker worker(crypto, transport, clock);
    bool cancelled = false;
    PHPVPN_REQUIRE_OK(worker.configure(fixtureConfig(), nullptr, [&] { return cancelled; }));
    PHPVPN_REQUIRE_OK(worker.performHealth());
    PHPVPN_REQUIRE_OK(worker.performInfo());
    worker.beginDataSession();
    DropCounters outDrops, inDrops;
    test::FixedFlowHasher flowHasher;
    DirectionQueues outgoing(10000, 0, 1, clock, flowHasher, outDrops), incoming(10000, 0, 1, clock, flowHasher, inDrops);
    auto exchange = [&] { return worker.exchangeData(outgoing, incoming, outDrops, inDrops, true); };
    clock.now = 5000;
    auto idle = exchange(); PHPVPN_REQUIRE_OK(idle);
    PHPVPN_CHECK(!idle->lastPacketAtMs);
    PHPVPN_CHECK_EQ(worker.nextPollDelayMs(*idle), std::int64_t(160));
    DropReason reason{};
    PHPVPN_CHECK(outgoing.tryEnqueue(*Ipv4Address::parse("10.99.99.3"), packet(40, false), reason).valueOr(false));
    cancelled = true;
    PHPVPN_REQUIRE_ERROR(exchange(), ErrorCode::cancelled);
    PHPVPN_CHECK_EQ(transport.dataCalls.load(), 1);
    cancelled = false; transport.defer = true;
    auto deferred = exchange(); PHPVPN_REQUIRE_OK(deferred);
    PHPVPN_CHECK_EQ(deferred->packetsSent, std::size_t(0));
    PHPVPN_CHECK_EQ(*deferred->lastPacketAtMs, std::int64_t(5000));
    PHPVPN_CHECK_EQ(worker.nextPollDelayMs(*deferred), std::int64_t(80));
    transport.defer = false;
    PHPVPN_REQUIRE_OK(exchange()); // Drain the returned outgoing packet.
    clock.now = 9999;
    transport.incoming = {packet(40, true)};
    auto rx = exchange(); PHPVPN_REQUIRE_OK(rx);
    PHPVPN_CHECK_EQ(rx->packetsReceived, std::size_t(1));
    PHPVPN_CHECK_EQ(*rx->lastPacketAtMs, std::int64_t(9999));
    PHPVPN_CHECK_EQ(worker.nextPollDelayMs(*rx), std::int64_t(80));
    transport.incoming.clear();
    clock.now = 14998;
    idle = exchange(); PHPVPN_REQUIRE_OK(idle);
    clock.now = 20000; // A slow health after data cannot change data's completion time.
    PHPVPN_REQUIRE_OK(worker.performHealth());
    PHPVPN_CHECK_EQ(worker.nextPollDelayMs(*idle), std::int64_t(80));
    idle = exchange(); PHPVPN_REQUIRE_OK(idle);
    PHPVPN_CHECK_EQ(worker.nextPollDelayMs(*idle), std::int64_t(160));
    transport.incoming = {packet(40, true)}; transport.badPacket = true;
    auto rejected = exchange(); PHPVPN_REQUIRE_OK(rejected);
    PHPVPN_CHECK(!rejected->lastPacketAtMs);
    PHPVPN_CHECK_EQ(worker.nextPollDelayMs(*rejected), std::int64_t(320));
}

PHPVPN_TEST(runtime, polling_historical_tx_does_not_pin_minimum) {
    EngineFixture fixture;
    Gate bootstrap, finished;
    std::int64_t observed = 0;
    std::chrono::steady_clock::time_point emptyStarted;
    std::int64_t waitedMs = 0;
    fixture.transport.hook = [&](HttpAction action, std::string_view, int call) -> std::optional<HttpExchangeResult> {
        if (action != HttpAction::data) return {};
        if (call == 1) bootstrap.block();
        if (call == 2) fixture.clock.now = 5000;
        if (call == 3) emptyStarted = std::chrono::steady_clock::now();
        if (call == 4) {
            observed = VpnEngineTestAccess::pollInterval(fixture.engine);
            waitedMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-emptyStarted).count();
            finished.block();
        }
        return {};
    };
    fixture.transport.cancel = [&] { bootstrap.release(); finished.release(); };
    PHPVPN_REQUIRE_OK(fixture.engine.start());
    PHPVPN_REQUIRE_OK(fixture.engine.connect());
    PHPVPN_CHECK(bootstrap.await());
    VpnEngineTestAccess::enqueue(fixture.engine, packet(40, false));
    bootstrap.release();
    PHPVPN_CHECK(finished.await());
    std::cout << "last TX=0 ms, empty poll completion=5000 ms, next interval=" << observed << " ms\n";
    PHPVPN_CHECK_EQ(observed, std::int64_t(160));
    PHPVPN_CHECK(waitedMs >= 140); // The real command wait uses the computed delay.
    PHPVPN_CHECK_EQ(fixture.engine.status().counters.ipv4PacketsSent, std::uint64_t(1));
    fixture.engine.shutdown();
}

PHPVPN_TEST(runtime, queued_packet_shortens_only_a_longer_remaining_wait) {
    // Three seconds left: the packet cuts the wait to the short interval.
    PHPVPN_CHECK_EQ(shortenedPollDeadlineMs(3000, 0, 80), std::int64_t(80));
    PHPVPN_CHECK_EQ(shortenedPollDeadlineMs(4000, 1000, 80), std::int64_t(1080));
    // The remaining wait counts, not the length of the current interval.
    PHPVPN_CHECK_EQ(shortenedPollDeadlineMs(3000, 2950, 80), std::int64_t(3000));
    PHPVPN_CHECK_EQ(shortenedPollDeadlineMs(3000, 2920, 80), std::int64_t(3000));
    PHPVPN_CHECK_EQ(shortenedPollDeadlineMs(3000, 2919, 80), std::int64_t(2999));
    PHPVPN_CHECK_EQ(shortenedPollDeadlineMs(3000, 3000, 80), std::int64_t(3000));
    PHPVPN_CHECK_EQ(shortenedPollDeadlineMs(3000, 3500, 80), std::int64_t(3000));
    // Later packets never postpone an already planned request.
    std::int64_t deadline = 3000;
    for (std::int64_t at : {0, 10, 40, 79, 80, 200}) {
        const std::int64_t next = shortenedPollDeadlineMs(deadline, at, 80);
        PHPVPN_CHECK(next <= deadline);
        deadline = next;
    }
    PHPVPN_CHECK_EQ(deadline, std::int64_t(80));
    PHPVPN_CHECK_EQ(shortenedPollDeadlineMs(INT64_MAX, INT64_MIN, 80), INT64_MIN + 80);
}

PHPVPN_TEST(runtime, queued_packet_wakes_a_long_poll_wait_after_the_short_interval) {
    EngineFixture fixture;
    std::atomic<bool> idle{false}, queued{false};
    std::atomic<std::int64_t> polledAfterMs{-1};
    std::chrono::steady_clock::time_point queuedAt;
    fixture.transport.hook = [&](HttpAction action, std::string_view, int call) -> std::optional<HttpExchangeResult> {
        if (action != HttpAction::data) return {};
        // Far from the last packet, so the interval stays at the server maximum.
        if (call == 2) fixture.clock.now = 100000;
        if (call == 3) {
            VpnEngineTestAccess::setPollInterval(fixture.engine, 1000);
            idle = true;
        }
        if (call == 4 && queued.load())
            polledAfterMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - queuedAt).count();
        return {};
    };
    PHPVPN_REQUIRE_OK(fixture.engine.start());
    PHPVPN_REQUIRE_OK(fixture.engine.connect());
    PHPVPN_CHECK(eventually([&] { return idle.load() && VpnEngineTestAccess::waitingForPoll(fixture.engine); }));
    VpnEngineTestAccess::enqueue(fixture.engine, packet(40, false));
    queuedAt = std::chrono::steady_clock::now();
    queued = true;
    VpnEngineTestAccess::outboundPacketQueued(fixture.engine);
    // Another packet inside the short window leaves the planned request alone.
    VpnEngineTestAccess::outboundPacketQueued(fixture.engine);
    PHPVPN_CHECK(eventually([&] { return polledAfterMs.load() >= 0; }));
    std::cout << "remaining wait=1000 ms, short interval=80 ms, polled after=" << polledAfterMs.load() << " ms\n";
    PHPVPN_CHECK(polledAfterMs.load() >= 60);
    PHPVPN_CHECK(polledAfterMs.load() < 600);
    PHPVPN_CHECK(eventually([&] { return fixture.engine.status().counters.ipv4PacketsSent == 1; }));
    fixture.engine.shutdown();
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

PHPVPN_TEST(runtime, lifecycle_keeps_one_network_role_and_recreates_two_io_roles) {
    Gate bootstrap[3], polls[2];
    std::atomic<bool> sameNetworkThread{true};
    EngineFixture fixture;
    fixture.transport.incoming = {packet(100, true)};
    fixture.transport.hook = [&](HttpAction action, std::string_view, int call) -> std::optional<HttpExchangeResult> {
        if (std::this_thread::get_id() != VpnEngineTestAccess::networkThread(fixture.engine))
            sameNetworkThread.store(false);
        if (call == 1) {
            bootstrap[action == HttpAction::health ? 0 : action == HttpAction::info ? 1 : 2].block();
        }
        if (action == HttpAction::data && (call == 2 || call == 4)) polls[call / 2 - 1].block();
        return {};
    };
    fixture.transport.cancel = [&] {
        for (auto& gate : bootstrap) gate.release();
        polls[fixture.transport.dataCalls.load() >= 4 ? 1 : 0].release();
    };
    PHPVPN_CHECK_EQ(VpnEngineTestAccess::networkThread(fixture.engine), std::thread::id{});
    PHPVPN_REQUIRE_OK(fixture.engine.start());
    const auto networkThread = VpnEngineTestAccess::networkThread(fixture.engine);
    PHPVPN_CHECK(networkThread != std::thread::id{});
    PHPVPN_CHECK_EQ(fixture.engine.status().state, ConnectionState::disconnected);
    PHPVPN_CHECK_EQ(VpnEngineTestAccess::tunnelRoles(fixture.engine), 0u);
    PHPVPN_CHECK_EQ(fixture.driver.starts.load(), 0);
    PHPVPN_CHECK_EQ(fixture.transport.healthCalls.load(), 0);
    PHPVPN_REQUIRE_ERROR(fixture.engine.start(), ErrorCode::service_already_running);
    PHPVPN_REQUIRE_OK(fixture.engine.connect());
    for (auto& gate : bootstrap) {
        PHPVPN_CHECK(gate.await());
        PHPVPN_CHECK_EQ(VpnEngineTestAccess::tunnelRoles(fixture.engine), 0u);
        PHPVPN_CHECK_EQ(fixture.driver.loads.load(), 0);
        PHPVPN_CHECK_EQ(fixture.driver.creates.load(), 0);
        PHPVPN_CHECK_EQ(fixture.driver.starts.load(), 0);
        PHPVPN_CHECK_EQ(fixture.driver.activity.readers.load(), 0);
        PHPVPN_CHECK_EQ(fixture.driver.activity.writers.load(), 0);
        PHPVPN_REQUIRE_OK(fixture.engine.connect());
        PHPVPN_CHECK_EQ(VpnEngineTestAccess::networkThread(fixture.engine), networkThread);
        gate.release();
    }
    for (int cycle = 1; cycle <= 2; ++cycle) {
        PHPVPN_CHECK(polls[cycle - 1].await());
        PHPVPN_CHECK_EQ(fixture.engine.status().state, ConnectionState::connected);
        PHPVPN_CHECK_EQ(VpnEngineTestAccess::networkThread(fixture.engine), networkThread);
        PHPVPN_CHECK_EQ(VpnEngineTestAccess::tunnelRoles(fixture.engine), 2u);
        PHPVPN_CHECK(eventually([&] {
            return fixture.driver.activity.readers.load() == cycle && fixture.driver.activity.writers.load() == cycle;
        }));
        PHPVPN_CHECK(fixture.driver.activity.distinctThreads.load());
        PHPVPN_REQUIRE_OK(fixture.engine.connect());
        PHPVPN_CHECK_EQ(fixture.driver.creates.load(), cycle);
        PHPVPN_CHECK_EQ(fixture.driver.starts.load(), cycle);
        PHPVPN_CHECK_EQ(fixture.transport.healthCalls.load(), cycle);
        PHPVPN_CHECK_EQ(fixture.transport.infoCalls.load(), cycle * 2);
        PHPVPN_CHECK_EQ(fixture.transport.dataCalls.load(), cycle * 2);
        if (cycle == 1) {
            PHPVPN_REQUIRE_OK(fixture.engine.disconnect());
            PHPVPN_CHECK(eventually([&] { return fixture.engine.status().state == ConnectionState::disconnected; }));
            PHPVPN_CHECK_EQ(VpnEngineTestAccess::networkThread(fixture.engine), networkThread);
            PHPVPN_CHECK_EQ(VpnEngineTestAccess::tunnelRoles(fixture.engine), 0u);
            PHPVPN_CHECK_EQ(fixture.driver.ends.load(), 1);
            PHPVPN_CHECK_EQ(fixture.driver.closes.load(), 1);
            PHPVPN_CHECK_EQ(fixture.driver.activity.stopped.load(), 1);
            PHPVPN_CHECK_EQ(fixture.driver.activity.destroyed.load(), 1);
            PHPVPN_REQUIRE_OK(fixture.engine.disconnect());
            PHPVPN_REQUIRE_OK(fixture.engine.connect());
        }
    }
    fixture.engine.shutdown();
    fixture.engine.shutdown();
    PHPVPN_CHECK_EQ(VpnEngineTestAccess::networkThread(fixture.engine), std::thread::id{});
    PHPVPN_CHECK_EQ(VpnEngineTestAccess::tunnelRoles(fixture.engine), 0u);
    PHPVPN_CHECK_EQ(fixture.engine.status().state, ConnectionState::disconnected);
    PHPVPN_CHECK_EQ(fixture.driver.ends.load(), 2);
    PHPVPN_CHECK_EQ(fixture.driver.closes.load(), 2);
    PHPVPN_CHECK_EQ(fixture.driver.activity.stopped.load(), 2);
    PHPVPN_CHECK_EQ(fixture.driver.activity.destroyed.load(), 2);
    PHPVPN_CHECK(sameNetworkThread.load());
}

PHPVPN_TEST(runtime, lifecycle_recovery_tears_down_and_recreates_after_current_backoff) {
    Gate failedPoll, recoveredPoll;
    std::atomic<bool> sameNetworkThread{true};
    EngineFixture fixture;
    fixture.transport.incoming = {packet(100, true)};
    fixture.transport.hook = [&](HttpAction action, std::string_view, int call) -> std::optional<HttpExchangeResult> {
        if (std::this_thread::get_id() != VpnEngineTestAccess::networkThread(fixture.engine))
            sameNetworkThread.store(false);
        if (action == HttpAction::data && call == 2) {
            failedPoll.block();
            return HttpExchangeResult{Error(ErrorCode::transport_timeout), 17};
        }
        if (action == HttpAction::health && call >= 2 && call <= 8)
            return HttpExchangeResult{Error(ErrorCode::transport_timeout), 0};
        if (action == HttpAction::data && call == 4) recoveredPoll.block();
        return {};
    };
    fixture.transport.cancel = [&] { failedPoll.release(); recoveredPoll.release(); };
    // A timeout is transient; with soft recovery disabled the first failure
    // takes the full teardown path this scenario verifies.
    VpnEngineTestAccess::softRecoveryGrace(fixture.engine, 0);
    PHPVPN_REQUIRE_OK(fixture.engine.start());
    const auto networkThread = VpnEngineTestAccess::networkThread(fixture.engine);
    PHPVPN_REQUIRE_OK(fixture.engine.connect());
    PHPVPN_CHECK(failedPoll.await());
    PHPVPN_CHECK(eventually([&] {
        return fixture.driver.activity.readers.load() == 1 && fixture.driver.activity.writers.load() == 1;
    }));
    failedPoll.release();
    PHPVPN_CHECK(eventually([&] { return fixture.engine.status().state == ConnectionState::recovering; }));
    PHPVPN_CHECK_EQ(fixture.driver.ends.load(), 1);
    PHPVPN_CHECK_EQ(fixture.driver.closes.load(), 1);
    PHPVPN_CHECK_EQ(fixture.driver.activity.stopped.load(), 1);
    PHPVPN_CHECK_EQ(fixture.driver.activity.destroyed.load(), 1);
    PHPVPN_CHECK_EQ(VpnEngineTestAccess::tunnelRoles(fixture.engine), 0u);

    // Observe real retries, not a copy of backoffDelayMs. The first failure
    // advances the ladder to 2 s, consecutive failures saturate at 60 s.
    // Fake time covers the lower bound and the inclusive +20% jitter bound.
    const std::int64_t bases[] = {2000, 4000, 8000, 16000, 32000, 60000, 60000, 60000};
    std::int64_t failedAt = 0;
    int healthCalls = 1;
    for (const auto base : bases) {
        fixture.clock.now.store(failedAt + base - 1);
        const auto reads = fixture.clock.reads.load();
        // At least two network-loop passes at this time, with no diagnostic
        // reads or wall-clock sleep standing in for the fake deadline.
        PHPVPN_CHECK(eventually([&] { return fixture.clock.reads.load() >= reads + 4; }));
        PHPVPN_CHECK_EQ(fixture.transport.healthCalls.load(), healthCalls);
        PHPVPN_CHECK_EQ(fixture.driver.starts.load(), 1);
        PHPVPN_CHECK_EQ(fixture.driver.activity.destroyed.load(), 1);
        PHPVPN_CHECK_EQ(VpnEngineTestAccess::networkThread(fixture.engine), networkThread);
        PHPVPN_REQUIRE_OK(fixture.engine.connect());
        failedAt += base + base / 5;
        fixture.clock.now.store(failedAt);
        ++healthCalls;
        if (healthCalls < 9) {
            PHPVPN_CHECK(eventually([&] {
                if (fixture.transport.healthCalls.load() != healthCalls) return false;
                const auto status = fixture.engine.status();
                return status.state == ConnectionState::recovering && status.phase == ConnectionPhase::idle;
            }));
        }
    }
    PHPVPN_CHECK(recoveredPoll.await());
    PHPVPN_CHECK_EQ(fixture.engine.status().state, ConnectionState::connected);
    PHPVPN_CHECK_EQ(VpnEngineTestAccess::networkThread(fixture.engine), networkThread);
    PHPVPN_CHECK_EQ(VpnEngineTestAccess::tunnelRoles(fixture.engine), 2u);
    PHPVPN_CHECK_EQ(fixture.driver.creates.load(), 2);
    PHPVPN_CHECK_EQ(fixture.driver.starts.load(), 2);
    PHPVPN_CHECK_EQ(fixture.driver.ends.load(), 1);
    PHPVPN_CHECK(eventually([&] {
        return fixture.driver.activity.readers.load() == 2 && fixture.driver.activity.writers.load() == 2;
    }));
    PHPVPN_CHECK(fixture.driver.activity.distinctThreads.load());
    PHPVPN_CHECK_EQ(fixture.transport.healthCalls.load(), 9);
    PHPVPN_CHECK_EQ(fixture.transport.infoCalls.load(), 3);
    fixture.engine.shutdown();
    PHPVPN_CHECK_EQ(VpnEngineTestAccess::networkThread(fixture.engine), std::thread::id{});
    PHPVPN_CHECK_EQ(VpnEngineTestAccess::tunnelRoles(fixture.engine), 0u);
    PHPVPN_CHECK_EQ(fixture.driver.ends.load(), 2);
    PHPVPN_CHECK_EQ(fixture.driver.closes.load(), 2);
    PHPVPN_CHECK_EQ(fixture.driver.activity.destroyed.load(), 2);
    PHPVPN_CHECK(sameNetworkThread.load());
}

PHPVPN_TEST(runtime, transient_error_classification_for_soft_recovery) {
    for (const auto code : {ErrorCode::transport_timeout, ErrorCode::transport_receive_failed,
                            ErrorCode::transport_connect_failed, ErrorCode::transport_dns_failed,
                            ErrorCode::server_unavailable, ErrorCode::server_internal_error,
                            ErrorCode::server_unexpected_status, ErrorCode::response_invalid,
                            ErrorCode::response_mac_invalid, ErrorCode::wire_invalid_json}) {
        PHPVPN_CHECK(errorCodeIsTransient(code));
        PHPVPN_CHECK(errorCodeIsRecoverable(code));
    }
    for (const auto code : {ErrorCode::cancelled, ErrorCode::server_unauthorized, ErrorCode::server_bad_request,
                            ErrorCode::config_missing, ErrorCode::contract_missing, ErrorCode::tunnel_io_failed,
                            ErrorCode::time_not_synchronised, ErrorCode::crypto_failed, ErrorCode::internal}) {
        PHPVPN_CHECK(!errorCodeIsTransient(code));
    }
    PHPVPN_CHECK_EQ(std::string(connectionPhaseName(ConnectionPhase::reconnecting)), std::string("reconnecting"));
}

PHPVPN_TEST(runtime, soft_recovery_keeps_tunnel_and_queue_through_one_transient_failure) {
    Gate failedPoll, retriedPoll;
    std::mutex requestMutex;
    std::string retriedRequest;
    EngineFixture fixture;
    fixture.transport.incoming = {packet(100, true)};
    fixture.transport.hook = [&](HttpAction action, std::string_view request, int call) -> std::optional<HttpExchangeResult> {
        if (action == HttpAction::data && call == 2) {
            failedPoll.block();
            // nginx in front of a stalled PHP backend: one 502 with an HTML body.
            return HttpExchangeResult{HttpResponse{502, std::string("<html>502 Bad Gateway</html>"), "text/html"}, 29};
        }
        if (action == HttpAction::data && call == 3) {
            { std::lock_guard lock(requestMutex); retriedRequest = std::string(request); }
            retriedPoll.block();
        }
        return {};
    };
    fixture.transport.cancel = [&] { failedPoll.release(); retriedPoll.release(); };
    PHPVPN_REQUIRE_OK(fixture.engine.start());
    PHPVPN_REQUIRE_OK(fixture.engine.connect());
    PHPVPN_CHECK(failedPoll.await());
    PHPVPN_CHECK(eventually([&] {
        return fixture.driver.activity.readers.load() == 1 && fixture.driver.activity.writers.load() == 1;
    }));
    // A packet admitted while the failing exchange is in flight must survive
    // the failure inside the outbound queue and leave with the retry.
    VpnEngineTestAccess::enqueue(fixture.engine, packet(100, false));
    failedPoll.release();
    PHPVPN_CHECK(eventually([&] { return fixture.engine.status().phase == ConnectionPhase::reconnecting; }));
    const auto during = fixture.engine.status();
    PHPVPN_CHECK_EQ(during.state, ConnectionState::connected);
    PHPVPN_CHECK_EQ(during.tunnelReady, Tristate::yes);
    PHPVPN_CHECK(!during.lastError.has_value());
    PHPVPN_CHECK_EQ(during.recoveryCount, std::uint64_t(0));
    PHPVPN_CHECK(during.connectionStartedAtUtc.has_value());
    PHPVPN_CHECK(retriedPoll.await());
    // The retry happened on the same tunnel session: nothing was released.
    PHPVPN_CHECK_EQ(VpnEngineTestAccess::tunnelRoles(fixture.engine), 2u);
    PHPVPN_CHECK_EQ(fixture.driver.ends.load(), 0);
    PHPVPN_CHECK_EQ(fixture.driver.closes.load(), 0);
    PHPVPN_CHECK_EQ(fixture.driver.activity.stopped.load(), 0);
    PHPVPN_CHECK_EQ(fixture.driver.activity.destroyed.load(), 0);
    PHPVPN_CHECK_EQ(fixture.driver.starts.load(), 1);
    PHPVPN_CHECK_EQ(fixture.transport.healthCalls.load(), 1);
    {
        std::lock_guard lock(requestMutex);
        PHPVPN_CHECK(retriedRequest.find("\"dst\":\"10.99.99.3\"") != std::string::npos);
    }
    retriedPoll.release();
    PHPVPN_CHECK(eventually([&] {
        const auto status = fixture.engine.status();
        return status.state == ConnectionState::connected && status.phase == ConnectionPhase::running;
    }));
    const auto diagnostics = fixture.engine.diagnostics();
    PHPVPN_CHECK_EQ(diagnostics.softRecoveryCount, std::uint64_t(1));
    PHPVPN_CHECK_EQ(diagnostics.recoveryCount, std::uint64_t(0));
    PHPVPN_CHECK(!diagnostics.lastSoftRecoveryAtUtc.empty());
    PHPVPN_CHECK(diagnostics.lastRecoveryAtUtc.empty());
    PHPVPN_CHECK(!diagnostics.lastError.has_value());
    PHPVPN_CHECK_EQ(diagnostics.responsesRejected, std::uint64_t(1));
    PHPVPN_CHECK_EQ(fixture.engine.status().connectionStartedAtUtc, during.connectionStartedAtUtc);
    fixture.engine.shutdown();
    PHPVPN_CHECK_EQ(fixture.driver.ends.load(), 1);
}

PHPVPN_TEST(runtime, soft_recovery_401_refreshes_verified_time_once_without_teardown) {
    Gate retriedPoll;
    EngineFixture fixture;
    fixture.transport.hook = [&](HttpAction action, std::string_view, int call) -> std::optional<HttpExchangeResult> {
        if (action == HttpAction::data && call == 2)
            return HttpExchangeResult{HttpResponse{401, std::string("{\"type\":\"error\",\"code\":\"unauthorized\"}"), "application/json"}, 40};
        if (action == HttpAction::data && call == 3) retriedPoll.block();
        return {};
    };
    fixture.transport.cancel = [&] { retriedPoll.release(); };
    PHPVPN_REQUIRE_OK(fixture.engine.start());
    PHPVPN_REQUIRE_OK(fixture.engine.connect());
    PHPVPN_CHECK(retriedPoll.await());
    // The 401 was answered by one fresh verified health, not by a bootstrap.
    PHPVPN_CHECK_EQ(fixture.transport.healthCalls.load(), 2);
    PHPVPN_CHECK_EQ(fixture.transport.infoCalls.load(), 2);
    PHPVPN_CHECK_EQ(fixture.driver.ends.load(), 0);
    PHPVPN_CHECK_EQ(VpnEngineTestAccess::tunnelRoles(fixture.engine), 2u);
    PHPVPN_CHECK_EQ(fixture.engine.status().phase, ConnectionPhase::reconnecting);
    retriedPoll.release();
    PHPVPN_CHECK(eventually([&] { return fixture.engine.status().phase == ConnectionPhase::running; }));
    PHPVPN_CHECK_EQ(fixture.engine.status().state, ConnectionState::connected);
    PHPVPN_CHECK_EQ(fixture.engine.diagnostics().softRecoveryCount, std::uint64_t(1));
    PHPVPN_CHECK_EQ(fixture.engine.diagnostics().recoveryCount, std::uint64_t(0));
}

PHPVPN_TEST(runtime, soft_recovery_repeated_401_after_refresh_takes_the_full_path) {
    EngineFixture fixture;
    fixture.transport.hook = [&](HttpAction action, std::string_view, int call) -> std::optional<HttpExchangeResult> {
        if (action == HttpAction::data && (call == 2 || call == 3))
            return HttpExchangeResult{HttpResponse{401, std::string("{\"type\":\"error\",\"code\":\"unauthorized\"}"), "application/json"}, 40};
        return {};
    };
    PHPVPN_REQUIRE_OK(fixture.engine.start());
    PHPVPN_REQUIRE_OK(fixture.engine.connect());
    // A 401 while running is not recoverable: after the single time refresh
    // the second 401 releases the tunnel and reports the error.
    PHPVPN_CHECK(eventually([&] { return fixture.engine.status().state == ConnectionState::error; }));
    const auto status = fixture.engine.status();
    PHPVPN_CHECK_EQ(status.phase, ConnectionPhase::idle);
    PHPVPN_CHECK(status.lastError.has_value());
    PHPVPN_CHECK_EQ(status.lastError->code, std::string("communication_failed"));
    PHPVPN_CHECK(!status.lastError->recoverable);
    PHPVPN_CHECK_EQ(fixture.transport.healthCalls.load(), 2);
    PHPVPN_CHECK_EQ(fixture.transport.dataCalls.load(), 3);
    PHPVPN_CHECK_EQ(fixture.driver.ends.load(), 1);
    PHPVPN_CHECK_EQ(fixture.driver.activity.stopped.load(), 1);
    PHPVPN_CHECK_EQ(fixture.engine.diagnostics().softRecoveryCount, std::uint64_t(1));
}

PHPVPN_TEST(runtime, soft_recovery_budget_exhausted_falls_back_to_full_recovery) {
    std::atomic<bool> failing{true};
    EngineFixture fixture;
    fixture.transport.hook = [&](HttpAction action, std::string_view, int call) -> std::optional<HttpExchangeResult> {
        if (action == HttpAction::data && call >= 2 && failing.load())
            return HttpExchangeResult{Error(ErrorCode::transport_timeout), 0};
        return {};
    };
    VpnEngineTestAccess::softRecoveryGrace(fixture.engine, 3000);
    PHPVPN_REQUIRE_OK(fixture.engine.start());
    PHPVPN_REQUIRE_OK(fixture.engine.connect());
    PHPVPN_CHECK(eventually([&] { return fixture.engine.status().phase == ConnectionPhase::reconnecting; }));
    // Several retries inside the budget keep the tunnel and publish no error.
    const auto calls = fixture.transport.dataCalls.load();
    PHPVPN_CHECK(eventually([&] { return fixture.transport.dataCalls.load() >= calls + 2; }));
    PHPVPN_CHECK_EQ(fixture.engine.status().state, ConnectionState::connected);
    PHPVPN_CHECK(!fixture.engine.status().lastError.has_value());
    PHPVPN_CHECK_EQ(fixture.driver.ends.load(), 0);
    PHPVPN_CHECK_EQ(fixture.driver.activity.stopped.load(), 0);
    // Beyond the budget the next failure takes the full teardown and backoff.
    fixture.clock.now += 3000;
    PHPVPN_CHECK(eventually([&] { return fixture.engine.status().state == ConnectionState::recovering; }));
    const auto failed = fixture.engine.status();
    PHPVPN_CHECK_EQ(failed.phase, ConnectionPhase::idle);
    PHPVPN_CHECK(failed.lastError.has_value());
    PHPVPN_CHECK_EQ(failed.lastError->code, std::string("communication_timeout"));
    PHPVPN_CHECK(failed.lastError->recoverable);
    PHPVPN_CHECK_EQ(failed.recoveryCount, std::uint64_t(1));
    PHPVPN_CHECK_EQ(fixture.driver.ends.load(), 1);
    PHPVPN_CHECK_EQ(fixture.driver.activity.stopped.load(), 1);
    PHPVPN_CHECK_EQ(fixture.engine.diagnostics().softRecoveryCount, std::uint64_t(1));
    // The full recovery reconnects once the server answers again.
    failing.store(false);
    fixture.clock.now += 10000;
    PHPVPN_CHECK(eventually([&] {
        const auto status = fixture.engine.status();
        return status.state == ConnectionState::connected && status.phase == ConnectionPhase::running;
    }));
    PHPVPN_CHECK_EQ(fixture.driver.starts.load(), 2);
    PHPVPN_CHECK_EQ(fixture.engine.diagnostics().softRecoveryCount, std::uint64_t(1));
    PHPVPN_CHECK_EQ(fixture.engine.diagnostics().recoveryCount, std::uint64_t(1));
}

PHPVPN_TEST(runtime, soft_recovery_honours_disconnect_and_stop_during_backoff) {
    std::atomic<bool> failing{false};
    EngineFixture fixture;
    fixture.transport.hook = [&](HttpAction action, std::string_view, int) -> std::optional<HttpExchangeResult> {
        if (action == HttpAction::data && failing.load())
            return HttpExchangeResult{Error(ErrorCode::transport_receive_failed), 0};
        return {};
    };
    PHPVPN_REQUIRE_OK(fixture.engine.start());
    PHPVPN_REQUIRE_OK(fixture.engine.connect());
    PHPVPN_CHECK(eventually([&] { return fixture.engine.status().phase == ConnectionPhase::running; }));
    failing.store(true);
    PHPVPN_CHECK(eventually([&] { return fixture.engine.status().phase == ConnectionPhase::reconnecting; }));
    PHPVPN_REQUIRE_OK(fixture.engine.disconnect());
    PHPVPN_CHECK(eventually([&] { return fixture.engine.status().state == ConnectionState::disconnected; }));
    PHPVPN_CHECK_EQ(fixture.engine.status().phase, ConnectionPhase::idle);
    PHPVPN_CHECK(!fixture.engine.status().lastError.has_value());
    PHPVPN_CHECK_EQ(fixture.driver.ends.load(), 1);
    PHPVPN_CHECK_EQ(VpnEngineTestAccess::tunnelRoles(fixture.engine), 0u);
    failing.store(false);
    PHPVPN_REQUIRE_OK(fixture.engine.connect());
    PHPVPN_CHECK(eventually([&] { return fixture.engine.status().phase == ConnectionPhase::running; }));
    PHPVPN_CHECK_EQ(fixture.engine.diagnostics().softRecoveryCount, std::uint64_t(1));
    failing.store(true);
    PHPVPN_CHECK(eventually([&] { return fixture.engine.status().phase == ConnectionPhase::reconnecting; }));
    PHPVPN_CHECK_EQ(fixture.engine.diagnostics().softRecoveryCount, std::uint64_t(2));
    fixture.engine.shutdown();
    PHPVPN_CHECK_EQ(fixture.engine.status().state, ConnectionState::disconnected);
    PHPVPN_CHECK_EQ(fixture.driver.ends.load(), 2);
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
        {HttpAction::health, ErrorCode::none, 200, false, false, "endpoint_unavailable_or_invalid", f,p,p,false},
        {HttpAction::health, ErrorCode::none, 0, true, false, "server_verification_failed", s,f,p,false},
        {HttpAction::info, ErrorCode::transport_timeout, 0, false, false, "communication_timeout", s,f,p,true},
        {HttpAction::info, ErrorCode::none, 500, false, false, "communication_failed", s,f,p,false},
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
            // The explicitly requested local HTTP object is the sole raw-data
            // exception. Public status fields and get-diagnostics stay safe.
            auto publicStatus = status;
            publicStatus.httpDiagnostic.reset();
            for (const auto& output : {
                    IpcProtocol::buildStatusResponse(kMessageId, publicStatus),
                    IpcProtocol::buildDiagnosticsResponse(kMessageId, fixture.engine.diagnostics())}) {
                for (const auto* forbidden : {"private endpoint details", "runtime.invalid", "secret-path",
                                              "runtime-fixture-secret", "010203040506", "212223242526"})
                    PHPVPN_CHECK(output.find(forbidden) == std::string::npos);
            }
        }
    }
}

PHPVPN_TEST(runtime, connect_http_diagnostic_phase_scope_and_cleanup) {
    for (int mode = 0; mode < 8; ++mode) {
        EngineFixture f;
        f.transport.badHealthTag = mode == 2;
        f.transport.badInfo = mode == 1;
        f.transport.suppressDiagnostic = mode == 7;
        f.transport.hook = [mode](HttpAction action, std::string_view, int) -> std::optional<HttpExchangeResult> {
            if ((mode == 0 || mode == 7) && action == HttpAction::health) return HttpExchangeResult{HttpResponse{200, "invalid-health", "application/json"}, 14};
            if (mode == 3 && action == HttpAction::health) return HttpExchangeResult{Error(ErrorCode::transport_dns_failed), 0};
            if (mode == 4 && action == HttpAction::health) return HttpExchangeResult{Error(ErrorCode::transport_timeout), 0};
            if (mode == 5 && action == HttpAction::data) return HttpExchangeResult{HttpResponse{401, "bad"}, 3};
            return {};
        };
        if (mode == 6) f.driver.failure = ErrorCode::tunnel_library_missing;
        PHPVPN_REQUIRE_OK(f.engine.start());
        PHPVPN_REQUIRE_OK(f.engine.connect());
        PHPVPN_CHECK(eventually([&] { auto s = f.engine.status(); return s.state == ConnectionState::error || s.state == ConnectionState::recovering; }));
        auto s = f.engine.status();
        PHPVPN_CHECK_EQ(s.httpDiagnostic.has_value(), mode <= 2);
        const auto capturedCalls = f.transport.diagnosticCalls.load();
        const auto healthCalls = f.transport.healthCalls.load();
        const auto infoCalls = f.transport.infoCalls.load();
        f.clock.now.store(100000);
        if (mode <= 2 || mode == 7) {
            PHPVPN_CHECK_EQ(s.state, ConnectionState::error);
            PHPVPN_CHECK(!s.lastError->recoverable);
            std::this_thread::sleep_for(std::chrono::milliseconds(350));
            PHPVPN_CHECK_EQ(f.transport.healthCalls.load(), healthCalls);
            PHPVPN_CHECK_EQ(f.transport.infoCalls.load(), infoCalls);
            PHPVPN_CHECK_EQ(f.engine.status().httpDiagnostic.has_value(), mode <= 2);
        } else if (mode == 3 || mode == 4) {
            PHPVPN_CHECK_EQ(s.state, ConnectionState::recovering);
            PHPVPN_CHECK(eventually([&] { return f.transport.healthCalls.load() > healthCalls; }));
            PHPVPN_CHECK_EQ(f.transport.diagnosticCalls.load(), capturedCalls);
            PHPVPN_CHECK(!f.engine.status().httpDiagnostic);
        }
        if (s.httpDiagnostic) {
            PHPVPN_CHECK_EQ(s.httpDiagnostic->action, mode == 1 ? HttpAction::info : HttpAction::health);
            auto wire = IpcProtocol::buildStatusResponse(kMessageId, s);
            const auto parsed = tray::parseStatusResponse(wire);
            PHPVPN_CHECK(parsed.valid && parsed.httpDiagnostic);
            f.engine.dismissConnectDiagnostic(s.connectionEpoch + 1, *s.connectAttemptId);
            PHPVPN_CHECK(f.engine.status().httpDiagnostic);
            f.engine.dismissConnectDiagnostic(s.connectionEpoch, "3f2a7c1e-9b44-4b0e-8d2f-5c6a1e7b9d03");
            PHPVPN_CHECK(f.engine.status().httpDiagnostic);
            f.engine.dismissConnectDiagnostic(s.connectionEpoch, *s.connectAttemptId);
            PHPVPN_CHECK(!f.engine.status().httpDiagnostic);
        }
        PHPVPN_CHECK_EQ(f.transport.diagnosticCalls.load(), mode == 0 || mode == 2 || mode == 3 || mode == 4 || mode == 7 ? 1 : mode == 6 ? 3 : 2);
        PHPVPN_REQUIRE_OK(f.engine.disconnect());
        PHPVPN_CHECK(!f.engine.status().httpDiagnostic);
        PHPVPN_CHECK(eventually([&] { return f.engine.status().state == ConnectionState::disconnected; }));
        f.engine.shutdown();
        PHPVPN_CHECK(!f.engine.status().httpDiagnostic);
    }
}

PHPVPN_TEST(runtime, connect_http_diagnostic_late_cancelled_reply_never_publishes) {
    EngineFixture f;
    Gate response;
    f.transport.hook = [&](HttpAction action, std::string_view, int call) -> std::optional<HttpExchangeResult> {
        if (action == HttpAction::health && call == 1) {
            response.block();
            return HttpExchangeResult{HttpResponse{200, "late invalid", "application/json"}, 12};
        }
        return {};
    };
    f.transport.cancel = [&] { response.release(); };
    PHPVPN_REQUIRE_OK(f.engine.start());
    PHPVPN_REQUIRE_OK(f.engine.connect());
    PHPVPN_CHECK(response.await());
    const auto oldEpoch = f.engine.status().connectionEpoch;
    PHPVPN_REQUIRE_OK(f.engine.disconnect());
    PHPVPN_CHECK(eventually([&] { return f.engine.status().state == ConnectionState::disconnected; }));
    PHPVPN_CHECK(!f.engine.status().httpDiagnostic);
    PHPVPN_REQUIRE_OK(f.engine.connect());
    PHPVPN_CHECK(eventually([&] { return f.engine.status().state == ConnectionState::connected; }));
    PHPVPN_CHECK(f.engine.status().connectionEpoch > oldEpoch);
    PHPVPN_CHECK(!f.engine.status().httpDiagnostic);
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
    PHPVPN_CHECK(before.connectionStartedAtUtc.has_value());
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
    PHPVPN_CHECK_EQ(failed.connectionStartedAtUtc, before.connectionStartedAtUtc);
    PHPVPN_CHECK(allChecks(failed, CheckpointResult::success, CheckpointResult::success, CheckpointResult::success));
    PHPVPN_CHECK_EQ(failed.lastError->code, std::string("communication_failed"));
    PHPVPN_CHECK_EQ(failed.counters.serializedDataResponseBodyBytes, before.counters.serializedDataResponseBodyBytes + 73);
    fixture.clock.now += 10000;
    PHPVPN_CHECK(eventually([&] { return fixture.engine.status().state == ConnectionState::connected; }));
    PHPVPN_CHECK_EQ(fixture.engine.status().connectionStartedAtUtc, before.connectionStartedAtUtc);
    PHPVPN_REQUIRE_OK(fixture.engine.disconnect());
    PHPVPN_CHECK(eventually([&] { return fixture.engine.status().state == ConnectionState::disconnected; }));
    PHPVPN_CHECK_EQ(fixture.engine.status().counters.serializedDataResponseBodyBytes, std::uint64_t(0));
    PHPVPN_CHECK_EQ(fixture.engine.diagnostics().counters.dataPollsTotal, std::uint64_t(0));
    PHPVPN_CHECK(!fixture.engine.status().connectionStartedAtUtc);
    PHPVPN_REQUIRE_OK(fixture.engine.connect());
    PHPVPN_CHECK(eventually([&] { return fixture.engine.status().state == ConnectionState::connected; }));
    PHPVPN_CHECK(fixture.engine.status().connectionStartedAtUtc != before.connectionStartedAtUtc);
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
    PHPVPN_CHECK(!blockedConnect && blockedConnect.error().code == blockedStart.error().code);
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
PHPVPN_TEST(wizard_service, fix2_additive_finish_preserves_existing_keys_and_binds_all_metadata) {
    for (bool repair : {false, true}) {
        WizardServiceFixture fixture;
        if (repair) {
            auto dom = Json::parse(*fixture.runtime.files->bytes).value();
            for (auto& [name, value] : dom.objectValue()) if (name == "masterSetup")
                std::erase_if(value.objectValue(), [](const auto& item) { return item.first == "virtualIp"; });
            fixture.runtime.files->bytes = Json::serialize(dom);
            auto view = WizardIpc::parseView(fixture.call(WizardCommand::open).result).value();
            PHPVPN_CHECK_EQ(view.mode, WizardMode::repair);
            fixture.request.sessionId = view.sessionId;
            fixture.request.fields = *view.fields;
            fixture.request.fields.virtualIp = "10.99.99.2";
        }
        const auto original = analyzeWizardDocument(*fixture.runtime.files->bytes).value();
        fixture.request.fields.autoDetectIpAddressInWizard = true;
        fixture.allChecks();
        fixture.request.commitId = fixture.runtime.crypto.actual->randomUuidV4().value();
        fixture.request.createDefaultContracts = true;
        auto started = fixture.call(WizardCommand::finish);
        PHPVPN_CHECK(!started.error);
        fixture.runFile();
        auto receipt = fixture.call(WizardCommand::get_commit);
        PHPVPN_CHECK_EQ(receipt.result.find("commitState")->stringValue(), std::string("confirmed"));
        auto stored = analyzeWizardDocument(*fixture.runtime.files->bytes).value();
        PHPVPN_CHECK_EQ(stored.contractPeers.size(), 13u);
        PHPVPN_CHECK(stored.fields.autoDetectIpAddressInWizard);
        PHPVPN_CHECK_EQ(Json::serialize(*stored.dom.find("communicationContracts")->find("10.99.99.3")),
            Json::serialize(*original.dom.find("communicationContracts")->find("10.99.99.3")));
        const auto writes = fixture.runtime.files->writes;
        PHPVPN_CHECK(!fixture.call(WizardCommand::finish).error);
        PHPVPN_CHECK_EQ(fixture.runtime.files->writes, writes);
        fixture.request.fields.autoDetectIpAddressInWizard = false;
        auto changed = fixture.call(WizardCommand::finish);
        PHPVPN_CHECK(changed.error && changed.error->code == "invalid_argument");
    }
}
PHPVPN_TEST(wizard_service, fix2_rejects_false_default_claims_and_unverified_automatic_ip_before_http) {
    WizardServiceFixture fixture;
    fixture.check(WizardCommand::check_url);
    fixture.check(WizardCommand::check_secret);
    const auto original = fixture.request.fields;
    const auto httpCalls = fixture.httpCalls;
    for (unsigned bad = 0; bad < 3; ++bad) {
        fixture.request.fields = original;
        if (bad == 0) fixture.request.fields.useDefaultServerSecretInWizard = true;
        if (bad == 1) fixture.request.fields.useDefaultAccessKeyInWizard = true;
        if (bad == 2) { fixture.request.fields.autoDetectIpAddressInWizard = true; fixture.request.fields.virtualIp = "10.99.99.10"; }
        fixture.request.attemptSequence = ++fixture.sequence;
        fixture.request.clientAttemptId = fixture.runtime.crypto.actual->randomUuidV4().value();
        auto rejected = fixture.call(WizardCommand::check_access);
        PHPVPN_CHECK(rejected.error && rejected.error->code == "wizard_prerequisite_failed");
        PHPVPN_CHECK_EQ(fixture.httpCalls, httpCalls);
        PHPVPN_CHECK(!fixture.diagnosticTask);
    }
    fixture.request.fields = original;
    fixture.check(WizardCommand::check_access);
    fixture.request.fields.autoDetectIpAddressInWizard = true;
    auto unproven = fixture.beginFinish();
    PHPVPN_CHECK(unproven.error && unproven.error->code == "wizard_prerequisite_failed");
    PHPVPN_CHECK_EQ(fixture.runtime.files->writes, 0u);
}
PHPVPN_TEST(runtime, fix2_degraded_storage_keeps_diagnostics_and_blocks_connect_with_original_error) {
    EngineFixture fixture;
    fixture.files->recoveryError = ErrorCode::journal_corrupt;
    PHPVPN_CHECK(!fixture.store.recover());
    const auto bytes = fixture.files->bytes;
    auto start = fixture.engine.start();
    PHPVPN_CHECK(!start && start.error().code == ErrorCode::journal_corrupt);
    auto connect = fixture.engine.connect();
    PHPVPN_CHECK(!connect && connect.error().code == ErrorCode::journal_corrupt);
    PHPVPN_CHECK_EQ(fixture.engine.status().state, ConnectionState::error);
    PHPVPN_CHECK_EQ(fixture.files->bytes, bytes);
    PHPVPN_CHECK_EQ(fixture.files->writes, 0u);
    PHPVPN_CHECK_EQ(fixture.driver.loads.load(), 0);
    PHPVPN_CHECK_EQ(fixture.transport.dataCalls.load(), 0);
    PHPVPN_REQUIRE_OK(fixture.engine.disconnect());
    IpcDispatcher dispatcher(fixture.engine);
    const auto requestId = fixture.crypto.actual->randomUuidV4().value();
    for (auto command : {IpcCommand::get_status, IpcCommand::get_diagnostics, IpcCommand::disconnect}) {
        const auto reply = Json::parse(dispatcher.handle(IpcProtocol::buildRequest(command, requestId)));
        PHPVPN_REQUIRE_OK(reply);
        PHPVPN_CHECK(!reply->find("error"));
    }
    const auto rejected = Json::parse(dispatcher.handle(IpcProtocol::buildRequest(IpcCommand::connect, requestId)));
    PHPVPN_REQUIRE_OK(rejected);
    PHPVPN_CHECK_EQ(rejected->find("error")->find("code")->stringValue(), std::string("journal_corrupt"));
}

PHPVPN_TEST(wizard_service, fix2_stored_false_survives_model_suggestion_finish_and_new_open) {
    WizardServiceFixture fixture;
    auto dom = Json::parse(makeDefaultEditorDocument()).value();
    for (auto& section : dom.objectValue()) if (section.first == "masterSetup")
        for (auto& field : section.second.objectValue())
            if (field.first.ends_with("InWizard")) field.second = JsonValue::makeBool(false);
    fixture.runtime.files->bytes = Json::serialize(dom);
    const auto original = *fixture.runtime.files->bytes;
    ConfigWarnings warnings;
    fixture.relayConfig = parseAndValidateConfig(original, warnings).value();
    auto view = WizardIpc::parseView(fixture.call(WizardCommand::open).result).value();
    fixture.request.sessionId = view.sessionId;
    WizardModel model;
    model.load(view);
    const auto expected = model.fields();
    PHPVPN_CHECK(!model.autoDetectIpAddress());
    PHPVPN_CHECK(!model.useDefaultSecret());
    PHPVPN_CHECK(!model.useDefaultAccessKey());
    for (auto command : {WizardCommand::check_url, WizardCommand::check_secret, WizardCommand::check_access}) {
        auto attempt = model.beginCheck(fixture.runtime.crypto.actual->randomUuidV4().value(), 1).value();
        fixture.request.fields = model.fields();
        fixture.request.clientAttemptId = attempt.clientAttemptId;
        fixture.request.attemptSequence = attempt.sequence;
        auto started = fixture.call(command);
        PHPVPN_CHECK(!started.error);
        fixture.request.operationId = started.result.find("operationId")->stringValue();
        PHPVPN_CHECK(model.acceptStart(fixture.request.operationId, attempt.clientAttemptId, attempt.sequence));
        fixture.runDiagnostic();
        auto result = WizardIpc::parseOperation(fixture.call(WizardCommand::get_operation).result).value();
        PHPVPN_CHECK_EQ(result.state, WizardOperationState::succeeded);
        if (command == WizardCommand::check_secret) {
            PHPVPN_CHECK(result.suggestedIp.has_value());
            // Inject a different proposal at the presentation seam so a mistaken
            // automatic overwrite cannot pass merely because both IPs are .2.
            result.suggestedIp = "10.99.99.10";
        }
        PHPVPN_CHECK(model.applyOperation(result, 2));
        PHPVPN_CHECK_EQ(model.fields(), expected);
    }
    PHPVPN_CHECK(model.hasSuggestedIp());
    PHPVPN_CHECK_EQ(model.step(), WizardStep::contracts);
    PHPVPN_CHECK_EQ(*fixture.runtime.files->bytes, original);
    PHPVPN_CHECK_EQ(fixture.runtime.files->writes, 0u);
    fixture.request.fields = model.fields();
    PHPVPN_CHECK(!fixture.beginFinish().error);
    fixture.runFile();
    const auto receipt = fixture.call(WizardCommand::get_commit);
    PHPVPN_CHECK_EQ(receipt.result.find("commitState")->stringValue(), std::string("confirmed"));
    PHPVPN_CHECK_EQ(fixture.runtime.files->writes, 1u);
    const auto stored = analyzeWizardDocument(*fixture.runtime.files->bytes).value();
    PHPVPN_CHECK_EQ(stored.fields, expected);
    PHPVPN_CHECK_EQ(Json::serialize(*stored.dom.find("communicationContracts")), Json::serialize(*dom.find("communicationContracts")));
    PHPVPN_CHECK(!fixture.call(WizardCommand::close).error);
    const auto httpCalls = fixture.httpCalls;
    auto reopened = WizardIpc::parseView(fixture.call(WizardCommand::open).result).value();
    PHPVPN_CHECK(reopened.sessionId != view.sessionId);
    WizardModel newModel;
    newModel.load(reopened);
    PHPVPN_CHECK_EQ(newModel.fields(), expected);
    PHPVPN_CHECK(!newModel.hasSuggestedIp());
    PHPVPN_CHECK_EQ(fixture.httpCalls, httpCalls);
    PHPVPN_CHECK_EQ(fixture.runtime.files->writes, 1u);
}

PHPVPN_TEST(wizard_service, fix2_all_preferences_persist_through_verified_finish_and_reopen) {
    for (unsigned mask = 0; mask < 8; ++mask) {
        WizardServiceFixture fixture;
        fixture.runtime.files->bytes = makeDefaultEditorDocument();
        ConfigWarnings warnings;
        fixture.relayConfig = parseAndValidateConfig(*fixture.runtime.files->bytes, warnings).value();
        const auto view = WizardIpc::parseView(fixture.call(WizardCommand::open).result).value();
        fixture.request.sessionId = view.sessionId;
        fixture.request.fields = *view.fields;
        fixture.request.fields.useDefaultServerSecretInWizard = (mask & 1) != 0;
        fixture.request.fields.autoDetectIpAddressInWizard = (mask & 2) != 0;
        fixture.request.fields.useDefaultAccessKeyInWizard = (mask & 4) != 0;
        const auto expected = fixture.request.fields;
        fixture.allChecks();
        PHPVPN_CHECK(!fixture.beginFinish().error);
        fixture.runFile();
        const auto receipt = fixture.call(WizardCommand::get_commit);
        PHPVPN_CHECK_EQ(receipt.result.find("commitState")->stringValue(), std::string("confirmed"));
        for (unsigned flag = 0; flag < 3; ++flag) {
            fixture.request.fields = expected;
            if (flag == 0) fixture.request.fields.useDefaultServerSecretInWizard = !expected.useDefaultServerSecretInWizard;
            if (flag == 1) fixture.request.fields.autoDetectIpAddressInWizard = !expected.autoDetectIpAddressInWizard;
            if (flag == 2) fixture.request.fields.useDefaultAccessKeyInWizard = !expected.useDefaultAccessKeyInWizard;
            auto changed = fixture.call(WizardCommand::finish);
            PHPVPN_CHECK(changed.error && changed.error->code == "invalid_argument");
        }
        PHPVPN_CHECK_EQ(fixture.runtime.files->writes, 1u);
        auto reopened = WizardIpc::parseView(fixture.call(WizardCommand::open).result);
        PHPVPN_REQUIRE_OK(reopened);
        PHPVPN_CHECK_EQ(*reopened->fields, expected);
        PHPVPN_CHECK_EQ(reopened->contractCount, 0u);
    }
}
}  // namespace phpvpn

namespace phpvpn {
PHPVPN_TEST(runtime_http_headers, worker_three_actions_authenticated_time_refresh_and_reconfigure) {
    ObservedCrypto crypto;
    ScriptedTransport transport(*crypto.actual);
    TestClock clock;
    NetworkWorker worker(crypto, transport, clock);
    auto dom = Json::parse(kConfig).value();
    const std::string custom = "X-Runtime: own\nX-Trace: second";
    for (auto& section : dom.objectValue()) if (section.first == "masterSetup")
        for (auto& item : section.second.objectValue()) if (item.first == "httpHeaders") item.second = JsonValue::makeString(custom);
    ConfigWarnings warnings;
    auto config = parseAndValidateConfig(Json::serialize(dom), warnings).value();
    PHPVPN_REQUIRE_OK(worker.configure(config));
    PHPVPN_REQUIRE_OK(worker.performHealth());
    PHPVPN_CHECK(worker.timeSync().hasSnapshot());
    PHPVPN_CHECK_EQ(worker.timeSync().snapshot()->authenticatedServerTimestampMs, *Iso8601::parse(kTimestamp));
    clock.now = 2500;
    PHPVPN_REQUIRE_OK(worker.performInfo());
    DropCounters inDrops, outDrops;
    test::FixedFlowHasher flowHasher;
    DirectionQueues incoming(10000, 0, 1, clock, flowHasher, inDrops), outgoing(10000, 0, 1, clock, flowHasher, outDrops);
    PHPVPN_REQUIRE_OK(worker.verifyAccessKey(incoming, inDrops));
    PHPVPN_REQUIRE_OK(worker.exchangeData(outgoing, incoming, outDrops, inDrops, true));
    const auto observations = transport.observedHeaders();
    PHPVPN_CHECK_EQ(observations.size(), 4u);
    for (const auto& request : observations) {
        PHPVPN_CHECK_EQ(request.headers, custom);
        if (request.action != HttpAction::health) {
            const auto auth = Json::parse(request.request.substr(0, request.request.find('\n'))).value();
            PHPVPN_CHECK_EQ(*Iso8601::parse(auth.find("timestamp")->stringValue()), *Iso8601::parse(kTimestamp) + 1500);
        }
    }
    clock.now = 300000;
    PHPVPN_REQUIRE_OK(worker.performHealth());
    PHPVPN_CHECK_EQ(worker.timeSync().snapshot()->synchronisedAtMonoMs, 300000);
    transport.badHealthTag = true;
    clock.now = 310000;
    PHPVPN_CHECK(!worker.performHealth());
    PHPVPN_CHECK_EQ(worker.timeSync().snapshot()->synchronisedAtMonoMs, 300000);
    PHPVPN_REQUIRE_OK(worker.configure(fixtureConfig()));
    PHPVPN_CHECK(!worker.timeSync().hasSnapshot());
    PHPVPN_CHECK(!worker.performHealth());
    PHPVPN_CHECK(!worker.timeSync().hasSnapshot());
    transport.badHealthTag = false;
    PHPVPN_REQUIRE_OK(worker.performHealth());
    const auto final = transport.observedHeaders();
    PHPVPN_CHECK_EQ(final.back().headers, "X-Test: fixture");
}
PHPVPN_TEST(probe_http_headers, all_direct_and_service_checks_use_operation_fields) {
    for (bool diagnostics : {false, true}) {
        ObservedCrypto crypto;
        ScriptedTransport transport(*crypto.actual);
        TestClock clock;
        auto document = analyzeWizardDocument(kConfig).value();
        document.fields.httpHeaders = "X-Probe: own\nX-Trace: second";
        ProbeClient probe(crypto, transport, clock);
        probe.captureWizardHttpDiagnostics(diagnostics);
        PHPVPN_REQUIRE_OK(probe.checkUrl(document.context, document.fields));
        PHPVPN_REQUIRE_OK(probe.checkSecret(document.context, document.fields));
        PHPVPN_REQUIRE_OK(probe.checkAccess(document, document.fields));
        const auto observations = transport.observedHeaders();
        PHPVPN_CHECK_EQ(observations.size(), 6u);
        for (const auto& request : observations) PHPVPN_CHECK_EQ(request.headers, document.fields.httpHeaders);
        PHPVPN_CHECK_EQ(document.context.httpHeaders, "X-Test: fixture");
        PHPVPN_CHECK_EQ(observations.back().action, HttpAction::data);
    }
    WizardServiceFixture fixture;
    fixture.allChecks();
    PHPVPN_CHECK_EQ(fixture.probeHeaders.size(), 6u);
    for (const auto& request : fixture.probeHeaders) PHPVPN_CHECK_EQ(request.headers, "X-Test: fixture");
    // An independently saved file cannot replace the opened wizard's context.
    PHPVPN_REQUIRE_OK(fixture.runtime.store.save(makeDefaultEditorDocument(), fixture.request.requestId));
    fixture.check(WizardCommand::check_url);
    PHPVPN_CHECK_EQ(fixture.probeHeaders.back().headers, "X-Test: fixture");
}
}

namespace phpvpn {
PHPVPN_TEST(runtime_http_headers, invalid_save_keeps_active_config_and_new_connect_uses_new_headers) {
    EngineFixture fixture;
    Gate polling;
    fixture.transport.hook = [&](HttpAction action, std::string_view, int call) -> std::optional<HttpExchangeResult> {
        if (action == HttpAction::data && call == 2) polling.block();
        return {};
    };
    fixture.transport.cancel = [&] { polling.release(); };
    PHPVPN_REQUIRE_OK(fixture.engine.start());
    PHPVPN_REQUIRE_OK(fixture.engine.connect());
    PHPVPN_CHECK(polling.await());
    const auto original = *fixture.files->bytes;
    const auto writes = fixture.files->writes;
    auto dom = Json::parse(original).value();
    auto setHeaders = [&](const std::string& headers) {
        for (auto& section : dom.objectValue()) if (section.first == "masterSetup")
            for (auto& item : section.second.objectValue()) if (item.first == "httpHeaders") item.second = JsonValue::makeString(headers);
    };
    setHeaders("X: permitted\nHOST: forbidden");
    PHPVPN_REQUIRE_ERROR(fixture.engine.setConfigJson(Json::serialize(dom)), ErrorCode::config_invalid_value);
    PHPVPN_CHECK_EQ(*fixture.files->bytes, original);
    PHPVPN_CHECK_EQ(fixture.files->writes, writes);
    PHPVPN_CHECK_EQ(fixture.engine.status().state, ConnectionState::connected);
    setHeaders("X-New: next-connection");
    PHPVPN_REQUIRE_OK(fixture.engine.setConfigJson(Json::serialize(dom)));
    for (const auto& request : fixture.transport.observedHeaders()) PHPVPN_CHECK_EQ(request.headers, "X-Test: fixture");
    PHPVPN_REQUIRE_OK(fixture.engine.disconnect());
    PHPVPN_CHECK(eventually([&] { return fixture.engine.status().state == ConnectionState::disconnected; }));
    fixture.transport.hook = {};
    PHPVPN_REQUIRE_OK(fixture.engine.connect());
    PHPVPN_CHECK(eventually([&] { return fixture.engine.status().state == ConnectionState::connected; }));
    PHPVPN_REQUIRE_OK(fixture.engine.disconnect());
    PHPVPN_CHECK(eventually([&] { return fixture.engine.status().state == ConnectionState::disconnected; }));
    const auto observations = fixture.transport.observedHeaders();
    PHPVPN_CHECK_EQ(observations.back().headers, "X-New: next-connection");
}
#include "runtime_info_extensions.inc"
}
namespace phpvpn {
PHPVPN_TEST(flow_integration, production_network_and_tunnel_keep_interactive_flows_ahead_of_continuous_bulk) {
    for(unsigned mode=0;mode<3;++mode) {
        const bool tx=mode!=1,rx=mode!=0;
        ObservedCrypto crypto;ScriptedTransport transport(*crypto.actual);TestClock clock;
        NetworkWorker worker(crypto,transport,clock);TrafficCounters counters;
        PHPVPN_REQUIRE_OK(worker.configure(fixtureConfig(),&counters));
        PHPVPN_REQUIRE_OK(worker.performHealth());
        PHPVPN_REQUIRE_OK(VpnEngineTestAccess::requestLimits(worker,4000));
        test::FixedFlowHasher hasher;DropCounters outDrops,inDrops;
        DirectionQueues outbound(1000000,0,1,clock,hasher,outDrops,5000,1000);
        DirectionQueues inbound(1000000,0,1,clock,hasher,inDrops,5000,1000);
        const auto peer=*Ipv4Address::parse("10.99.99.3");DropReason reason{};
        if(tx) {
            for(unsigned i=0;i<100;++i) PHPVPN_CHECK(outbound.tryEnqueue(peer,test::flowPacket(1000,1),reason).valueOr(false));
            PHPVPN_CHECK(outbound.tryEnqueue(peer,test::flowPacket(100,2,42),reason).valueOr(false));
        }
        if(rx) {
            for(unsigned i=0;i<101;++i) {
                auto bytes=test::flowPacket(i==100?100:1000,i==100?std::uint16_t(2):std::uint16_t(1),i==100?std::uint16_t(42):std::uint16_t(0));
                std::swap_ranges(bytes.begin()+12,bytes.begin()+16,bytes.begin()+16);test::checksum(bytes);
                transport.incoming.push_back(std::move(bytes));
            }
        }
        bool sentInteractive=false;
        transport.hook=[&](HttpAction action,std::string_view request,int)->std::optional<HttpExchangeResult> {
            if(action!=HttpAction::data || !tx) return {};
            const auto start=request.find('\n')+1;
            auto record=DataRecord::parse(request.substr(start,request.find('\n',start)-start)).value();
            std::vector<std::uint8_t> iv,tag,ciphertext;
            PHPVPN_CHECK(Base64Url::decode(record.iv,iv));PHPVPN_CHECK(Base64Url::decode(record.tag,tag));PHPVPN_CHECK(Base64Url::decode(record.ciphertext,ciphertext));
            const auto aad=dataAad(record.messageId,record.src,record.dst,record.iv);
            auto opened=crypto.actual->aesGcmOpen(fixtureConfig()->contracts.front().payloadKey.span(),iv,asBytes(aad),ciphertext,tag).value();
            auto packets=PayloadBatch::decodeAndValidate(opened.span(),fixtureConfig()->virtualIp,peer).value();
            PHPVPN_CHECK(request.size()<=4000);
            PHPVPN_CHECK(outbound.statistics().inFlightBytes>0);
            unsigned position=0;
            for(const auto& bytes:packets) {
                if(test::packetId(bytes)==42) { sentInteractive=true;PHPVPN_CHECK(position<=1);PHPVPN_CHECK(outbound.statistics().queuedItemCount>0); }
                else PHPVPN_CHECK(outbound.tryEnqueue(peer,bytes,reason).valueOr(false));
                ++position;
            }
            return {};
        };
        auto exchange=worker.exchangeData(outbound,inbound,outDrops,inDrops,true);PHPVPN_REQUIRE_OK(exchange);
        if(tx) PHPVPN_CHECK(sentInteractive);
        if(rx) {
            PHPVPN_CHECK_EQ(exchange->packetsReceived,std::size_t(101));
            auto tunnel=std::make_shared<test::RecordingTunnel>();TunnelWorkerCounters tunnelCounters;
            std::atomic<bool> continuousBulkValid{true},interactiveWithBacklog{false};
            tunnel->onSend=[&](std::span<const std::uint8_t> bytes) {
                if(bytes[5]==42) interactiveWithBacklog=inbound.statistics().queuedItemCount>0;
                else {
                    DropReason drop{};
                    if(!inbound.tryEnqueue(peer,PacketBytes(bytes.begin(),bytes.end()),drop).valueOr(false)) continuousBulkValid=false;
                }
                return ok();
            };
            TunnelOutputWorker output(tunnel,inbound,inDrops,tunnelCounters);
            output.start();PHPVPN_CHECK(tunnel->awaitCount(3));output.stop();
            const auto delivered=tunnel->packets();
            PHPVPN_CHECK(delivered.size()>=2);
            if(delivered.size()>=2) PHPVPN_CHECK_EQ(test::packetId(delivered[1]),std::uint16_t(42));
            PHPVPN_CHECK(continuousBulkValid.load());PHPVPN_CHECK(interactiveWithBacklog.load());
            PHPVPN_CHECK_EQ(tunnelCounters.writeFailures.load(),std::uint64_t(0));
        }
        PHPVPN_CHECK_EQ(outDrops.total()+inDrops.total(),std::uint64_t(0));
        PHPVPN_CHECK_EQ(outbound.statistics().inFlightBytes,std::int64_t(0));
        PHPVPN_CHECK_EQ(inbound.statistics().inFlightBytes,std::int64_t(0));
    }
}
PHPVPN_TEST(flow_integration, every_request_failure_rolls_back_original_nodes_age_and_order) {
    for(unsigned failure=0;failure<12;++failure) {
        ObservedCrypto crypto;ScriptedTransport transport(*crypto.actual);TestClock clock;
        NetworkWorker worker(crypto,transport,clock);bool cancelled=false;
        PHPVPN_REQUIRE_OK(worker.configure(fixtureConfig(),nullptr,[&]{return cancelled;}));
        PHPVPN_REQUIRE_OK(worker.performHealth());
        test::QueueContext context;DropCounters inDrops;
        DirectionQueues outbound(10000,0,2,context.clock,context.hasher,context.drops);
        DirectionQueues inbound(10000,0,2,context.clock,context.hasher,inDrops);
        const auto peer=*Ipv4Address::parse(failure==11?"10.99.99.4":"10.99.99.3");DropReason reason{};
        for(std::uint16_t id=1;id<=3;++id) PHPVPN_CHECK(outbound.tryEnqueue(peer,test::flowPacket(100,1,id),reason).valueOr(false));
        if(failure<3) crypto.failRandomAt=crypto.randomCalls+failure+1;
        if(failure==3) crypto.failSeal=true;
        if(failure==4) cancelled=true;
        if(failure==5) transport.hook=[](HttpAction,std::string_view,int)->std::optional<HttpExchangeResult>{return HttpExchangeResult(Error(ErrorCode::transport_send_failed));};
        if(failure==6) transport.mutateResponse=[](HttpResponse& response){response.statusCode=503;};
        if(failure==7) transport.badMac=true;
        if(failure==8) transport.defer=true;
        if(failure==9) transport.hook=[](HttpAction,std::string_view,int)->std::optional<HttpExchangeResult>{throw std::runtime_error("injected transport exception");};
        if(failure==10) clock.now=-1;
        bool threw=false;
        try {
            const auto result=worker.exchangeData(outbound,inbound,context.drops,inDrops,true);
            if(failure==8) { PHPVPN_REQUIRE_OK(result);PHPVPN_CHECK_EQ(result->packetsSent,std::size_t(0)); }
            else PHPVPN_CHECK(!result);
        } catch(const std::runtime_error&) {threw=true;}
        PHPVPN_CHECK_EQ(threw,failure==9);
        PHPVPN_CHECK_EQ(outbound.statistics().occupiedBytes,std::int64_t(300));
        PHPVPN_CHECK_EQ(outbound.statistics().inFlightBytes,std::int64_t(0));
        PacketLease returned(outbound,peer);
        for(std::uint16_t id=1;id<=3;++id) {
            PHPVPN_CHECK(outbound.select(returned));
            PHPVPN_CHECK_EQ(test::packetId(returned.packets().back().bytes),id);
            PHPVPN_CHECK_EQ(returned.packets().back().queuedAt,std::uint64_t(0));
            PHPVPN_CHECK_EQ(returned.packets().back().bucket,std::uint8_t(0));
        }
        returned.rollback();context.clock.now=5000;
        PHPVPN_CHECK(!outbound.takePacket(peer));
        PHPVPN_CHECK_EQ(context.drops.totalForReason(DropReason::queue_expired),std::uint64_t(3));
    }
}
PHPVPN_TEST(flow_integration, partial_ack_commits_only_confirmed_peer_record) {
    auto document=Json::parse(kConfig).value();
    for(auto& [name,value]:document.objectValue()) if(name=="communicationContracts")
        value.objectValue().emplace_back("10.99.99.4",value.objectValue().front().second);
    ConfigWarnings warnings;auto config=parseAndValidateConfig(Json::serialize(document),warnings).value();
    ObservedCrypto crypto;ScriptedTransport transport(*crypto.actual,config);TestClock clock;
    NetworkWorker worker(crypto,transport,clock);PHPVPN_REQUIRE_OK(worker.configure(config));PHPVPN_REQUIRE_OK(worker.performHealth());
    test::FixedFlowHasher hasher;DropCounters outDrops,inDrops;
    DirectionQueues outbound(10000,0,2,clock,hasher,outDrops),inbound(10000,0,2,clock,hasher,inDrops);
    DropReason reason{};
    const auto a=*Ipv4Address::parse("10.99.99.3"),b=*Ipv4Address::parse("10.99.99.4");
    PHPVPN_CHECK(outbound.tryEnqueue(a,test::flowPacket(100,1,1),reason).valueOr(false));
    PHPVPN_CHECK(outbound.tryEnqueue(b,test::flowPacket(100,2,2),reason).valueOr(false));
    transport.acceptTarget=[](std::string_view target){return target=="10.99.99.3";};
    const auto result=worker.exchangeData(outbound,inbound,outDrops,inDrops,true);PHPVPN_REQUIRE_OK(result);
    PHPVPN_CHECK_EQ(result->packetsSent,std::size_t(1));PHPVPN_CHECK_EQ(result->deferredRecords,std::size_t(1));
    PHPVPN_CHECK_EQ(outbound.statistics().itemCount,std::size_t(1));PHPVPN_CHECK_EQ(outbound.statistics().inFlightBytes,std::int64_t(0));
    PHPVPN_CHECK(outbound.bucketSnapshot(b)[0].owner.has_value());
    PHPVPN_CHECK(!outbound.takePacket(a));auto retained=outbound.takePacket(b);
    PHPVPN_CHECK(retained && test::packetId(*retained)==2);
}
PHPVPN_TEST(flow_integration, inbound_hash_failure_is_propagated_after_authenticated_ingest) {
    ObservedCrypto crypto;ScriptedTransport transport(*crypto.actual);TestClock clock;
    NetworkWorker worker(crypto,transport,clock);PHPVPN_REQUIRE_OK(worker.configure(fixtureConfig()));PHPVPN_REQUIRE_OK(worker.performHealth());
    test::FixedFlowHasher hasher;hasher.fail=true;DropCounters outDrops,inDrops;
    DirectionQueues outbound(100000,0,1,clock,hasher,outDrops),inbound(100000,0,1,clock,hasher,inDrops);
    for(std::uint16_t flow=1;flow<=17;++flow) {
        auto bytes=test::flowPacket(100,flow);std::swap_ranges(bytes.begin()+12,bytes.begin()+16,bytes.begin()+16);test::checksum(bytes);
        transport.incoming.push_back(std::move(bytes));
    }
    PHPVPN_REQUIRE_ERROR(worker.exchangeData(outbound,inbound,outDrops,inDrops,false),ErrorCode::crypto_failed);
    PHPVPN_CHECK_EQ(inbound.statistics().itemCount,std::size_t(16));
    PHPVPN_CHECK_EQ(inDrops.total(),std::uint64_t(0));
}
} // namespace phpvpn
namespace phpvpn {
PHPVPN_TEST(flow_lifecycle, ttl_changes_only_on_new_connect_and_hash_secret_survives) {
    EngineFixture fixture;Gate data;
    fixture.transport.hook=[&](HttpAction action,std::string_view,int call)->std::optional<HttpExchangeResult>{
        if(action==HttpAction::data && call==2) data.block();return {};
    };
    fixture.transport.cancel=[&]{data.release();};
    PHPVPN_REQUIRE_OK(fixture.engine.start());PHPVPN_REQUIRE_OK(fixture.engine.connect());
    PHPVPN_CHECK(data.await());
    const auto* hasher=VpnEngineTestAccess::flowHasherIdentity(fixture.engine);
    auto document=Json::parse(kConfig).value();
    document.objectValue().emplace_back("runtime",JsonValue::makeObject({{"maxQueuedPacketAgeMs",JsonValue::makeInteger(1234)}}));
    PHPVPN_REQUIRE_OK(fixture.engine.setConfigJson(Json::serialize(document)));
    PHPVPN_CHECK_EQ(VpnEngineTestAccess::queueAge(fixture.engine),std::uint64_t(5000));
    PHPVPN_REQUIRE_OK(fixture.engine.disconnect());
    PHPVPN_CHECK(eventually([&]{return fixture.engine.status().state==ConnectionState::disconnected;}));
    PHPVPN_REQUIRE_OK(fixture.engine.connect());
    PHPVPN_CHECK(eventually([&]{return fixture.engine.status().state==ConnectionState::connected;}));
    PHPVPN_CHECK_EQ(VpnEngineTestAccess::queueAge(fixture.engine),std::uint64_t(1234));
    PHPVPN_CHECK_EQ(VpnEngineTestAccess::flowHasherIdentity(fixture.engine),hasher);
    fixture.engine.shutdown();PHPVPN_REQUIRE_OK(fixture.engine.start());
    PHPVPN_CHECK_EQ(VpnEngineTestAccess::flowHasherIdentity(fixture.engine),hasher);
}
PHPVPN_TEST(probe_http_headers, changed_headers_revoke_service_evidence_and_commit_identity) {
    WizardServiceFixture fixture;
    const auto original = *fixture.runtime.files->bytes;
    fixture.allChecks();
    fixture.request.fields.httpHeaders = "Cookie: synthetic=required\r\nUser-Agent: fixture";
    for (auto command : {WizardCommand::check_secret, WizardCommand::check_access}) {
        fixture.request.clientAttemptId = fixture.runtime.crypto.actual->randomUuidV4().value();
        fixture.request.attemptSequence = ++fixture.sequence;
        const auto rejected = fixture.call(command);
        PHPVPN_CHECK(rejected.error && rejected.error->code == "wizard_prerequisite_failed");
    }
    PHPVPN_CHECK(fixture.beginFinish().error.has_value());
    PHPVPN_CHECK_EQ(*fixture.runtime.files->bytes, original);
    PHPVPN_CHECK_EQ(fixture.runtime.files->writes, 0u);
    fixture.probeHeaders.clear();
    fixture.allChecks();
    PHPVPN_CHECK_EQ(fixture.probeHeaders.size(), 6u);
    for (const auto& observation : fixture.probeHeaders) PHPVPN_CHECK_EQ(observation.headers, fixture.request.fields.httpHeaders);
    const auto view = WizardIpc::parseView(fixture.call(WizardCommand::resume).result);
    PHPVPN_REQUIRE_OK(view);
    PHPVPN_CHECK_EQ(view->context->httpHeaders, "X-Test: fixture");
    PHPVPN_CHECK_EQ(view->fields->httpHeaders, fixture.request.fields.httpHeaders);
    // Resume deliberately clears evidence. Recheck the same immutable input.
    fixture.allChecks();
    const auto begun = fixture.beginFinish();
    PHPVPN_CHECK(!begun.error);
    PHPVPN_CHECK(!fixture.call(WizardCommand::finish).error);
    const auto headers = fixture.request.fields.httpHeaders;
    fixture.request.fields.httpHeaders = "Cookie: changed-again";
    const auto rejected = fixture.call(WizardCommand::finish);
    PHPVPN_CHECK(rejected.error && rejected.error->code == "invalid_argument");
    fixture.request.fields.httpHeaders = headers;
    fixture.runFile();
    PHPVPN_CHECK_EQ(fixture.runtime.files->writes, 1u);
    PHPVPN_CHECK_EQ(analyzeWizardDocument(*fixture.runtime.files->bytes)->fields.httpHeaders, headers);
}
PHPVPN_TEST(probe_http_headers, required_cookie_unlocks_all_checks_but_cancel_never_publishes) {
    WizardServiceFixture fixture;
    const auto original = *fixture.runtime.files->bytes;
    fixture.configureProbe = [&](ScriptedTransport& transport) {
        auto originalHook = transport.hook;
        transport.hook = [&, observed = &transport, originalHook](HttpAction action, std::string_view body, int count) -> std::optional<HttpExchangeResult> {
            if (observed->configuredHeaders.find("Cookie: required=synthetic") == std::string::npos)
                return HttpExchangeResult(HttpResponse{403, "required cookie missing", "text/plain"}, 23);
            return originalHook(action, body, count);
        };
    };
    fixture.request.clientAttemptId = fixture.runtime.crypto.actual->randomUuidV4().value();
    fixture.request.attemptSequence = ++fixture.sequence;
    auto started = fixture.call(WizardCommand::check_url);
    PHPVPN_CHECK(!started.error);
    fixture.request.operationId = started.result.find("operationId")->stringValue();
    fixture.runDiagnostic();
    auto result = WizardIpc::parseOperation(fixture.call(WizardCommand::get_operation).result).value();
    PHPVPN_CHECK_EQ(result.state, WizardOperationState::failed);
    fixture.request.fields.httpHeaders = "Cookie: required=synthetic\r\nUser-Agent: fixture";
    fixture.allChecks();
    PHPVPN_CHECK_EQ(fixture.probeHeaders.size(), 6u);
    PHPVPN_CHECK(!fixture.call(WizardCommand::close).error);
    PHPVPN_CHECK_EQ(fixture.runtime.files->writes, 0u);
    PHPVPN_CHECK_EQ(*fixture.runtime.files->bytes, original);
}
PHPVPN_TEST(flow_lifecycle, random_secret_initialization_failure_fails_start_before_http) {
    EngineFixture fixture;fixture.crypto.failRandom=true;
    PHPVPN_REQUIRE_ERROR(fixture.engine.start(),ErrorCode::crypto_failed);
    PHPVPN_CHECK_EQ(fixture.transport.healthCalls.load(),0);
    PHPVPN_CHECK_EQ(fixture.driver.starts.load(),0);
    PHPVPN_CHECK_EQ(fixture.engine.status().state,ConnectionState::error);
}
} // namespace phpvpn

namespace phpvpn {
PHPVPN_TEST(startup_connect, false_missing_and_default_never_connect_or_replay) {
    for (int mode = 0; mode < 3; ++mode) {
        EngineFixture f;
        if (mode == 1) setStartupPreference(f, false);
        if (mode == 2) f.files->bytes = Json::serialize(makeNewWizardDocument().dom);
        PHPVPN_REQUIRE_OK(f.engine.start());
        StartupConnect startup(f.store, f.engine);
        std::function<void()> task;
        unsigned posted = 0;
        auto post = [&](std::function<void()> fn) { ++posted; task = std::move(fn); return true; };
        startup.request(post);
        PHPVPN_CHECK_EQ(f.files->reads, 0u);
        task();
        startup.request(post); startup.tick(post);
        PHPVPN_CHECK_EQ(posted, 1u);
        PHPVPN_CHECK_EQ(f.engine.connectionIdentity().epoch, std::uint64_t(0));
        PHPVPN_CHECK_EQ(f.engine.status().state, ConnectionState::disconnected);
        PHPVPN_CHECK_EQ(f.driver.loads.load(), 0);
    }
}
PHPVPN_TEST(startup_connect, true_uses_normal_connect_and_coalesces_manual_request) {
    EngineFixture f;
    setStartupPreference(f, true);
    PHPVPN_REQUIRE_OK(f.engine.start());
    StartupConnect startup(f.store, f.engine);
    std::function<void()> task;
    startup.request([&](auto fn) { task = std::move(fn); return true; });
    PHPVPN_REQUIRE_OK(f.engine.connect());
    task();
    PHPVPN_CHECK(eventually([&] { return f.engine.status().state == ConnectionState::connected; }));
    PHPVPN_CHECK_EQ(f.engine.connectionIdentity().epoch, std::uint64_t(1));
    PHPVPN_CHECK_EQ(f.driver.starts.load(), 1);
}
PHPVPN_TEST(startup_connect, invalid_inputs_publish_safe_error_without_http_and_allow_repair) {
    for (int mode = 0; mode < 6; ++mode) {
        EngineFixture f;
        setStartupPreference(f, true);
        if (mode == 0) f.files->bytes = "{secret-invalid-json";
        if (mode == 1) f.files->readError = ErrorCode::config_unreadable;
        if (mode == 2) f.files->bytes = std::string("\xff", 1);
        if (mode == 3) f.files->bytes = R"({"masterSetup":{"autoConnectOnStartup":true}})";
        if (mode == 4) f.files->bytes = R"({"masterSetup":{"autoConnectOnStartup":"true"}})";
        if (mode == 5) f.files->beforeRead = [] { throw std::runtime_error("PRIVATE-SECRET"); };
        PHPVPN_REQUIRE_OK(f.engine.start());
        StartupConnect startup(f.store, f.engine);
        startup.request([](auto fn) { fn(); return true; });
        const auto status = f.engine.status();
        PHPVPN_CHECK_EQ(status.state, ConnectionState::error);
        PHPVPN_CHECK(status.lastError.has_value());
        PHPVPN_CHECK(status.lastError->description.find("PRIVATE") == std::string::npos);
        PHPVPN_CHECK_EQ(f.driver.loads.load(), 0);
        PHPVPN_CHECK_EQ(f.engine.connectionIdentity().epoch, std::uint64_t(0));
        f.files->readError.reset(); f.files->beforeRead = {}; f.files->bytes = kConfig;
        PHPVPN_REQUIRE_OK(f.engine.connect());
        PHPVPN_CHECK(eventually([&] { return f.engine.status().state == ConnectionState::connected; }));
    }
}
PHPVPN_TEST(startup_connect, full_queue_retries_once_and_disconnect_or_stop_cancel_delayed_work) {
    for (int mode = 0; mode < 3; ++mode) {
        EngineFixture f;
        setStartupPreference(f, true);
        PHPVPN_REQUIRE_OK(f.engine.start());
        StartupConnect startup(f.store, f.engine);
        startup.request([](auto) { return false; });
        PHPVPN_CHECK_EQ(f.files->reads, 0u);
        std::function<void()> task;
        startup.tick([&](auto fn) { task = std::move(fn); return true; });
        if (mode == 0) PHPVPN_REQUIRE_OK(f.engine.disconnect());
        if (mode == 1) { startup.cancel(); f.engine.shutdown(); }
        if (mode == 2) f.files->beforeRead = [&] { (void)f.engine.disconnect(); };
        task();
        PHPVPN_CHECK_EQ(f.engine.connectionIdentity().epoch, std::uint64_t(0));
        PHPVPN_CHECK_EQ(f.driver.loads.load(), 0);
        PHPVPN_CHECK(!f.engine.status().lastError);
    }
}
PHPVPN_TEST(startup_connect, automatic_bootstrap_publishes_connecting_success_and_standard_failures) {
    for (int mode = 0; mode < 3; ++mode) {
        EngineFixture f;
        setStartupPreference(f, true);
        Gate health;
        f.transport.badHealthTag = mode == 2;
        f.transport.hook = [&](HttpAction action, std::string_view, int) -> std::optional<HttpExchangeResult> {
            if (action == HttpAction::health) {
                health.block();
                if (mode == 1) return HttpExchangeResult{Error(ErrorCode::transport_timeout), 0};
            }
            return {};
        };
        f.transport.cancel = [&] { health.release(); };
        PHPVPN_REQUIRE_OK(f.engine.start());
        StartupConnect startup(f.store, f.engine);
        startup.request([](auto fn) { fn(); return true; });
        PHPVPN_CHECK(health.await());
        PHPVPN_CHECK_EQ(f.engine.status().state, ConnectionState::connecting);
        health.release();
        const auto expected = mode == 0 ? ConnectionState::connected : mode == 1 ? ConnectionState::recovering : ConnectionState::error;
        PHPVPN_CHECK(eventually([&] { return f.engine.status().state == expected; }));
        PHPVPN_CHECK_EQ(f.engine.connectionIdentity().epoch, std::uint64_t(1));
        PHPVPN_CHECK_EQ(f.engine.status().lastError.has_value(), mode != 0);
        PHPVPN_CHECK_EQ(f.driver.starts.load(), mode == 0 ? 1 : 0);
        startup.request([](auto) { throw std::runtime_error("startup replayed"); return false; });
        PHPVPN_REQUIRE_OK(f.engine.disconnect());
        PHPVPN_CHECK(eventually([&] { return f.engine.status().state == ConnectionState::disconnected; }));
    }
}
PHPVPN_TEST(startup_connect, stopping_during_blocked_read_revokes_late_connect) {
    EngineFixture f;
    setStartupPreference(f, true);
    Gate read;
    f.files->beforeRead = [&] { read.block(); };
    PHPVPN_REQUIRE_OK(f.engine.start());
    StartupConnect startup(f.store, f.engine);
    std::function<void()> task;
    startup.request([&](auto fn) { task = std::move(fn); return true; });
    std::thread callback([&] { task(); });
    PHPVPN_CHECK(read.await());
    startup.cancel();
    f.engine.shutdown();
    read.release();
    callback.join();
    PHPVPN_CHECK_EQ(f.transport.healthCalls.load(), 0);
    PHPVPN_CHECK_EQ(f.driver.starts.load(), 0);
    PHPVPN_CHECK_EQ(f.engine.connectionIdentity().epoch, std::uint64_t(0));
}
PHPVPN_TEST(startup_connect, delayed_startup_cannot_retry_or_replace_a_newer_manual_failure) {
    EngineFixture f;
    setStartupPreference(f, true);
    f.transport.badHealthTag = true;
    PHPVPN_REQUIRE_OK(f.engine.start());
    StartupConnect startup(f.store, f.engine);
    const auto token = f.engine.startupControlToken();
    const auto epoch = f.engine.connectionIdentity().epoch;
    std::function<void()> task;
    startup.request([&](auto fn) { task = std::move(fn); return true; });
    PHPVPN_REQUIRE_OK(f.engine.connect());
    PHPVPN_CHECK(eventually([&] { return f.engine.status().state == ConnectionState::error; }));
    const auto error = f.engine.status().lastError->code;
    const auto reads = f.files->reads;
    task();
    PHPVPN_REQUIRE_OK(f.engine.connectAtStartup(token, epoch));
    f.engine.publishStartupError(token, epoch, Error(ErrorCode::config_invalid_json));
    PHPVPN_CHECK_EQ(f.files->reads, reads);
    PHPVPN_CHECK_EQ(f.engine.connectionIdentity().epoch, std::uint64_t(1));
    PHPVPN_CHECK_EQ(f.engine.status().lastError->code, error);
    PHPVPN_CHECK_EQ(f.transport.healthCalls.load(), 1);
}
PHPVPN_TEST(server_version, connect_to_an_incompatible_server_fails_without_tunnel_or_retry) {
    // health and info both carry the version; either one stops the bootstrap.
    for (const bool inHealth : {true, false}) {
        EngineFixture fixture;
        (inHealth ? fixture.transport.incompatibleHealth : fixture.transport.incompatibleInfo) = true;
        PHPVPN_REQUIRE_OK(fixture.engine.start());
        PHPVPN_REQUIRE_OK(fixture.engine.connect());
        PHPVPN_CHECK(eventually([&] { return fixture.engine.status().state == ConnectionState::error; }));
        const auto status = fixture.engine.status();
        PHPVPN_CHECK(status.lastError.has_value());
        if (status.lastError) {
            PHPVPN_CHECK_EQ(status.lastError->code, std::string("server_version_incompatible"));
            PHPVPN_CHECK(!status.lastError->recoverable);
        }
        // The reachable but incompatible server fails the URL checkpoint.
        PHPVPN_CHECK(allChecks(status, CheckpointResult::failed, CheckpointResult::pending, CheckpointResult::pending));
        PHPVPN_CHECK(!status.httpDiagnostic);
        PHPVPN_CHECK_EQ(fixture.transport.dataCalls.load(), 0);
        PHPVPN_CHECK_EQ(fixture.driver.starts.load(), 0);
        // No recovery with backoff: the attempt stays a single terminal failure.
        fixture.clock.now += 600000;
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        PHPVPN_CHECK_EQ(fixture.engine.status().state, ConnectionState::error);
        PHPVPN_CHECK_EQ(fixture.transport.healthCalls.load(), 1);
        PHPVPN_CHECK_EQ(fixture.transport.infoCalls.load(), inHealth ? 0 : 1);
    }
}

PHPVPN_TEST(server_version, compatible_minor_and_patch_versions_connect) {
    EngineFixture fixture;
    PHPVPN_REQUIRE_OK(fixture.engine.start());
    PHPVPN_REQUIRE_OK(fixture.engine.connect());
    PHPVPN_CHECK(eventually([&] { return fixture.engine.status().state == ConnectionState::connected; }));
    PHPVPN_CHECK(allChecks(fixture.engine.status(), CheckpointResult::success, CheckpointResult::success,
                           CheckpointResult::success));
}

PHPVPN_TEST(server_version, server_replaced_during_connection_is_detected_by_the_time_resync) {
    EngineFixture fixture;
    PHPVPN_REQUIRE_OK(fixture.engine.start());
    PHPVPN_REQUIRE_OK(fixture.engine.connect());
    PHPVPN_CHECK(eventually([&] { return fixture.engine.status().state == ConnectionState::connected; }));
    const int healthBefore = fixture.transport.healthCalls.load();
    fixture.transport.incompatibleHealth = true;
    fixture.clock.now += kDefaultTimeSyncIntervalSeconds * 1000;
    PHPVPN_CHECK(eventually([&] { return fixture.engine.status().state == ConnectionState::error; }));
    const auto status = fixture.engine.status();
    PHPVPN_CHECK(fixture.transport.healthCalls.load() > healthBefore);
    PHPVPN_CHECK(status.lastError.has_value());
    if (status.lastError) {
        PHPVPN_CHECK_EQ(status.lastError->code, std::string("server_version_incompatible"));
        PHPVPN_CHECK(!status.lastError->recoverable);
    }
    PHPVPN_CHECK(eventually([&] { return fixture.driver.ends.load() >= 1; }));
    PHPVPN_CHECK_EQ(fixture.engine.diagnostics().recoveryCount, std::uint64_t(0));
    // Nothing is exchanged with the incompatible server any more.
    const int dataAfter = fixture.transport.dataCalls.load();
    const int healthAfter = fixture.transport.healthCalls.load();
    fixture.clock.now += 600000;
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    PHPVPN_CHECK_EQ(fixture.transport.dataCalls.load(), dataAfter);
    PHPVPN_CHECK_EQ(fixture.transport.healthCalls.load(), healthAfter);
    PHPVPN_CHECK_EQ(fixture.engine.status().state, ConnectionState::error);
}

PHPVPN_TEST(server_version, soft_recovery_401_refresh_from_an_incompatible_server_stops_the_connection) {
    EngineFixture fixture;
    fixture.transport.hook = [&](HttpAction action, std::string_view, int call) -> std::optional<HttpExchangeResult> {
        if (action == HttpAction::data && call == 2) {
            fixture.transport.incompatibleHealth = true;
            return HttpExchangeResult{HttpResponse{401, std::string("{\"type\":\"error\",\"code\":\"unauthorized\"}"), "application/json"}, 40};
        }
        return {};
    };
    PHPVPN_REQUIRE_OK(fixture.engine.start());
    PHPVPN_REQUIRE_OK(fixture.engine.connect());
    PHPVPN_CHECK(eventually([&] { return fixture.engine.status().state == ConnectionState::error; }));
    const auto status = fixture.engine.status();
    PHPVPN_CHECK(status.lastError.has_value());
    if (status.lastError) PHPVPN_CHECK_EQ(status.lastError->code, std::string("server_version_incompatible"));
    PHPVPN_CHECK_EQ(fixture.transport.healthCalls.load(), 2);
    PHPVPN_CHECK_EQ(fixture.engine.diagnostics().recoveryCount, std::uint64_t(0));
    PHPVPN_CHECK(eventually([&] { return fixture.driver.ends.load() >= 1; }));
}

PHPVPN_TEST(server_version, worker_rejects_incompatible_health_and_info_without_http_diagnostic) {
    // The same NetworkWorker calls serve Connect, recovery, the time resync,
    // get-info (phpvpn info) and get-server-info (Server information).
    ObservedCrypto crypto;
    ScriptedTransport transport(*crypto.actual);
    TestClock clock;
    NetworkWorker worker(crypto, transport, clock);
    PHPVPN_REQUIRE_OK(worker.configure(fixtureConfig()));
    std::optional<HttpDiagnostic> diagnostic;
    bool rejected = true;
    transport.incompatibleHealth = true;
    auto health = worker.performHealth({}, &diagnostic, &rejected);
    PHPVPN_REQUIRE_ERROR(health, ErrorCode::server_version_incompatible);
    PHPVPN_CHECK_EQ(health.error().detail, incompatibleServerVersion());
    PHPVPN_CHECK(!diagnostic && !rejected);
    // An incompatible server is not even used as a clock.
    PHPVPN_CHECK(!worker.timeSync().hasSnapshot());
    transport.incompatibleHealth = false;
    PHPVPN_REQUIRE_OK(worker.performHealth());
    transport.incompatibleInfo = true;
    rejected = true;
    PHPVPN_REQUIRE_ERROR(worker.performInfo(&diagnostic, &rejected), ErrorCode::server_version_incompatible);
    PHPVPN_CHECK(!diagnostic && !rejected);
    rejected = true;
    PHPVPN_REQUIRE_ERROR(worker.performExtendedInfo(InfoRequestOptions{std::nullopt, true}, &diagnostic, &rejected),
                         ErrorCode::server_version_incompatible);
    PHPVPN_CHECK(!diagnostic && !rejected);
    PHPVPN_CHECK_EQ(transport.dataCalls.load(), 0);
    transport.incompatibleInfo = false;
    PHPVPN_REQUIRE_OK(worker.performInfo());
}

PHPVPN_TEST(server_version, wizard_reports_the_incompatible_server_in_every_check) {
    const auto failedCheck = [](WizardServiceFixture& fixture, WizardCommand command) {
        fixture.request.command = command;
        fixture.request.attemptSequence = ++fixture.sequence;
        fixture.request.clientAttemptId = fixture.runtime.crypto.actual->randomUuidV4().value();
        const auto started = fixture.ask(fixture.request);
        PHPVPN_CHECK(!started.error);
        fixture.request.operationId = started.result.find("operationId")->stringValue();
        fixture.runDiagnostic();
        auto result = WizardIpc::parseOperation(fixture.call(WizardCommand::get_operation).result);
        if (!result) throw std::runtime_error("operation did not parse");
        return std::move(result).value();
    };
    struct Case { WizardCommand command; bool health; const char* field; };
    for (const Case item : {Case{WizardCommand::check_url, true, "serverUrl"},
                            Case{WizardCommand::check_secret, true, "serverSecret"},
                            Case{WizardCommand::check_secret, false, "serverSecret"},
                            Case{WizardCommand::check_access, true, "accessKey"},
                            Case{WizardCommand::check_access, false, "accessKey"}}) {
        WizardServiceFixture fixture;
        // The earlier steps pass against a compatible server, then the server is replaced.
        if (item.command != WizardCommand::check_url) fixture.check(WizardCommand::check_url);
        if (item.command == WizardCommand::check_access) fixture.check(WizardCommand::check_secret);
        const auto probesBefore = fixture.probeHeaders.size();
        fixture.configureProbe = [&](ScriptedTransport& transport) {
            (item.health ? transport.incompatibleHealth : transport.incompatibleInfo) = true;
        };
        const auto operation = failedCheck(fixture, item.command);
        PHPVPN_CHECK(operation.state == WizardOperationState::failed);
        PHPVPN_CHECK(operation.error.has_value());
        if (!operation.error) continue;
        PHPVPN_CHECK_EQ(operation.error->code, std::string("server_version_incompatible"));
        PHPVPN_CHECK_EQ(operation.error->field.value_or(""), std::string(item.field));
        PHPVPN_CHECK(!operation.error->httpStatus);
        PHPVPN_CHECK(!operation.httpDiagnostic);
        PHPVPN_CHECK(operation.error->description.find("server version " + incompatibleServerVersion() + ".") !=
                     std::string::npos);
        PHPVPN_CHECK(operation.error->suggestedAction.find("same major version") != std::string::npos);
        // No access key data request is ever sent to an incompatible server.
        for (auto index = probesBefore; index < fixture.probeHeaders.size(); ++index)
            PHPVPN_CHECK(fixture.probeHeaders[index].action != HttpAction::data);
    }
}
}
