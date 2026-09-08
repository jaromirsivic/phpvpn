#include "contracts_fixture.h"
#include <limits>
#include <type_traits>

namespace phpvpn {
using namespace contracts_test;
static_assert(!std::is_copy_constructible_v<ContractsModel>);
static_assert(!std::is_copy_constructible_v<ContractsDocument>);
static_assert(!std::is_copy_constructible_v<ContractsCandidate>);
PHPVPN_TEST(ContractsDocument, absent_present_empty_and_unchecked_removal) {
    Crypto crypto;
    for (const bool present : {false, true}) {
        auto source = document("10.99.99.2", "10.99.99.0/24", present);
        auto created = ContractsModel::create(crypto, source);
        PHPVPN_REQUIRE_OK(created);
        auto& model = *created.value();
        auto candidate = model.prepareSave(envelopeSize);
        PHPVPN_REQUIRE_OK(candidate);
        const auto* contracts = candidate.value()->dom().find("communicationContracts");
        PHPVPN_CHECK_EQ(contracts != nullptr, present);
        if (contracts) PHPVPN_CHECK(contracts->isObject() && contracts->objectValue().empty());
        PHPVPN_CHECK(model.resumeEditing(model.saveGeneration()));
        const auto peer = ip("10.99.99.10");
        PHPVPN_REQUIRE_OK(model.setEnabled(peer, true));
        auto enabled = model.prepareSave(envelopeSize);
        PHPVPN_REQUIRE_OK(enabled);
        contracts = enabled.value()->dom().find("communicationContracts");
        PHPVPN_CHECK_EQ(contracts->objectValue().size(), 1u);
        PHPVPN_CHECK(contracts->find("10.99.99.10") != nullptr);
        PHPVPN_CHECK(contracts->find("10.99.99.2_10.99.99.10") == nullptr);
    }
    auto created = ContractsModel::create(crypto, makeDefaultEditorDocument());
    PHPVPN_REQUIRE_OK(created);
    auto& model = *created.value();
    PHPVPN_REQUIRE_OK(model.setEnabled(ip("10.99.99.3"), false));
    PHPVPN_REQUIRE_OK(model.setKey(ip("10.99.99.3"), "INVALID"));
    auto removed = model.prepareSave(envelopeSize);
    PHPVPN_REQUIRE_OK(removed);
    PHPVPN_CHECK(removed.value()->dom().find("communicationContracts")->objectValue().empty());
    PHPVPN_CHECK(removed.value()->text().find("INVALID") == std::string_view::npos);
}
PHPVPN_TEST(ContractsDocument, preserves_all_unrelated_values_types_and_optional_presence) {
    Crypto crypto;
    // Every combination of absent/present optional master members and runtime.
    constexpr std::string_view optional[] = {"vpnCidr", "trustAnyTlsCertificate",
        "useDefaultServerSecretInWizard", "autoDetectIpAddressInWizard", "useDefaultAccessKeyInWizard"};
    for (unsigned mask = 0; mask < 64; ++mask) {
        auto dom = Json::parse(document()).value();
        for (auto& [name, master] : dom.objectValue()) if (name == "masterSetup") {
            for (unsigned bit = 0; bit < 5; ++bit) {
                if ((mask & (1u << bit)) == 0)
                    std::erase_if(master.objectValue(), [&](const auto& member) { return member.first == optional[bit]; });
                else if (bit != 0) for (auto& [field, value] : master.objectValue())
                    if (field == optional[bit]) value = JsonValue::makeBool(false);
            }
            for (auto& [field, value] : master.objectValue()) if (field == "serverSecret")
                value = JsonValue::makeString(std::string("secret\0\n\"\\", 10) + "\xc3\xa9");
        }
        if (mask & 32) dom.objectValue().emplace_back("runtime", JsonValue::makeObject({
            {"timeSyncIntervalSeconds", JsonValue::makeInteger(123)},
            {"maxOutboundQueueBytes", JsonValue::makeInteger(67108864)},
            {"responseTimeoutSeconds", JsonValue::makeInteger(11)}}));
        const auto beforeMaster = Json::serialize(*dom.find("masterSetup"));
        const auto beforeRuntime = dom.find("runtime") ? Json::serialize(*dom.find("runtime")) : "absent";
        const auto original = Json::serialize(dom);
        auto created = ContractsModel::create(crypto, original);
        PHPVPN_REQUIRE_OK(created);
        PHPVPN_REQUIRE_OK(created.value()->setEnabled(ip("10.99.99.254"), true));
        auto candidate = created.value()->prepareSave(envelopeSize);
        PHPVPN_REQUIRE_OK(candidate);
        const auto& patched = candidate.value()->dom();
        PHPVPN_CHECK_EQ(Json::serialize(*patched.find("masterSetup")), beforeMaster);
        PHPVPN_CHECK_EQ(patched.find("runtime") ? Json::serialize(*patched.find("runtime")) : "absent", beforeRuntime);
        ConfigWarnings warnings;
        PHPVPN_REQUIRE_OK(parseAndValidateConfig(candidate.value()->text(), warnings));
    }
}
PHPVPN_TEST(ContractsDocument, complete_snapshot_rejects_omissions_duplicates_foreign_self_and_bad_keys) {
    Crypto crypto;
    auto source = document("172.16.2.2", "172.16.2.0/29");
    auto parsed = ContractsDocument::parse(source);
    auto created = ContractsModel::create(crypto, source);
    PHPVPN_REQUIRE_OK(parsed); PHPVPN_REQUIRE_OK(created);
    auto rows = edits(*created.value());
    std::reverse(rows.begin(), rows.end());
    PHPVPN_REQUIRE_OK(parsed.value()->buildCandidate(rows, envelopeSize));
    auto bad = rows; bad.pop_back();
    PHPVPN_REQUIRE_ERROR(parsed.value()->buildCandidate(bad, envelopeSize), ErrorCode::invalid_argument);
    bad = rows; bad.push_back(rows.front());
    PHPVPN_REQUIRE_ERROR(parsed.value()->buildCandidate(bad, envelopeSize), ErrorCode::invalid_argument);
    bad = rows; bad[0].remoteIp = bad[1].remoteIp;
    PHPVPN_REQUIRE_ERROR(parsed.value()->buildCandidate(bad, envelopeSize), ErrorCode::invalid_argument);
    for (const auto peer : {"172.16.2.2", "172.16.2.0", "172.16.2.1", "172.16.2.7", "172.16.3.2"}) {
        bad = rows; bad[0].remoteIp = ip(peer);
        PHPVPN_REQUIRE_ERROR(parsed.value()->buildCandidate(bad, envelopeSize), ErrorCode::config_invalid_address);
    }
    bad = rows; bad[0].keyText = "BAD";
    PHPVPN_REQUIRE_OK(parsed.value()->buildCandidate(bad, envelopeSize));
    bad[0].enabled = true;
    PHPVPN_REQUIRE_ERROR(parsed.value()->buildCandidate(bad, envelopeSize), ErrorCode::config_invalid_key);
    const std::string over(kMaxContractKeyTextBytes, 'a');
    bad = rows; bad[0].keyText = over;
    PHPVPN_REQUIRE_ERROR(parsed.value()->buildCandidate(bad, envelopeSize), ErrorCode::config_too_large);
    PHPVPN_CHECK_EQ(Json::serialize(parsed.value()->dom()), source);
}
PHPVPN_TEST(ContractsDocument, strict_schema_full_validation_encoding_bom_and_size_at_open) {
    for (const auto bad : {"{}", "{", "{\"masterSetup\":null}", "{\"masterSetup\":{},\"masterSetup\":{}}"})
        PHPVPN_CHECK(!ContractsDocument::parse(bad));
    auto text = document();
    text.insert(1, "\"unknown\":true,");
    PHPVPN_REQUIRE_ERROR(ContractsDocument::parse(text), ErrorCode::config_invalid_value);
    auto dom = Json::parse(document()).value();
    dom.objectValue().emplace_back("runtime", JsonValue::makeObject({{"minPerPeerQueueBytes", JsonValue::makeInteger(67108864)}}));
    PHPVPN_REQUIRE_ERROR(ContractsDocument::parse(Json::serialize(dom)), ErrorCode::config_queue_budget);
    PHPVPN_REQUIRE_ERROR(ContractsDocument::parse("\xff"), ErrorCode::config_encoding_error);
    PHPVPN_REQUIRE_OK(ContractsDocument::parse("\xef\xbb\xbf" + document()));
    PHPVPN_REQUIRE_ERROR(ContractsDocument::parse("\xef\xbb\xbf\xef\xbb\xbf" + document()), ErrorCode::config_invalid_json);
    text = document(); text.append(kMaxConfigFileBytes - text.size(), ' ');
    PHPVPN_REQUIRE_OK(ContractsDocument::parse(text));
    PHPVPN_REQUIRE_ERROR(ContractsDocument::parse(text + ' '), ErrorCode::config_too_large);
}
PHPVPN_TEST(ContractsDocument, result_document_limit_and_actual_encoded_envelope) {
    Crypto crypto;
    auto dom = Json::parse(document()).value();
    auto& master = dom.objectValue().front().second;
    for (auto& [name, value] : master.objectValue()) if (name == "serverSecret") value = JsonValue::makeString("");
    const auto overhead = Json::serialize(dom).size();
    for (auto& [name, value] : master.objectValue()) if (name == "serverSecret")
        value = JsonValue::makeString(std::string(kMaxConfigFileBytes - overhead, 'x'));
    const auto exactText = Json::serialize(dom);
    PHPVPN_CHECK_EQ(exactText.size(), kMaxConfigFileBytes);
    auto created = ContractsModel::create(crypto, exactText);
    PHPVPN_REQUIRE_OK(created);
    auto& m = *created.value();
    auto exact = m.prepareSave(envelopeSize);
    PHPVPN_REQUIRE_OK(exact);
    PHPVPN_CHECK_EQ(exact.value()->text().size(), kMaxConfigFileBytes);
    PHPVPN_CHECK(m.resumeEditing(m.saveGeneration()));
    PHPVPN_REQUIRE_OK(m.setEnabled(m.rows().front().remoteIp(), true));
    int called = 0;
    PHPVPN_REQUIRE_ERROR(m.prepareSave([&](std::string_view value) { ++called; return envelopeSize(value); }), ErrorCode::config_too_large);
    PHPVPN_CHECK_EQ(called, 0);
    PHPVPN_CHECK(!m.frozen() && m.dirty());
    PHPVPN_REQUIRE_OK(validateContractsEnvelopeSize(kIpcMaxMessageBytes));
    PHPVPN_REQUIRE_ERROR(validateContractsEnvelopeSize(kIpcMaxMessageBytes + 1), ErrorCode::ipc_message_too_large);
    PHPVPN_REQUIRE_ERROR(validateContractsEnvelopeSize((std::numeric_limits<std::size_t>::max)()), ErrorCode::ipc_message_too_large);
    // Worst escaping is measured through the existing JSON/IPC implementation.
    const std::string controls(kMaxConfigFileBytes, '\x01');
    const auto escaped = envelopeSize(controls);
    PHPVPN_REQUIRE_OK(escaped);
    PHPVPN_CHECK_EQ(*escaped - kMaxConfigFileBytes * 6, *envelopeSize(""));
    PHPVPN_REQUIRE_OK(validateContractsEnvelopeSize(*escaped));
}
PHPVPN_TEST(ContractsDocument, whole_dom_match_ignores_object_order_but_not_presence_values_or_types) {
    Crypto crypto;
    auto created = ContractsModel::create(crypto, document());
    PHPVPN_REQUIRE_OK(created);
    auto built = created.value()->prepareSave(envelopeSize);
    PHPVPN_REQUIRE_OK(built);
    auto reordered = built.value()->dom();
    std::reverse(reordered.objectValue().begin(), reordered.objectValue().end());
    for (auto& member : reordered.objectValue()) if (member.first == "masterSetup")
        std::reverse(member.second.objectValue().begin(), member.second.objectValue().end());
    auto matched = built.value()->matchesDocument(Json::serializePretty(reordered));
    PHPVPN_REQUIRE_OK(matched); PHPVPN_CHECK(*matched);
    reordered.objectValue().emplace_back("communicationContracts", JsonValue::makeObject({}));
    matched = built.value()->matchesDocument(Json::serialize(reordered));
    PHPVPN_REQUIRE_OK(matched); PHPVPN_CHECK(!*matched);
    auto changed = built.value()->dom();
    for (auto& member : changed.objectValue()) if (member.first == "masterSetup")
        for (auto& [name, value] : member.second.objectValue()) if (name == "trustAnyTlsCertificate") value = JsonValue::makeBool(false);
    matched = built.value()->matchesDocument(Json::serialize(changed));
    PHPVPN_REQUIRE_OK(matched); PHPVPN_CHECK(!*matched);
    for (auto& member : changed.objectValue()) if (member.first == "masterSetup")
        for (auto& [name, value] : member.second.objectValue()) if (name == "trustAnyTlsCertificate") value = JsonValue::makeInteger(1);
    PHPVPN_REQUIRE_ERROR(built.value()->matchesDocument(Json::serialize(changed)), ErrorCode::config_invalid_value);
    PHPVPN_CHECK(!built.value()->matchesDocument("{}"));
}
} // namespace phpvpn
