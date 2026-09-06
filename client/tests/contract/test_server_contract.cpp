// Explicit integration target. Requires an isolated PHP wizard-default relay.
// Every HTTP exchange uses WinHTTP and every crypto operation delegates to CNG.
// No service, tunnel, ProgramData, filesystem queue manipulation or mock server.
#include "harness.h"
#include "core/config/config_defaults.h"
#include "core/crypto/canonical.h"
#include "core/crypto/key_derivation.h"
#include "core/protocol/base64url.h"
#include "core/protocol/data_record.h"
#include "core/protocol/request_builder.h"
#include "core/runtime/network_worker.h"
#include "core/wizard/probe_client.h"
#include "core/wizard/wizard_defaults.h"
#include <windows.h>
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
    std::array<char, 2048> buffer{};
    const DWORD length = GetEnvironmentVariableA("PHPVPN_CONTRACT_URL", buffer.data(),
                                                static_cast<DWORD>(buffer.size()));
    if (length >= buffer.size()) throw std::runtime_error("PHPVPN_CONTRACT_URL is too long");
    const std::string value = length ? std::string(buffer.data(), length) : "http://127.0.0.1:18093/phpvpn.php";
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
            {"serverUrl", string(relayUrl())}, {"serverSecret", string(std::string(kDefaultServerSecret))},
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
    DirectionQueues outbound(1024 * 1024, 0, 1), inbound(1024 * 1024, 0, 1), senderInbound(1024 * 1024, 0, 1);
    DropCounters outDrops, inDrops, senderDrops;
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
    PHPVPN_CHECK(outbound.tryEnqueue(*Ipv4Address::parse("10.99.99.3"), expected, reason));
    auto sent = send.exchangeData(outbound, senderInbound, outDrops, senderDrops, true);
    PHPVPN_REQUIRE_OK(sent);
    PHPVPN_CHECK_EQ(sent->packetsSent, std::size_t(1));
    require(receive.verifyAccessKey(inbound, inDrops));
    PHPVPN_CHECK_EQ(receiver.crypto.payloadOpens, beforeOpens + 2);
    auto delivered = inbound.popFront(*Ipv4Address::parse("10.99.99.2"));
    PHPVPN_CHECK(delivered.has_value());
    if (delivered) PHPVPN_CHECK_EQ(*delivered, expected);
    PHPVPN_CHECK_EQ(receiver.poll(), std::size_t(0));
}
} // namespace phpvpn
