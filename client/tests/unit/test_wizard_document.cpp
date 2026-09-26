#include "harness.h"
#include "one_contract_fixture.h"
#include "core/config/config_defaults.h"
#include "core/wizard/wizard_defaults.h"
#include <algorithm>

namespace phpvpn {
namespace {
std::string withoutIdentity(bool ip, bool key) {
    auto parsed = Json::parse(test::oneContractDocument());
    auto& dom = parsed.value();
    for (auto& member : dom.objectValue()) if (member.first == "masterSetup") {
        auto& fields = member.second.objectValue();
        std::erase_if(fields, [&](const auto& field) { return (ip && field.first == "virtualIp") || (key && field.first == "accessKey"); });
    }
    return Json::serialize(dom);
}
}
PHPVPN_TEST(wizard_document, OnlyMissingIdentityIsRepairable) {
    for (const auto missing : {std::pair{true, false}, std::pair{false, true}, std::pair{true, true}}) {
        const auto text = withoutIdentity(missing.first, missing.second);
        auto draft = analyzeWizardDocument(text);
        PHPVPN_REQUIRE_OK(draft);
        PHPVPN_CHECK_EQ(draft->hasVirtualIp, !missing.first);
        PHPVPN_CHECK_EQ(draft->hasAccessKey, !missing.second);
        ConfigWarnings warnings;
        PHPVPN_CHECK(!parseAndValidateConfig(text, warnings));
        std::string invalid = text;
        invalid.insert(1, "\"unexpected\":0,");
        PHPVPN_CHECK(!analyzeWizardDocument(invalid));
    }
    for (const auto& [field, value] : JsonObject{
            {"trustAnyTlsCertificate", JsonValue::makeString("bad")},
            {"virtualIp", JsonValue::makeInteger(2)},
            {"virtualIp", JsonValue::makeString("10.99.99.3")},
            {"accessKey", JsonValue::makeString("not-a-key")},
            {"serverSecret", JsonValue::makeNull()},
            {"vpnCidr", JsonValue::makeString("10.99.99.1/24")}}) {
        auto parsed = Json::parse(withoutIdentity(true, true));
        for (auto& member : parsed->objectValue()) if (member.first == "masterSetup") {
            std::erase_if(member.second.objectValue(), [&](const auto& item) { return item.first == field; });
            member.second.objectValue().emplace_back(field, value);
        }
        PHPVPN_CHECK(!analyzeWizardDocument(Json::serialize(parsed.value())));
    }
    auto parsed = Json::parse(withoutIdentity(true, true));
    parsed->objectValue().emplace_back("runtime", JsonValue::makeObject({
        {"maxOutboundQueueBytes", JsonValue::makeInteger(1)}}));
    PHPVPN_CHECK_EQ(analyzeWizardDocument(Json::serialize(parsed.value())).code(), ErrorCode::config_queue_budget);
}
PHPVPN_TEST(wizard_document, PatchPreservesUnshownValuesAndOmission) {
    auto text = withoutIdentity(true, false);
    const auto trust = text.find(":true");
    text.replace(trust, 5, ":false");
    auto draft = analyzeWizardDocument(text);
    PHPVPN_REQUIRE_OK(draft);
    auto fields = draft->fields;
    fields.virtualIp = "10.99.99.10";
    fields.serverSecret = std::string("a\0b", 3);
    auto crypto = createPlatformCryptoProvider();
    PHPVPN_REQUIRE_OK(crypto);
    auto output = buildWizardDocument(*crypto.value(), draft.value(), WizardMode::repair, fields, false);
    PHPVPN_REQUIRE_OK(output);
    auto after = analyzeWizardDocument(output.value());
    PHPVPN_REQUIRE_OK(after);
    PHPVPN_CHECK(!after->context.trustAnyTlsCertificate);
    PHPVPN_CHECK_EQ(after->fields.serverSecret, fields.serverSecret);
    PHPVPN_CHECK_EQ(after->contractPeers, draft->contractPeers);
    PHPVPN_CHECK(after->dom.find("runtime") == nullptr);
    fields.virtualIp = "10.99.99.3";
    PHPVPN_CHECK_EQ(validateWizardIdentity(draft.value(), fields).code(), ErrorCode::wizard_contract_self_conflict);
}

PHPVPN_TEST(wizard_document, startup_preference_preserved_defaulted_and_ordered) {
    auto crypto = createPlatformCryptoProvider();
    PHPVPN_REQUIRE_OK(crypto);
    for (int preference : {-1, 0, 1}) {
        auto dom = Json::parse(makeDefaultEditorDocument()).value();
        for (auto& [name, master] : dom.objectValue()) if (name == "masterSetup") {
            std::erase_if(master.objectValue(), [](const auto& item) { return item.first == "autoConnectOnStartup"; });
            if (preference >= 0) master.objectValue().emplace_back("autoConnectOnStartup", JsonValue::makeBool(preference == 1));
        }
        auto original = analyzeWizardDocument(Json::serialize(dom));
        PHPVPN_REQUIRE_OK(original);
        for (const auto mode : {WizardMode::edit, WizardMode::repair}) {
            auto output = buildWizardDocument(*crypto.value(), *original, mode, original->fields, false);
            PHPVPN_REQUIRE_OK(output);
            auto parsed = Json::parse(*output);
            const auto& members = parsed->find("masterSetup")->objectValue();
            PHPVPN_CHECK_EQ(members.back().first, std::string("autoConnectOnStartup"));
            PHPVPN_CHECK_EQ(members[members.size() - 2].first, std::string("useDefaultAccessKeyInWizard"));
            PHPVPN_CHECK_EQ(members.back().second.boolValue(), preference == 1);
        }
    }
    auto reset = makeNewWizardDocument();
    PHPVPN_CHECK(!readAutoConnectOnStartup(reset.dom).value());
    reset.fields.virtualIp = "10.99.99.2";
    reset.fields.accessKey = std::string(64, '2');
    for (const auto mode : {WizardMode::create, WizardMode::reset}) {
        auto output = buildWizardDocument(*crypto.value(), reset, mode, reset.fields, true);
        PHPVPN_REQUIRE_OK(output);
        auto dom = Json::parse(*output);
        PHPVPN_CHECK_EQ(dom->find("masterSetup")->objectValue().back().first, std::string("autoConnectOnStartup"));
        PHPVPN_CHECK(!readAutoConnectOnStartup(*dom).value());
    }
}
PHPVPN_TEST(wizard_defaults, AccessKeyAndIndependentPayloadVectors) {
    for (unsigned octet : {2U, 10U, 15U, 17U}) {
        const auto key = defaultAccessKey("10.99.99." + std::to_string(octet));
        PHPVPN_CHECK_EQ(key.size(), std::size_t(64));
        const auto pair = octet == 2 ? "02" : octet == 10 ? "0a" : octet == 15 ? "0f" : "11";
        for (std::size_t i = 0; i < 64; i += 2) PHPVPN_CHECK_EQ(key.substr(i, 2), std::string(pair));
    }
    PHPVPN_CHECK(defaultAccessKey("10.99.99.").empty());
    auto crypto = createPlatformCryptoProvider();
    PHPVPN_REQUIRE_OK(crypto);
    const auto key = defaultPayloadKey(*crypto.value(), *Ipv4Address::parse("10.99.99.10"), *Ipv4Address::parse("10.99.99.2"));
    PHPVPN_REQUIRE_OK(key);
    PHPVPN_CHECK_EQ(key.value(), std::string("f41f0ed508411aef592228e9d73ae137a4d31933f038160c04ae1e8d70597ebe"));
    const auto other = defaultPayloadKey(*crypto.value(), *Ipv4Address::parse("10.20.30.2"), *Ipv4Address::parse("10.20.30.20"));
    PHPVPN_REQUIRE_OK(other);
    PHPVPN_CHECK_EQ(other.value(), std::string("3e0d09769e22926032c4dbf961e2d6bc7caa9c6f326af51362c827849358b265"));
}
PHPVPN_TEST(wizard_defaults, ContractsAndIpSuggestion) {
    auto crypto = createPlatformCryptoProvider();
    PHPVPN_REQUIRE_OK(crypto);
    auto draft = makeNewWizardDocument();
    auto fields = draft.fields;
    fields.serverUrl = "http://127.0.0.1/phpvpn.php";
    for (const auto ip : {"10.99.99.2", "10.99.99.17"}) {
        fields.virtualIp = ip;
        fields.accessKey = defaultAccessKey(ip);
        auto output = buildWizardDocument(*crypto.value(), draft, WizardMode::create, fields, true);
        PHPVPN_REQUIRE_OK(output);
        auto parsed = analyzeWizardDocument(output.value());
        PHPVPN_REQUIRE_OK(parsed);
        PHPVPN_CHECK_EQ(parsed->contractPeers.size(), fields.virtualIp == "10.99.99.2" ? std::size_t(13) : std::size_t(14));
    }
    std::vector<SeenClient> clients;
    for (unsigned i = 2; i <= 15; ++i) clients.push_back({*Ipv4Address::parse("10.99.99." + std::to_string(i)), 10, {}});
    clients[8].lastSeenMs = 5;
    PHPVPN_CHECK_EQ(suggestWizardIp(draft, clients)->toString(), std::string("10.99.99.10"));
    clients.erase(clients.begin());
    PHPVPN_CHECK_EQ(suggestWizardIp(draft, clients)->toString(), std::string("10.99.99.2"));
    draft.context.vpnCidr = *Ipv4Cidr::parse("10.20.30.0/24");
    PHPVPN_CHECK(!suggestWizardIp(draft, clients));
    PHPVPN_CHECK(supportsDefaultContracts(draft.context.vpnCidr));
    draft.context.vpnCidr = *Ipv4Cidr::parse("10.20.30.0/25");
    PHPVPN_CHECK(!supportsDefaultContracts(draft.context.vpnCidr));
}
PHPVPN_TEST(wizard_document, StrictEncodingAndReadBound) {
    for (const auto invalid : {"\xc0\xaf", "\xe2\x82", "\xed\xa0\x80", "\xf4\x90\x80\x80", "\xff\xfe"})
        PHPVPN_CHECK_EQ(normalizeConfigText(invalid).code(), ErrorCode::config_encoding_error);
    PHPVPN_CHECK_EQ(normalizeConfigText(std::string(kMaxConfigFileBytes + 1, ' ')).code(), ErrorCode::config_too_large);
    PHPVPN_REQUIRE_OK(normalizeConfigText(std::string(kMaxConfigFileBytes, ' ')));
    const auto text = normalizeConfigText("\xef\xbb\xbf{\"text\":\"Příliš žluťoučký 🐎\"}");
    PHPVPN_REQUIRE_OK(text);
    PHPVPN_CHECK(text->starts_with("{"));
    const auto repeated = normalizeConfigText("\xef\xbb\xbf\xef\xbb\xbf{}");
    PHPVPN_REQUIRE_OK(repeated);
    PHPVPN_CHECK(repeated->starts_with("\xef\xbb\xbf"));
    PHPVPN_CHECK(!Json::parse(repeated.value()));
}

PHPVPN_TEST(wizard_document, cidr_finish_revalidates_the_actual_dom_in_full_and_repair) {
    auto crypto = createPlatformCryptoProvider();
    PHPVPN_REQUIRE_OK(crypto);
    for (const auto mode : {WizardMode::edit, WizardMode::repair}) {
        const auto text = mode == WizardMode::repair ? withoutIdentity(true, true) : makeDefaultEditorDocument();
        const auto original = analyzeWizardDocument(text);
        PHPVPN_REQUIRE_OK(original);
        auto fields = original->fields;
        fields.virtualIp = "10.99.99.2";
        fields.accessKey = defaultAccessKey(fields.virtualIp);
        for (const auto prefix : {16, 17, 18, 19, 20, 21, 22, 23, 24, 29, 30, 31, 32}) {
            auto draft = *original;
            // Deliberately keep a valid typed context: the final JSON must be
            // checked even if an internal producer patches an invalid CIDR.
            for (auto& root : draft.dom.objectValue()) if (root.first == "masterSetup") {
                for (auto& member : root.second.objectValue()) if (member.first == "vpnCidr") {
                    const auto network = prefix >= 24 ? "10.99.99.0/" : "10.99.0.0/";
                    member.second = JsonValue::makeString(network + std::to_string(prefix));
                }
            }
            const auto built = buildWizardDocument(*crypto.value(), draft, mode, fields, false);
            if (prefix == 24 || prefix == 29) {
                PHPVPN_REQUIRE_OK(built);
                const auto parsed = analyzeWizardDocument(*built);
                PHPVPN_REQUIRE_OK(parsed);
                PHPVPN_CHECK_EQ(parsed->context.vpnCidr.prefixLength(), prefix);
                PHPVPN_CHECK_EQ(parsed->contractPeers, original->contractPeers);
            } else PHPVPN_REQUIRE_ERROR(built, ErrorCode::config_invalid_cidr);
        }
    }
}
}
namespace phpvpn {
PHPVPN_TEST(wizard_document, flow_queue_defaults_remain_omitted_and_explicit_runtime_survives_edit) {
    auto crypto=createPlatformCryptoProvider();PHPVPN_REQUIRE_OK(crypto);
    for(const bool explicitRuntime:{false,true}) {
        auto source=Json::parse(makeDefaultEditorDocument()).value();
        if(explicitRuntime) source.objectValue().emplace_back("runtime",JsonValue::makeObject({
            {"minOutboundPerPeerQueueBytes",JsonValue::makeInteger(17)},
            {"minInboundPerPeerQueueBytes",JsonValue::makeInteger(31)},
            {"maxQueuedPacketAgeMs",JsonValue::makeInteger(1234)}}));
        auto draft=analyzeWizardDocument(Json::serialize(source));PHPVPN_REQUIRE_OK(draft);
        auto fields=draft->fields;fields.virtualIp="10.99.99.4";
        auto output=buildWizardDocument(**crypto,*draft,WizardMode::edit,fields,false);PHPVPN_REQUIRE_OK(output);
        auto after=Json::parse(*output);PHPVPN_REQUIRE_OK(after);
        PHPVPN_CHECK_EQ(after->find("runtime")!=nullptr,explicitRuntime);
        if(explicitRuntime) PHPVPN_CHECK_EQ(Json::serialize(*after->find("runtime")),Json::serialize(*source.find("runtime")));
        auto context=analyzeWizardDocument(*output);PHPVPN_REQUIRE_OK(context);
        PHPVPN_CHECK_EQ(context->context.runtime.maxQueuedPacketAgeMs,std::int64_t(explicitRuntime?1234:5000));
    }
}
} // namespace phpvpn
