// Configuration parsing and validation, client.md "Konfiguracni soubor".

#include <string>
#include <limits>

#include "core/config/config.h"
#include "core/config/config_document.h"
#include "harness.h"

namespace phpvpn {
namespace {

constexpr const char* kAccessKey =
    "0102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f20";
constexpr const char* kPayloadKey =
    "2122232425262728292a2b2c2d2e2f303132333435363738393a3b3c3d3e3f40";

std::string documentWith(const std::string& masterExtras,
                         const std::string& runtime = std::string(),
                         const std::string& contracts = std::string()) {
    std::string text = R"({"masterSetup":{"serverUrl":"https://relay.example/vpn.php",)";
    text += masterExtras;
    text += R"("serverSecret":"a-long-and-unpredictable-secret","vpnCidr":"10.99.99.0/24",)";
    text += R"("virtualIp":"10.99.99.2","accessKey":")";
    text += kAccessKey;
    text += "\"}";
    if (!runtime.empty()) {
        text += ",\"runtime\":" + runtime;
    }
    if (!contracts.empty()) {
        text += ",\"communicationContracts\":" + contracts;
    }
    text += "}";
    return text;
}

std::string validDocument() {
    return documentWith(std::string(), std::string(),
                        std::string(R"({"10.99.99.3":{"payloadKey":")") + kPayloadKey + R"("}})");
}

}  // namespace

PHPVPN_TEST(config, accepts_a_minimal_valid_document) {
    ConfigWarnings warnings;
    auto config = parseAndValidateConfig(validDocument(), warnings);
    PHPVPN_REQUIRE_OK(config);

    const ClientConfig& value = *config.value();
    PHPVPN_CHECK_EQ(value.serverUrl.host, std::string("relay.example"));
    PHPVPN_CHECK_EQ(value.serverUrl.port, std::uint16_t(443));
    PHPVPN_CHECK_EQ(value.serverUrl.path, std::string("/vpn.php"));
    PHPVPN_CHECK(value.serverUrl.secure);
    PHPVPN_CHECK_EQ(value.virtualIp.toString(), std::string("10.99.99.2"));
    PHPVPN_CHECK_EQ(value.accessKey.size(), std::size_t(32));
    PHPVPN_CHECK_EQ(value.contracts.size(), std::size_t(1));
    PHPVPN_CHECK_EQ(value.maxRemotePeers(), std::uint32_t(252));
    PHPVPN_CHECK(value.findContract(*Ipv4Address::parse("10.99.99.3")) != nullptr);
    PHPVPN_CHECK(value.findContract(*Ipv4Address::parse("10.99.99.4")) == nullptr);
}

PHPVPN_TEST(config, applies_the_documented_defaults) {
    ConfigWarnings warnings;
    auto config = parseAndValidateConfig(validDocument(), warnings);
    PHPVPN_REQUIRE_OK(config);

    const RuntimeLimits& runtime = config.value()->runtime;
    PHPVPN_CHECK_EQ(runtime.maxOutboundQueueBytes, kDefaultMaxOutboundQueueBytes);
    PHPVPN_CHECK_EQ(runtime.maxInboundQueueBytes, kDefaultMaxInboundQueueBytes);
    PHPVPN_CHECK_EQ(runtime.minPerPeerQueueBytes, kDefaultMinPerPeerQueueBytes);
    PHPVPN_CHECK_EQ(runtime.maxResponseBodyBytes, kDefaultMaxResponseBodyBytes);
    PHPVPN_CHECK_EQ(runtime.responseTimeoutSeconds, kDefaultResponseTimeoutSeconds);
    PHPVPN_CHECK_EQ(runtime.timeSyncIntervalSeconds, kDefaultTimeSyncIntervalSeconds);
    PHPVPN_CHECK_EQ(config.value()->trustAnyTlsCertificate, kDefaultTrustAnyTlsCertificate);
}

PHPVPN_TEST(config, reports_the_accepted_risks_as_warnings) {
    ConfigWarnings warnings;
    auto config = parseAndValidateConfig(validDocument(), warnings);
    PHPVPN_REQUIRE_OK(config);

    // The default trusts any certificate, which is an accepted but reported risk.
    PHPVPN_CHECK(warnings.trustsAnyTlsCertificate);
    PHPVPN_CHECK(!warnings.usesPlainHttp);
    PHPVPN_CHECK(!warnings.messages().empty());

    const std::string plain =
        R"({"masterSetup":{"serverUrl":"http://relay.example/vpn.php","serverSecret":"0000",)"
        R"("virtualIp":"10.99.99.2","accessKey":")" +
        std::string(kAccessKey) + R"("}})";
    ConfigWarnings plainWarnings;
    auto plainConfig = parseAndValidateConfig(plain, plainWarnings);
    PHPVPN_REQUIRE_OK(plainConfig);
    PHPVPN_CHECK(plainWarnings.usesPlainHttp);
    PHPVPN_CHECK(plainWarnings.serverSecretPredictable);
}

PHPVPN_TEST(config, rejects_an_unknown_or_duplicate_member) {
    ConfigWarnings warnings;

    const std::string unknownTop =
        std::string(R"({"masterSetup":{"serverUrl":"https://a/b","serverSecret":"s",)"
                    R"("virtualIp":"10.99.99.2","accessKey":")") +
        kAccessKey + R"("},"unexpected":1})";
    PHPVPN_CHECK(!parseAndValidateConfig(unknownTop, warnings).ok());

    const std::string unknownMaster =
        std::string(R"({"masterSetup":{"serverUrl":"https://a/b","serverSecret":"s",)"
                    R"("virtualIp":"10.99.99.2","accessKey":")") +
        kAccessKey + R"(","extra":true}})";
    PHPVPN_CHECK(!parseAndValidateConfig(unknownMaster, warnings).ok());

    const std::string duplicate =
        std::string(R"({"masterSetup":{"serverUrl":"https://a/b","serverUrl":"https://a/c",)"
                    R"("serverSecret":"s","virtualIp":"10.99.99.2","accessKey":")") +
        kAccessKey + R"("}})";
    PHPVPN_CHECK(!parseAndValidateConfig(duplicate, warnings).ok());
}

PHPVPN_TEST(config, rejects_a_malformed_server_url) {
    ConfigWarnings warnings;
    const char* invalid[] = {
        "ftp://relay.example/vpn.php",
        "https://relay.example/vpn.php?action=data",
        "https://relay.example/vpn.php#fragment",
        "https://user:pass@relay.example/vpn.php",
        "https:///vpn.php",
        "https://relay.example:0/vpn.php",
        "https://relay.example:70000/vpn.php",
        "relay.example/vpn.php",
        "",
    };

    for (const char* url : invalid) {
        const std::string document =
            std::string(R"({"masterSetup":{"serverUrl":")") + url +
            R"(","serverSecret":"s","virtualIp":"10.99.99.2","accessKey":")" + kAccessKey +
            R"("}})";
        PHPVPN_CHECK(!parseAndValidateConfig(document, warnings).ok());
    }
}

PHPVPN_TEST(config, parses_a_valid_server_url) {
    auto secure = parseServerUrl("https://relay.example/vpn.php");
    PHPVPN_REQUIRE_OK(secure);
    PHPVPN_CHECK_EQ(secure.value().port, std::uint16_t(443));

    auto plain = parseServerUrl("http://relay.example/vpn.php");
    PHPVPN_REQUIRE_OK(plain);
    PHPVPN_CHECK_EQ(plain.value().port, std::uint16_t(80));

    auto explicitPort = parseServerUrl("http://127.0.0.1:8080/relay.php");
    PHPVPN_REQUIRE_OK(explicitPort);
    PHPVPN_CHECK_EQ(explicitPort.value().port, std::uint16_t(8080));
    PHPVPN_CHECK_EQ(explicitPort.value().host, std::string("127.0.0.1"));
    PHPVPN_CHECK_EQ(explicitPort.value().path, std::string("/relay.php"));
}

PHPVPN_TEST(config, rejects_an_invalid_virtual_address) {
    ConfigWarnings warnings;
    const char* invalid[] = {"10.99.99.0", "10.99.99.1", "10.99.99.255", "10.99.98.2", "not-an-ip"};

    for (const char* address : invalid) {
        const std::string document =
            std::string(R"({"masterSetup":{"serverUrl":"https://a/b","serverSecret":"s",)"
                        R"("vpnCidr":"10.99.99.0/24","virtualIp":")") +
            address + R"(","accessKey":")" + kAccessKey + R"("}})";
        PHPVPN_CHECK(!parseAndValidateConfig(document, warnings).ok());
    }
}

PHPVPN_TEST(config, rejects_a_key_that_is_not_64_lowercase_hex_characters) {
    ConfigWarnings warnings;
    const char* invalid[] = {
        "0102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f2",
        "0102030405060708090A0B0C0D0E0F101112131415161718191a1b1c1d1e1f20",
        "0102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f20ff",
        "",
    };

    for (const char* key : invalid) {
        const std::string document =
            std::string(R"({"masterSetup":{"serverUrl":"https://a/b","serverSecret":"s",)"
                        R"("virtualIp":"10.99.99.2","accessKey":")") +
            key + R"("}})";
        PHPVPN_CHECK(!parseAndValidateConfig(document, warnings).ok());
    }
}

PHPVPN_TEST(config, rejects_a_contract_that_targets_the_local_address_or_a_reserved_one) {
    ConfigWarnings warnings;

    const std::string self =
        documentWith(std::string(), std::string(),
                     std::string(R"({"10.99.99.2":{"payloadKey":")") + kPayloadKey + R"("}})");
    PHPVPN_CHECK(!parseAndValidateConfig(self, warnings).ok());

    const std::string gateway =
        documentWith(std::string(), std::string(),
                     std::string(R"({"10.99.99.1":{"payloadKey":")") + kPayloadKey + R"("}})");
    PHPVPN_CHECK(!parseAndValidateConfig(gateway, warnings).ok());

    const std::string outside =
        documentWith(std::string(), std::string(),
                     std::string(R"({"10.99.98.3":{"payloadKey":")") + kPayloadKey + R"("}})");
    PHPVPN_CHECK(!parseAndValidateConfig(outside, warnings).ok());
}

PHPVPN_TEST(config, rejects_a_queue_budget_that_cannot_cover_the_reservation) {
    ConfigWarnings warnings;

    // 252 possible peers times 128 KiB needs at least 33030144 bytes per side.
    const std::string tooSmall =
        documentWith(std::string(),
                     R"({"maxOutboundQueueBytes":1048576,"minPerPeerQueueBytes":131072})");
    auto result = parseAndValidateConfig(tooSmall, warnings);
    PHPVPN_REQUIRE_ERROR(result, ErrorCode::config_queue_budget);

    const std::string sufficient =
        documentWith(std::string(),
                     R"({"maxOutboundQueueBytes":33030144,"maxInboundQueueBytes":33030144,)"
                     R"("minPerPeerQueueBytes":131072})");
    PHPVPN_CHECK(parseAndValidateConfig(sufficient, warnings).ok());
}

PHPVPN_TEST(config, rejects_a_non_positive_runtime_limit) {
    ConfigWarnings warnings;
    PHPVPN_CHECK(!parseAndValidateConfig(documentWith(std::string(),
                                                      R"({"responseTimeoutSeconds":0})"),
                                          warnings)
                      .ok());
    PHPVPN_CHECK(!parseAndValidateConfig(documentWith(std::string(),
                                                      R"({"responseTimeoutSeconds":-1})"),
                                          warnings)
                      .ok());
    PHPVPN_CHECK(!parseAndValidateConfig(documentWith(std::string(),
                                                      R"({"responseTimeoutSeconds":"30"})"),
                                          warnings)
                      .ok());
}

PHPVPN_TEST(config, runtime_boundaries_in_full_and_every_repair_mode) {
    // Explicit numbers pin the documented contract independently of its constants.
    for (const bool timeout : {false, true}) {
        for (const bool exceeds : {false, true}) {
            const std::string runtime = timeout
                ? "{\"responseTimeoutSeconds\":" + std::to_string(2147483 + exceeds) + "}"
                : "{\"maxResponseBodyBytes\":4096,\"maxNdjsonLineBytes\":" +
                    std::to_string(4096 + exceeds) + "}";
            for (unsigned missing = 0; missing < 4; ++missing) {
                auto document = documentWith({}, runtime);
                for (const auto& field : {
                    std::pair{1U, std::string(R"("virtualIp":"10.99.99.2",)")},
                    std::pair{2U, std::string(R"(,"accessKey":")") + kAccessKey + "\""}}) {
                    if (missing & field.first)
                        document.erase(document.find(field.second), field.second.size());
                }
                if (missing == 0) {
                    ConfigWarnings warnings;
                    const auto full = parseAndValidateConfig(document, warnings);
                    if (exceeds) {
                        PHPVPN_REQUIRE_ERROR(full, ErrorCode::config_invalid_limit);
                    } else {
                        PHPVPN_REQUIRE_OK(full);
                        PHPVPN_CHECK_EQ(full.value()->runtime.responseTimeoutSeconds,
                            timeout ? 2147483 : 30);
                        if (!timeout) {
                            PHPVPN_CHECK_EQ(full.value()->runtime.maxResponseBodyBytes, 4096);
                            PHPVPN_CHECK_EQ(full.value()->runtime.maxNdjsonLineBytes, 4096);
                        }
                    }
                }
                const auto repair = analyzeWizardDocument(document);
                if (exceeds) {
                    PHPVPN_REQUIRE_ERROR(repair, ErrorCode::config_invalid_limit);
                } else {
                    PHPVPN_REQUIRE_OK(repair);
                    PHPVPN_CHECK_EQ(repair->hasVirtualIp, (missing & 1) == 0);
                    PHPVPN_CHECK_EQ(repair->hasAccessKey, (missing & 2) == 0);
                    PHPVPN_CHECK_EQ(repair->context.runtime.responseTimeoutSeconds,
                        timeout ? 2147483 : 30);
                    if (!timeout) {
                        PHPVPN_CHECK_EQ(repair->context.runtime.maxResponseBodyBytes, 4096);
                        PHPVPN_CHECK_EQ(repair->context.runtime.maxNdjsonLineBytes, 4096);
                    }
                }
            }
        }
    }
}

PHPVPN_TEST(config, rejects_an_invalid_prefix_length) {
    ConfigWarnings warnings;
    const char* invalid[] = {"10.99.99.0/8", "10.99.99.0/30", "10.99.99.0/33", "10.99.99.1/24"};

    for (const char* cidr : invalid) {
        const std::string document =
            std::string(R"({"masterSetup":{"serverUrl":"https://a/b","serverSecret":"s",)"
                        R"("vpnCidr":")") +
            cidr + R"(","virtualIp":"10.99.99.2","accessKey":")" + kAccessKey + R"("}})";
        PHPVPN_CHECK(!parseAndValidateConfig(document, warnings).ok());
    }
}

PHPVPN_TEST(config, cidr_range_is_global_in_full_and_every_repair_mode) {
    // A canonical network for every IPv4 prefix. Missing identity must never
    // turn an unsupported prefix into a repairable document.
    for (int prefix = 0; prefix <= 33; ++prefix) {
        for (unsigned missing = 0; missing < 4; ++missing) {
            const auto document = std::string(R"({"masterSetup":{"serverUrl":"https://a/b","serverSecret":"s","vpnCidr":"0.0.0.0/)") +
                std::to_string(prefix) + "\"" +
                ((missing & 1) ? "" : R"(,"virtualIp":"0.0.0.2")") +
                ((missing & 2) ? "" : std::string(R"(,"accessKey":")") + kAccessKey + "\"") + "}}";
            ConfigWarnings warnings;
            const auto full = parseAndValidateConfig(document, warnings);
            const auto repair = analyzeWizardDocument(document);
            if (prefix >= 24 && prefix <= 29) {
                PHPVPN_REQUIRE_OK(repair);
                PHPVPN_CHECK_EQ(full.ok(), missing == 0);
                PHPVPN_CHECK_EQ(repair->hasVirtualIp, (missing & 1) == 0);
                PHPVPN_CHECK_EQ(repair->hasAccessKey, (missing & 2) == 0);
            } else {
                PHPVPN_REQUIRE_ERROR(full, ErrorCode::config_invalid_cidr);
                PHPVPN_REQUIRE_ERROR(repair, ErrorCode::config_invalid_cidr);
                if (!full && !repair) {
                    PHPVPN_CHECK_EQ(full.error().detail,
                        "vpnCidr must be a canonical IPv4 network with a prefix length of 24 to 29");
                    PHPVPN_CHECK_EQ(repair.error().detail, full.error().detail);
                }
            }
        }
    }
}

PHPVPN_TEST(config, cidr_queue_reservations_check_both_sides_and_overflow) {
    for (const int prefix : {24, 29}) {
        const std::int64_t peers = prefix == 24 ? 252 : 4;
        const auto make = [&](std::int64_t outbound, std::int64_t inbound, std::int64_t perPeer) {
            auto text = documentWith({}, "{\"maxOutboundQueueBytes\":" + std::to_string(outbound) +
                ",\"maxInboundQueueBytes\":" + std::to_string(inbound) +
                ",\"minPerPeerQueueBytes\":" + std::to_string(perPeer) + "}");
            text.replace(text.find("/24"), 3, "/" + std::to_string(prefix));
            return text;
        };
        const auto maximum = (std::numeric_limits<std::int64_t>::max)();
        const auto perPeer = maximum / peers;
        const auto required = peers * perPeer;
        ConfigWarnings warnings;
        const auto exact = parseAndValidateConfig(make(required, required, perPeer), warnings);
        PHPVPN_REQUIRE_OK(exact);
        PHPVPN_CHECK_EQ(exact.value()->maxRemotePeers(), static_cast<std::uint32_t>(peers));
        for (const auto& rejected : {make(required - 1, required, perPeer),
                make(required, required - 1, perPeer), make(maximum, maximum, perPeer + 1)}) {
            PHPVPN_REQUIRE_ERROR(parseAndValidateConfig(rejected, warnings), ErrorCode::config_queue_budget);
            PHPVPN_REQUIRE_ERROR(analyzeWizardDocument(rejected), ErrorCode::config_queue_budget);
        }
    }
}

PHPVPN_TEST(config, cidr_edges_validate_local_and_remote_addresses_in_full_and_repair) {
    for (const int prefix : {24, 29}) {
        const auto broadcast = prefix == 24 ? 255 : 7;
        for (const auto octet : {0, 1, 2, 3, broadcast - 1, broadcast}) {
            for (const bool remote : {false, true}) {
                auto text = validDocument();
                text.replace(text.find("/24"), 3, "/" + std::to_string(prefix));
                const auto replaced = remote ? "10.99.99.3" : "10.99.99.2";
                text.replace(text.find(replaced), std::string_view(replaced).size(), "10.99.99." + std::to_string(octet));
                const bool valid = octet >= 2 && octet < broadcast && octet != (remote ? 2 : 3);
                ConfigWarnings warnings;
                const auto full = parseAndValidateConfig(text, warnings);
                const auto repair = analyzeWizardDocument(text);
                PHPVPN_CHECK_EQ(full.ok(), valid);
                PHPVPN_CHECK_EQ(repair.ok(), valid);
                if (!valid) {
                    PHPVPN_REQUIRE_ERROR(full, ErrorCode::config_invalid_address);
                    PHPVPN_REQUIRE_ERROR(repair, ErrorCode::config_invalid_address);
                }
            }
        }
    }
}

PHPVPN_TEST(config, keeps_the_exact_source_document) {
    ConfigWarnings warnings;
    const std::string document = validDocument();
    auto config = parseAndValidateConfig(document, warnings);
    PHPVPN_REQUIRE_OK(config);
    // `get-config` has to return the bytes the administrator stored.
    PHPVPN_CHECK_EQ(config.value()->rawJson, document);
}

PHPVPN_TEST(config, accepts_a_utf8_byte_order_mark_and_drops_it) {
    ConfigWarnings warnings;
    const std::string document = validDocument();
    auto config = parseAndValidateConfig("\xEF\xBB\xBF" + document, warnings);
    PHPVPN_REQUIRE_OK(config);
    // A Windows editor may add the mark, but it must not reach `get-config`.
    PHPVPN_CHECK_EQ(config.value()->rawJson, document);
}

PHPVPN_TEST(config, rejects_a_document_that_is_not_json) {
    ConfigWarnings warnings;
    PHPVPN_CHECK(!parseAndValidateConfig("", warnings).ok());
    PHPVPN_CHECK(!parseAndValidateConfig("[]", warnings).ok());
    PHPVPN_CHECK(!parseAndValidateConfig("{", warnings).ok());
    PHPVPN_CHECK(!parseAndValidateConfig("{}", warnings).ok());
    PHPVPN_CHECK(!parseAndValidateConfig(validDocument() + "x", warnings).ok());
}

}  // namespace phpvpn
