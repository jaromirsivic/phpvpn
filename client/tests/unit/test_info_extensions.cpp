#include "core/crypto/canonical.h"
#include "core/crypto/masked_contracts.h"
#include "core/protocol/request_builder.h"
#include "core/protocol/response_parser.h"
#include "harness.h"
namespace phpvpn {
PHPVPN_TEST(MaskedContracts, exact_secret_and_numeric_order) {
    auto provider = createPlatformCryptoProvider();
    PHPVPN_REQUIRE_OK(provider);
    ClientConfig config;
    config.virtualIp = *Ipv4Address::parse("10.99.99.2");
    config.vpnCidr = *Ipv4Cidr::parse("10.99.99.0/24");
    auto peer = *Ipv4Address::parse("10.99.99.10");
    config.contracts.push_back({peer, SecureBuffer(std::vector<std::uint8_t>(32, 1))});
    PHPVPN_CHECK_EQ(canonicalContractId(peer, config.virtualIp), "10.99.99.2_10.99.99.10");
    auto hashes = maskedCommunicationContracts(*provider.value(), config);
    PHPVPN_REQUIRE_OK(hashes);
    PHPVPN_CHECK_EQ(hashes->at(0).hash, "0c7a3dd333644ac165ce31c512e2e473c32909310f79c8cb7c492d84db14e459");
    config.serverSecret = "CHANGE_THIS_SERVER_SECRET";
    auto changed = maskedCommunicationContracts(*provider.value(), config);
    PHPVPN_REQUIRE_OK(changed);
    PHPVPN_CHECK(changed->at(0).hash != hashes->at(0).hash);
}
PHPVPN_TEST(InfoExtensions, canonical_presence_and_limits) {
    auto provider = createPlatformCryptoProvider();
    PHPVPN_REQUIRE_OK(provider);
    const auto src = *Ipv4Address::parse("10.99.99.2");
    InfoRequestOptions options{std::nullopt, true};
    auto canonical = canonicalInfoExtensions(options, src);
    PHPVPN_REQUIRE_OK(canonical);
    PHPVPN_CHECK_EQ(canonical.value(), "{\"includeMaskedCommunicationContracts\":true}");
    auto hash = provider.value()->sha256(asBytes(canonical.value()));
    PHPVPN_REQUIRE_OK(hash);
    PHPVPN_CHECK_EQ(sha256Hex(hash.value()), "225ecf0db7d2c44e191004a5afbbd9969fa7875097d1081d958f2d7d29f7ecf3");
    auto golden = RequestBuilder::buildInfoFull(*provider.value(), SecureBuffer(32), src,
        "2026-09-13T12:00:00.000Z", "8a1f9a26-6d0c-4c58-9f1d-2f0a7f6b1c30", options);
    PHPVPN_REQUIRE_OK(golden);
    PHPVPN_CHECK_EQ(Json::parse(*golden)->find("tag")->stringValue(),
                    "NrLbf7RH9URds5iv6S382huu-Msw1zZpLMvjox8gcmQ");
    options.maskedCommunicationContracts = std::vector<MaskedCommunicationContract>{};
    auto empty = canonicalInfoExtensions(options, src);
    PHPVPN_REQUIRE_OK(empty);
    PHPVPN_CHECK_EQ(empty.value(),
                    "{\"maskedCommunicationContracts\":[],\"includeMaskedCommunicationContracts\":true}");
    PHPVPN_CHECK(!canonicalInfoExtensions({}, src));
    for (std::uint32_t i = 3; i <= 254; ++i)
        options.maskedCommunicationContracts->push_back(
            {Ipv4Address((src.value() & 0xffffff00u) + i), std::string(64, 'a')});
    auto body = RequestBuilder::buildInfoFull(*provider.value(), SecureBuffer(32), src, "2026-09-13T12:00:00.000Z",
                                              "8a1f9a26-6d0c-4c58-9f1d-2f0a7f6b1c30", options);
    PHPVPN_REQUIRE_OK(body);
    PHPVPN_CHECK(body->size() < 65536);
    options.maskedCommunicationContracts->push_back(options.maskedCommunicationContracts->front());
    PHPVPN_CHECK(!canonicalInfoExtensions(options, src));
}
PHPVPN_TEST(InfoExtensions, response_requires_verified_privacy_extension) {
    const auto src = *Ipv4Address::parse("10.99.99.2");
    const auto info = [](const std::string &clients) {
        return std::string(R"({"serverVersion":"1","generatedAt":"2026-09-13T12:00:00.000Z","clients":)") + clients +
               R"(,"limits":{"maxDataRecordsPerRequest":16,"maxRequestBodyBytes":983040,"maxQueueBytesPerClient":16777216,"queueTtlSeconds":60,"minDataPollIntervalMs":50,"maxDataPollIntervalMs":3200}})";
    };
    PHPVPN_CHECK(!ResponseParser::parseInfoPlaintext(info("[]"), src));
    PHPVPN_REQUIRE_OK(ResponseParser::parseInfoPlaintext(info("[]")));
    const auto self =
        R"([{"address":"10.99.99.2","lastSeen":"2026-09-13T12:00:00.000Z","maskedCommunicationContracts":[]}])";
    PHPVPN_REQUIRE_OK(ResponseParser::parseInfoPlaintext(info(self), src));
    const auto foreign =
        std::string(
            R"([{"address":"10.99.99.3","lastSeen":"2026-09-13T12:00:00.000Z","maskedCommunicationContracts":[{"peer":"10.99.99.4","hash":")") +
        std::string(64, 'a') + R"("}]}])";
    PHPVPN_CHECK(!ResponseParser::parseInfoPlaintext(info(foreign), src));
}
} // namespace phpvpn
