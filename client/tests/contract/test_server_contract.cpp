// Explicit integration target. Requires an isolated PHP wizard-default relay.
// Every HTTP exchange uses the platform transport (WinHTTP, Linux libcurl) and
// every crypto operation delegates to the platform provider (CNG, OpenSSL).
// No service, tunnel, ProgramData, filesystem queue manipulation or mock server.
#include "harness.h"
#include "core/config/config_defaults.h"
#include "core/crypto/canonical.h"
#include "core/crypto/key_derivation.h"
#include "core/crypto/masked_contracts.h"
#include "core/presentation/server_info_model.h"
#include "core/protocol/base64url.h"
#include "core/protocol/data_record.h"
#include "core/protocol/request_builder.h"
#include "core/runtime/network_worker.h"
#include "core/runtime/vpn_engine.h"
#include "fake_config_files.h"
#include "core/wizard/probe_client.h"
#include "core/wizard/wizard_defaults.h"
#if defined(_WIN32)
#include <windows.h>
#else
#include <cstdlib>
#endif
#include <algorithm>
#include <array>
#include <set>
#include <stdexcept>

namespace phpvpn {
namespace {
template<class T> T require(Result<T> result) {
    if (!result) throw std::runtime_error(std::string("real relay operation failed: ") + result.error().name());
    return std::move(result).value();
}
void require(Status result) {
    if (!result) throw std::runtime_error(std::string("real relay operation failed: ") + result.error().name());
}
std::string relayUrl() {
#if defined(_WIN32)
    std::array<char, 2048> buffer{};
    const DWORD length = GetEnvironmentVariableA("PHPVPN_CONTRACT_URL", buffer.data(),
                                                static_cast<DWORD>(buffer.size()));
    if (length >= buffer.size()) throw std::runtime_error("PHPVPN_CONTRACT_URL is too long");
    const std::string value = length ? std::string(buffer.data(), length) : "http://127.0.0.1:18093/phpvpn.php";
#else
    const char* configured = std::getenv("PHPVPN_CONTRACT_URL");
    const std::size_t length = configured != nullptr ? std::string_view(configured).size() : 0;
    if (length >= 2048) throw std::runtime_error("PHPVPN_CONTRACT_URL is too long");
    const std::string value = length ? std::string(configured, length) : "http://127.0.0.1:18093/phpvpn.php";
#endif
    const auto parsed = parseServerUrl(value);
    if (!parsed || parsed->secure || parsed->host != "127.0.0.1" || parsed->path != "/phpvpn.php")
        throw std::runtime_error("contract target requires an explicit local http://127.0.0.1:<port>/phpvpn.php fixture");
    return value;
}
// Instrumentation observes the production provider without replacing outcomes.
class ObservedCrypto final : public CryptoProvider {
public:
    explicit ObservedCrypto(CryptoProvider& real) : real_(real) {}
    unsigned payloadOpens = 0;
    Result<Sha256Digest> sha256(std::span<const std::uint8_t> data) override { return real_.sha256(data); }
    Result<Sha256Digest> hmacSha256(std::span<const std::uint8_t> key, std::span<const std::uint8_t> data) override {
        return real_.hmacSha256(key, data);
    }
    Status aesGcmSeal(std::span<const std::uint8_t> key, std::span<const std::uint8_t> iv,
        std::span<const std::uint8_t> aad, std::span<const std::uint8_t> plaintext,
        std::vector<std::uint8_t>& ciphertext, GcmTag& tag) override {
        return real_.aesGcmSeal(key, iv, aad, plaintext, ciphertext, tag);
    }
    Result<SecureBuffer> aesGcmOpen(std::span<const std::uint8_t> key, std::span<const std::uint8_t> iv,
        std::span<const std::uint8_t> aad, std::span<const std::uint8_t> ciphertext,
        std::span<const std::uint8_t> tag) override {
        const std::string_view text(reinterpret_cast<const char*>(aad.data()), aad.size());
        if (text.starts_with("PHPVPN-DATA\n")) ++payloadOpens;
        return real_.aesGcmOpen(key, iv, aad, ciphertext, tag);
    }
    Status randomBytes(std::span<std::uint8_t> out) override { return real_.randomBytes(out); }
private:
    CryptoProvider& real_;
};
class ObservedTransport final : public Transport {
public:
    explicit ObservedTransport(Transport& real) : real_(real) {}
    unsigned dataPosts = 0;
    Status configure(const TransportSettings& settings) override { return real_.configure(settings); }
    HttpExchangeResult post(HttpAction action, std::string_view body) override {
        if (action == HttpAction::data) ++dataPosts;
        return real_.post(action, body);
    }
    void cancelCurrentRequest() override { real_.cancelCurrentRequest(); }
private:
    Transport& real_;
};
struct Fixture {
    std::unique_ptr<CryptoProvider> realCrypto = require(createPlatformCryptoProvider());
    ObservedCrypto crypto{*realCrypto};
    std::unique_ptr<Transport> realTransport = require(createPlatformTransport());
    ObservedTransport transport{*realTransport};
    std::unique_ptr<Clock> clock = createPlatformClock();
    ProbeClient probe{crypto, transport, *clock};
    WizardDocument document = makeNewWizardDocument();
    Fixture() {
        document.fields.serverUrl = relayUrl();
        document.context.runtime.responseTimeoutSeconds = 3;
        identity(2);
    }
    void identity(unsigned octet) {
        document.fields.virtualIp = "10.99.99." + std::to_string(octet);
        document.fields.accessKey = defaultAccessKey(document.fields.virtualIp);
    }
    std::size_t poll() { return require(probe.checkAccess(document, document.fields)); }
};
ClientConfigPtr runtimeConfig(unsigned own, unsigned peer) {
    const auto string = [](std::string text) { return JsonValue::makeString(std::move(text)); };
    const std::string ip = "10.99.99." + std::to_string(own);
    const auto json = JsonValue::makeObject({
        {"masterSetup", JsonValue::makeObject({
            {"httpHeaders", string(kDefaultHttpHeaders)}, {"serverUrl", string(relayUrl())}, {"serverSecret", string(std::string(kDefaultServerSecret))},
            {"vpnCidr", string("10.99.99.0/24")}, {"virtualIp", string(ip)},
            {"accessKey", string(defaultAccessKey(ip))}
        })},
        {"runtime", JsonValue::makeObject({{"responseTimeoutSeconds", JsonValue::makeInteger(3)}})},
        {"communicationContracts", JsonValue::makeObject({
            {"10.99.99." + std::to_string(peer), JsonValue::makeObject({
                // Independent fixed editor-template .2/.3 payload key.
                {"payloadKey", string("918c488528c2bc41a90f000b0a63bab0ba418790058ea7b448a3760aec16349d")}
            })}
        })}
    });
    ConfigWarnings warnings;
    return require(parseAndValidateConfig(Json::serialize(json), warnings));
}
std::string badPayloadRecord(Fixture& sender) {
    const auto id = require(sender.crypto.randomUuidV4());
    const auto iv = require(sender.crypto.randomVector(kGcmIvSize));
    const std::array<std::uint8_t, kGcmTagSize> invalidTag{};
    const std::array<std::uint8_t, 4> ciphertext{1, 2, 3, 4};
    return DataRecord::build(id, "10.99.99.2", "10.99.99.3", Base64Url::encode(iv),
        Base64Url::encode(invalidTag), Base64Url::encode(ciphertext));
}
void queueRecord(Fixture& sender, NetworkWorker& worker, const std::string& record) {
    auto config = worker.config();
    KeyDerivation keys(sender.crypto);
    auto key = require(keys.fullAuthKey(kDefaultServerSecret, config->accessKey.span(), "10.99.99.2"));
    const auto id = require(sender.crypto.randomUuidV4());
    const auto timestamp = require(worker.timeSync().requestTimestamp(sender.clock->monotonicMs()));
    const auto body = record + '\n';
    const auto auth = require(RequestBuilder::buildAuthFull(sender.crypto, key, "10.99.99.2", timestamp, id, body));
    auto response = sender.transport.post(HttpAction::data, auth + '\n' + body);
    if (!response.ok() || response.value().statusCode != 200)
        throw std::runtime_error("the real PHP relay did not accept the queued-record request");
    const auto parsed = require(NdjsonResponseReader::parseAndVerify(sender.crypto, key, id, response.value().body, {}));
    const auto fields = require(DataRecord::parse(record));
    if (parsed.acceptedRecords != 1 || parsed.acceptedMessageIds.size() != 1 ||
        parsed.acceptedMessageIds.front() != fields.messageId || !parsed.deferredMessageIds.empty())
        throw std::runtime_error("the real PHP relay did not confirm exactly the injected record");
}
PacketBytes packet() {
    PacketBytes bytes(24, 0);
    bytes[0] = 0x45;
    bytes[3] = 24;
    bytes[8] = 64;
    bytes[9] = 253;
    Ipv4Address::parse("10.99.99.2")->toNetworkBytes(bytes.data() + 12);
    Ipv4Address::parse("10.99.99.3")->toNetworkBytes(bytes.data() + 16);
    bytes[20] = 't'; bytes[21] = 'e'; bytes[22] = 's'; bytes[23] = 't';
    const auto checksum = ipv4HeaderChecksum(std::span<const std::uint8_t>(bytes.data(), 20));
    bytes[10] = static_cast<std::uint8_t>(checksum >> 8);
    bytes[11] = static_cast<std::uint8_t>(checksum);
    return bytes;
}
}

PHPVPN_TEST(server_contract, real_url_default_secret_and_wrong_credentials) {
    Fixture f;
    // Availability is a required real request. An absent server fails, never skips.
    PHPVPN_REQUIRE_OK(f.probe.checkUrl(f.document.context, f.document.fields));
    auto wrong = f.document.fields;
    wrong.serverSecret = "wrong-contract-secret";
    PHPVPN_REQUIRE_OK(f.probe.checkUrl(f.document.context, wrong)); // URL syntax is deliberately unauthenticated.
    PHPVPN_CHECK(!f.probe.checkSecret(f.document.context, wrong));
    PHPVPN_CHECK_EQ(f.probe.lastHttpStatus().value_or(0), 200); // GMAC failure, not HTTP connectivity.
    wrong.serverSecret.assign(64, '0');
    PHPVPN_CHECK(!f.probe.checkSecret(f.document.context, wrong));
    auto empty = f.document.fields;
    empty.serverSecret.clear();
    PHPVPN_REQUIRE_OK(f.probe.checkSecret(f.document.context, empty));
    PHPVPN_REQUIRE_OK(f.probe.checkSecret(f.document.context, f.document.fields));
    wrong = f.document.fields;
    wrong.accessKey.assign(64, '1');
    PHPVPN_REQUIRE_ERROR(f.probe.checkAccess(f.document, wrong), ErrorCode::server_unauthorized);
    PHPVPN_CHECK_EQ(f.probe.lastHttpStatus().value_or(0), 401);
}

PHPVPN_TEST(server_contract, all_fourteen_default_clients_authenticate_and_are_reported) {
    Fixture f;
    for (unsigned octet = 2; octet <= 15; ++octet) {
        f.identity(octet);
        const auto before = f.transport.dataPosts;
        PHPVPN_REQUIRE_OK(f.probe.checkAccess(f.document, f.document.fields));
        PHPVPN_CHECK_EQ(f.transport.dataPosts - before, 1u);
    }
    auto info = f.probe.checkSecret(f.document.context, f.document.fields);
    PHPVPN_REQUIRE_OK(info);
    std::set<std::string> clients;
    for (const auto& client : info->clients) {
        PHPVPN_CHECK(client.lastSeenMs > 0);
        clients.insert(client.address.toString());
    }
    PHPVPN_CHECK_EQ(clients.size(), std::size_t(14));
    for (unsigned octet = 2; octet <= 15; ++octet)
        PHPVPN_CHECK(clients.contains("10.99.99." + std::to_string(octet)));
    PHPVPN_CHECK_EQ(f.crypto.payloadOpens, 0u);
}

PHPVPN_TEST(server_contract, diagnostic_consumes_without_payload_gcm_while_bootstrap_validates_and_delivers) {
    Fixture sender, receiver;
    receiver.identity(3);
    sender.poll(); // Authenticated cleanup of only this isolated fixture's queues.
    receiver.poll();
    NetworkWorker send(sender.crypto, sender.transport, *sender.clock);
    NetworkWorker receive(receiver.crypto, receiver.transport, *receiver.clock);
    require(send.configure(runtimeConfig(2, 3)));
    require(send.performHealth());
    require(send.performHealth()); // Refresh with an existing snapshot remains auth-none.
    require(send.performInfo());
    require(receive.configure(runtimeConfig(3, 2)));
    require(receive.performHealth());
    require(receive.performInfo());
    DropCounters outDrops, inDrops, senderDrops;
    auto hasher = require(HmacFlowHasher::create(sender.crypto));
    DirectionQueues outbound(1024 * 1024, 0, 1, *sender.clock, *hasher, outDrops),
        inbound(1024 * 1024, 0, 1, *receiver.clock, *hasher, inDrops),
        senderInbound(1024 * 1024, 0, 1, *sender.clock, *hasher, senderDrops);
    const auto malformed = badPayloadRecord(sender);
    queueRecord(sender, send, malformed);
    const auto beforePosts = receiver.transport.dataPosts;
    const auto beforeOpens = receiver.crypto.payloadOpens;
    PHPVPN_CHECK_EQ(receiver.poll(), std::size_t(1));
    PHPVPN_CHECK_EQ(receiver.transport.dataPosts - beforePosts, 1u);
    PHPVPN_CHECK_EQ(receiver.crypto.payloadOpens, beforeOpens);
    PHPVPN_CHECK_EQ(inbound.statistics().itemCount, std::size_t(0));
    PHPVPN_CHECK_EQ(receiver.poll(), std::size_t(0)); // Real queue was consumed, not merely ignored locally.
    // Requeue identical authenticated record bytes. Ordinary bootstrap must
    // attempt GCM, reject the payload and deliver no packet.
    queueRecord(sender, send, malformed);
    require(receive.verifyAccessKey(inbound, inDrops));
    PHPVPN_CHECK_EQ(receiver.crypto.payloadOpens, beforeOpens + 1);
    PHPVPN_CHECK_EQ(inDrops.totalForReason(DropReason::payload_tag_invalid), std::uint64_t(1));
    PHPVPN_CHECK_EQ(inbound.statistics().itemCount, std::size_t(0));
    // A genuinely encrypted batch travels through both production NetworkWorkers.
    const auto expected = packet();
    DropReason reason{};
    PHPVPN_CHECK(outbound.tryEnqueue(*Ipv4Address::parse("10.99.99.3"), expected, reason).valueOr(false));
    auto sent = send.exchangeData(outbound, senderInbound, outDrops, senderDrops, true);
    PHPVPN_REQUIRE_OK(sent);
    PHPVPN_CHECK_EQ(sent->packetsSent, std::size_t(1));
    require(receive.verifyAccessKey(inbound, inDrops));
    PHPVPN_CHECK_EQ(receiver.crypto.payloadOpens, beforeOpens + 2);
    auto delivered = inbound.takePacket(*Ipv4Address::parse("10.99.99.2"));
    PHPVPN_CHECK(delivered.has_value());
    if (delivered) PHPVPN_CHECK_EQ(*delivered, expected);
    PHPVPN_CHECK_EQ(receiver.poll(), std::size_t(0));
}
PHPVPN_TEST(server_contract, masked_contracts_full_auth_publish_and_read_only_snapshot) {
    Fixture a, b;
    NetworkWorker first(a.crypto,a.transport,*a.clock), second(b.crypto,b.transport,*b.clock);
    const auto configA=runtimeConfig(2,3), configB=runtimeConfig(3,2);
    require(first.configure(configA));require(second.configure(configB));
    require(first.performHealth());require(second.performHealth());
    auto hashesA=require(maskedCommunicationContracts(a.crypto,*configA));
    auto hashesB=require(maskedCommunicationContracts(b.crypto,*configB));
    PHPVPN_CHECK_EQ(hashesA.front().hash,hashesB.front().hash);
    InfoRequestOptions publishA;publishA.maskedCommunicationContracts=hashesA;publishA.includeMaskedCommunicationContracts=true;
    InfoRequestOptions publishB;publishB.maskedCommunicationContracts=hashesB;publishB.includeMaskedCommunicationContracts=true;
    require(first.performExtendedInfo(publishA));require(second.performExtendedInfo(publishB));
    InfoRequestOptions read;read.includeMaskedCommunicationContracts=true;
    const auto before=a.transport.dataPosts;
    auto response=require(first.performExtendedInfo(read));
    auto model=require(buildServerInfoModel({response.plaintext,configA->virtualIp,hashesA}));
    const auto row=std::find_if(model.rows.begin(),model.rows.end(),[&](const auto& item){return item.peer==configB->virtualIp;});
    PHPVPN_CHECK(row!=model.rows.end());
    if(row!=model.rows.end())PHPVPN_CHECK_EQ(row->state,ServerClientState::valid);
    PHPVPN_CHECK_EQ(a.transport.dataPosts,before);
    // Explicit empty publication withdraws the old contract immediately.
    publishB.maskedCommunicationContracts=std::vector<MaskedCommunicationContract>{};
    require(second.performExtendedInfo(publishB));
    response=require(first.performExtendedInfo(read));
    model=require(buildServerInfoModel({response.plaintext,configA->virtualIp,hashesA}));
    const auto withdrawn=std::find_if(model.rows.begin(),model.rows.end(),[&](const auto& item){return item.peer==configB->virtualIp;});
    PHPVPN_CHECK(withdrawn!=model.rows.end());
    if(withdrawn!=model.rows.end())PHPVPN_CHECK_EQ(withdrawn->state,ServerClientState::invalid);
    auto standard=require(first.performInfo());
    PHPVPN_CHECK(standard.plaintext.starts_with("{\n    \"serverVersion\":"));
    PHPVPN_CHECK(standard.plaintext.size() <= 262144);
    PHPVPN_CHECK(response.plaintext.starts_with("{\n    \"serverVersion\":"));
    PHPVPN_CHECK(response.plaintext.size() <= 262144);
    PHPVPN_CHECK(standard.plaintext.find("maskedCommunicationContracts")==std::string::npos);
}
PHPVPN_TEST(server_contract, server_info_operation_reads_current_config_once_without_connecting) {
    Fixture fixture;
    auto config=runtimeConfig(2,3);
    auto backend=std::make_unique<FakeConfigFiles>(fixture.crypto);
    auto* files=backend.get();files->bytes=config->rawJson;
    ConfigStore store(fixture.crypto,std::move(backend));
    auto driver=createPlatformTunnelDriver();auto net=createPlatformNetConfig();
    const auto directory=std::filesystem::temp_directory_path()/fixture.crypto.randomUuidV4().value();
    // Nonexistent private journal means recovery performs no network mutation.
    VpnEngine engine(fixture.crypto,fixture.transport,*fixture.clock,*driver,*net,
                     EnginePaths{directory/"config.json",directory/"journal.json",directory.wstring()},store);
    require(engine.start());
    const auto before=files->reads;
    auto snapshot=require(engine.fetchServerInfoOnce());
    PHPVPN_CHECK_EQ(files->reads,before+1);
    PHPVPN_CHECK_EQ(snapshot.localAddress,config->virtualIp);
    const auto firstHash=snapshot.localMaskedCommunicationContracts.front().hash;
    const auto key=std::string("918c488528c2bc41a90f000b0a63bab0ba418790058ea7b448a3760aec16349d");
    const auto keyAt=files->bytes->find(key);PHPVPN_CHECK(keyAt!=std::string::npos);
    files->bytes->replace(keyAt,key.size(),std::string(64,'a'));
    snapshot=require(engine.fetchServerInfoOnce());
    PHPVPN_CHECK_EQ(files->reads,before+2);
    PHPVPN_CHECK(snapshot.localMaskedCommunicationContracts.front().hash!=firstHash);
    const auto status=engine.status();
    PHPVPN_CHECK_EQ(status.state,ConnectionState::disconnected);
    PHPVPN_CHECK_EQ(status.counters.dataPollsTotal,std::uint64_t(0));
    PHPVPN_CHECK_EQ(fixture.transport.dataPosts,0u);
    PHPVPN_CHECK_EQ(files->writes,0u);
    PHPVPN_CHECK(!std::filesystem::exists(directory));
    engine.shutdown();
}
} // namespace phpvpn
