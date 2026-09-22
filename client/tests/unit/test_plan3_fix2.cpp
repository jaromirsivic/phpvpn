#include "one_contract_fixture.h"
#include "harness.h"
#include "fake_config_files.h"
#include "core/config/config_defaults.h"
#include "core/ipc/wizard_ipc.h"
#include "core/wizard/wizard_defaults.h"
#include "core/wizard/wizard_model.h"
#include "core/base/hex.h"
#include <algorithm>

namespace phpvpn {
namespace {
constexpr const char* flags[] = {"useDefaultServerSecretInWizard", "autoDetectIpAddressInWizard", "useDefaultAccessKeyInWizard"};
constexpr char id[] = "00000000-0000-4000-8000-000000000001";
JsonValue& member(JsonValue& object, std::string_view name) {
    for (auto& item : object.objectValue()) if (item.first == name) return item.second;
    throw std::runtime_error("missing fixture member");
}
JsonValue legacyDocument() {
    auto dom = Json::parse(makeDefaultEditorDocument()).value();
    std::erase_if(member(dom, "masterSetup").objectValue(), [](const auto& item) { return item.first.ends_with("InWizard"); });
    return dom;
}
struct Store {
    std::unique_ptr<CryptoProvider> crypto = std::move(createPlatformCryptoProvider()).value();
    FakeConfigFiles* files;
    ConfigStore store;
    Store() : store(*crypto, backend()) {}
    std::unique_ptr<ConfigFiles> backend() {
        auto value = std::make_unique<FakeConfigFiles>(*crypto); files = value.get(); return value;
    }
};
WizardView viewFor(std::string_view text) {
    auto doc = analyzeWizardDocument(text).value();
    WizardView view;
    view.sessionId = view.serviceInstanceId = id;
    view.revisionToken = id;
    view.availability = ConfigAvailability::valid; view.mode = WizardMode::edit;
    view.fields = doc.fields; view.context = doc.context; view.contractCount = doc.contractPeers.size();
    for (auto peer : defaultContractCandidates(doc.context.vpnCidr))
        if (std::find(doc.contractPeers.begin(), doc.contractPeers.end(), peer) != doc.contractPeers.end()) view.existingDefaultContractPeers.push_back(peer);
    return view;
}
void complete(WizardModel& model, const std::optional<std::string>& suggestion = {}) {
    auto start = model.beginCheck(id, 1);
    if (!start) throw std::runtime_error("model checkpoint failed");
    PHPVPN_CHECK(model.acceptStart(id, id, start->sequence));
    WizardOperationResult result;
    result.operationId = id; result.clientAttemptId = id; result.attemptSequence = start->sequence;
    result.kind = start->kind; result.state = WizardOperationState::succeeded;
    result.suggestedIp = suggestion;
    PHPVPN_CHECK(model.applyOperation(result, 2));
}
}

PHPVPN_TEST(fix2, optional_flags_strict_types_and_all_boolean_combinations_leave_runtime_unchanged) {
    ConfigWarnings warnings;
    const auto baseline = parseAndValidateConfig(Json::serialize(legacyDocument()), warnings).value();
    for (unsigned mask = 0; mask < 8; ++mask) {
        auto dom = legacyDocument(); auto& master = member(dom, "masterSetup");
        for (unsigned i = 0; i < 3; ++i) master.objectValue().emplace_back(flags[i], JsonValue::makeBool((mask & (1u << i)) != 0));
        const auto text = Json::serialize(dom);
        auto doc = analyzeWizardDocument(text);
        PHPVPN_REQUIRE_OK(doc);
        PHPVPN_CHECK_EQ(doc->fields.useDefaultServerSecretInWizard, (mask & 1) != 0);
        PHPVPN_CHECK_EQ(doc->fields.autoDetectIpAddressInWizard, (mask & 2) != 0);
        PHPVPN_CHECK_EQ(doc->fields.useDefaultAccessKeyInWizard, (mask & 4) != 0);
        auto runtime = parseAndValidateConfig(text, warnings);
        PHPVPN_REQUIRE_OK(runtime);
        PHPVPN_CHECK_EQ(runtime.value()->virtualIp, baseline->virtualIp);
        PHPVPN_CHECK_EQ(runtime.value()->serverSecret, baseline->serverSecret);
        PHPVPN_CHECK_EQ(toLowercaseHex(runtime.value()->accessKey.span()), toLowercaseHex(baseline->accessKey.span()));
        PHPVPN_CHECK_EQ(runtime.value()->contracts.size(), baseline->contracts.size());
        PHPVPN_CHECK_EQ(runtime.value()->vpnCidr.toString(), baseline->vpnCidr.toString());
    }
    for (auto flag : flags) {
        for (const auto bad : {"null", "0", "1", "\"true\"", "{}", "[]"}) {
            auto dom = legacyDocument();
            member(dom, "masterSetup").objectValue().emplace_back(flag, Json::parse(bad).value());
            PHPVPN_REQUIRE_ERROR(analyzeWizardDocument(Json::serialize(dom)), ErrorCode::config_invalid_value);
            PHPVPN_REQUIRE_ERROR(parseAndValidateConfig(Json::serialize(dom), warnings), ErrorCode::config_invalid_value);
        }
    }
    for (auto flag : flags) {
        auto duplicate = makeDefaultEditorDocument();
        duplicate.insert(duplicate.find("\"serverUrl\""), "\"" + std::string(flag) + "\": false,");
        PHPVPN_CHECK(!analyzeWizardDocument(duplicate));
    }
}

PHPVPN_TEST(fix2, legacy_fallbacks_are_independent_and_explicit_false_wins_in_repair) {
    for (unsigned absent = 0; absent < 4; ++absent) {
        auto dom = legacyDocument(); auto& master = member(dom, "masterSetup");
        std::erase_if(master.objectValue(), [&](const auto& item) {
            return ((absent & 1) && item.first == "virtualIp") || ((absent & 2) && item.first == "accessKey");
        });
        auto doc = analyzeWizardDocument(Json::serialize(dom));
        PHPVPN_REQUIRE_OK(doc);
        PHPVPN_CHECK(doc->fields.useDefaultServerSecretInWizard);
        PHPVPN_CHECK_EQ(doc->fields.autoDetectIpAddressInWizard, (absent & 1) != 0);
        PHPVPN_CHECK_EQ(doc->fields.useDefaultAccessKeyInWizard, (absent & 2) != 0);
        for (auto flag : flags) master.objectValue().emplace_back(flag, JsonValue::makeBool(false));
        auto explicitFalse = analyzeWizardDocument(Json::serialize(dom)).value();
        PHPVPN_CHECK(!explicitFalse.fields.useDefaultServerSecretInWizard);
        PHPVPN_CHECK(!explicitFalse.fields.autoDetectIpAddressInWizard);
        PHPVPN_CHECK(!explicitFalse.fields.useDefaultAccessKeyInWizard);
    }
    auto dom = legacyDocument(); member(member(dom, "masterSetup"), "serverSecret") = JsonValue::makeString("");
    PHPVPN_CHECK(!analyzeWizardDocument(Json::serialize(dom))->fields.useDefaultServerSecretInWizard);
    for (auto omitted : flags) {
        auto current = Json::parse(makeDefaultEditorDocument()).value();
        std::erase_if(member(current, "masterSetup").objectValue(), [&](const auto& item) { return item.first == omitted; });
        auto doc = analyzeWizardDocument(Json::serialize(current));
        PHPVPN_REQUIRE_OK(doc);
        PHPVPN_CHECK_EQ(doc->fields.autoDetectIpAddressInWizard, std::string_view(omitted) != "autoDetectIpAddressInWizard");
        PHPVPN_CHECK_EQ(doc->fields.useDefaultAccessKeyInWizard, std::string_view(omitted) != "useDefaultAccessKeyInWizard");
    }
}

PHPVPN_TEST(fix2, startup_creates_one_shared_default_and_never_changes_existing_bytes) {
    Store fixture;
    PHPVPN_REQUIRE_OK(fixture.store.recover());
    auto created = fixture.store.ensureDefaultAtStartup();
    PHPVPN_REQUIRE_OK(created);
    PHPVPN_CHECK_EQ(created.value(), StartupConfigState::created);
    PHPVPN_CHECK_EQ(*fixture.files->bytes, makeDefaultEditorDocument());
    PHPVPN_CHECK_EQ(fixture.files->writes, 1u);
    for (const auto text : {std::string(" \n") + Json::serialize(legacyDocument()), std::string("{bad json"),
        std::string("\xef\xbb\xbf") + Json::serialize(legacyDocument()),
        std::string("\xff\xfe"), std::string(kMaxConfigFileBytes + 1, 'x'), makeDefaultEditorDocument()}) {
        fixture.files->bytes = text;
        PHPVPN_REQUIRE_OK(fixture.store.recover());
        const auto reads = fixture.files->reads;
        PHPVPN_REQUIRE_OK(fixture.store.ensureDefaultAtStartup());
        PHPVPN_CHECK_EQ(fixture.files->reads, reads + 1);
        PHPVPN_CHECK_EQ(*fixture.files->bytes, text);
        PHPVPN_CHECK_EQ(fixture.files->writes, 1u);
    }
    fixture.files->readError = ErrorCode::config_unreadable;
    PHPVPN_REQUIRE_OK(fixture.store.ensureDefaultAtStartup());
    PHPVPN_CHECK_EQ(fixture.files->writes, 1u);
    fixture.files->readError.reset(); fixture.files->bytes.reset();
    fixture.files->beforePublish = [&] { fixture.files->bytes = "foreign invalid document"; };
    auto raced = fixture.store.ensureDefaultAtStartup();
    PHPVPN_REQUIRE_OK(raced);
    PHPVPN_CHECK_EQ(raced.value(), StartupConfigState::alreadyExists);
    PHPVPN_CHECK_EQ(*fixture.files->bytes, std::string("foreign invalid document"));
    fixture.files->beforePublish = {};
    auto repair = legacyDocument();
    std::erase_if(member(repair, "masterSetup").objectValue(), [](const auto& item) { return item.first == "virtualIp"; });
    const auto repairBytes = Json::serialize(repair);
    fixture.files->bytes = repairBytes;
    const auto writes = fixture.files->writes;
    PHPVPN_REQUIRE_OK(fixture.store.ensureDefaultAtStartup());
    PHPVPN_CHECK_EQ(fixture.files->writes, writes);
    PHPVPN_CHECK_EQ(*fixture.files->bytes, repairBytes);
}

PHPVPN_TEST(fix2, startup_recovery_and_publication_errors_remain_fail_closed_without_retry) {
    Store fixture;
    fixture.files->recoveryState = ConfigRecoveryState::unknown;
    PHPVPN_REQUIRE_OK(fixture.store.recover());
    PHPVPN_REQUIRE_ERROR(fixture.store.ensureDefaultAtStartup(), ErrorCode::journal_ambiguous);
    PHPVPN_REQUIRE_ERROR(fixture.store.readValid(), ErrorCode::journal_ambiguous);
    PHPVPN_REQUIRE_ERROR(fixture.store.createDefault(), ErrorCode::journal_ambiguous);
    PHPVPN_CHECK_EQ(fixture.files->writes, 0u);
    fixture.files->recoveryError = ErrorCode::journal_corrupt;
    PHPVPN_REQUIRE_ERROR(fixture.store.recover(), ErrorCode::journal_corrupt);
    PHPVPN_REQUIRE_ERROR(fixture.store.operationalStatus(), ErrorCode::journal_corrupt);
    fixture.files->recoveryError.reset(); fixture.files->recoveryState = ConfigRecoveryState::clean;
    PHPVPN_REQUIRE_OK(fixture.store.recover());
    fixture.files->outcome = ConfigPublishState::unknown;
    PHPVPN_REQUIRE_ERROR(fixture.store.ensureDefaultAtStartup(), ErrorCode::config_write_failed);
    PHPVPN_CHECK_EQ(fixture.files->writes, 1u);
    PHPVPN_CHECK(!fixture.files->bytes);
    fixture.files->outcome = ConfigPublishState::confirmed;
    fixture.files->publishError = ErrorCode::config_write_failed;
    PHPVPN_REQUIRE_ERROR(fixture.store.ensureDefaultAtStartup(), ErrorCode::config_write_failed);
    PHPVPN_REQUIRE_OK(fixture.store.operationalStatus());
    fixture.files->publishError.reset();
    PHPVPN_REQUIRE_OK(fixture.store.createDefault());
    ConfigStore unavailable(*fixture.crypto, "C:/test/config.json", Error(ErrorCode::config_directory_security_invalid));
    PHPVPN_CHECK_EQ(unavailable.path().generic_string(), std::string("C:/test/config.json"));
    auto snapshot = unavailable.read(); PHPVPN_REQUIRE_OK(snapshot);
    PHPVPN_CHECK_EQ(snapshot->availability, ConfigAvailability::unreadable);
    PHPVPN_CHECK(!snapshot->document);
    PHPVPN_REQUIRE_ERROR(unavailable.ensureDefaultAtStartup(), ErrorCode::config_directory_security_invalid);
    PHPVPN_REQUIRE_ERROR(unavailable.save(makeDefaultEditorDocument(), id), ErrorCode::config_directory_security_invalid);
}

PHPVPN_TEST(fix2, contracts_are_additive_preserve_keys_and_preview_matches_publication) {
    auto crypto = std::move(createPlatformCryptoProvider()).value();
    for (auto mode : {WizardMode::edit, WizardMode::repair}) {
        for (unsigned count : {0u, 1u, 13u}) {
            auto dom = legacyDocument();
            auto& contracts = member(dom, "communicationContracts").objectValue(); contracts.clear();
            for (unsigned i = 3; i < 3 + count; ++i)
                contracts.emplace_back("10.99.99." + std::to_string(i), JsonValue::makeObject({{"payloadKey", JsonValue::makeString(std::string(64, 'a'))}}));
            contracts.emplace_back("10.99.99.200", JsonValue::makeObject({{"payloadKey", JsonValue::makeString(std::string(64, 'b'))}}));
            auto draft = analyzeWizardDocument(Json::serialize(dom)).value();
            const auto before = Json::serialize(*draft.dom.find("communicationContracts"));
            auto unchanged = buildWizardDocument(*crypto, draft, mode, draft.fields, false);
            PHPVPN_REQUIRE_OK(unchanged);
            PHPVPN_CHECK_EQ(Json::serialize(*Json::parse(unchanged.value())->find("communicationContracts")), before);
            const auto missing = missingDefaultContractPeers(draft.context.vpnCidr, *Ipv4Address::parse(draft.fields.virtualIp), draft.contractPeers);
            auto text = buildWizardDocument(*crypto, draft, mode, draft.fields, true);
            PHPVPN_REQUIRE_OK(text);
            auto result = analyzeWizardDocument(text.value()).value();
            PHPVPN_CHECK_EQ(result.contractPeers.size(), draft.contractPeers.size() + missing.size());
            for (const auto& existing : draft.dom.find("communicationContracts")->objectValue())
                PHPVPN_CHECK_EQ(Json::serialize(*result.dom.find("communicationContracts")->find(existing.first)), Json::serialize(existing.second));
            auto repeat = buildWizardDocument(*crypto, result, mode, result.fields, true);
            PHPVPN_REQUIRE_OK(repeat);
            PHPVPN_CHECK_EQ(Json::serialize(*Json::parse(repeat.value())->find("communicationContracts")), Json::serialize(*result.dom.find("communicationContracts")));
            PHPVPN_CHECK(!result.dom.find("runtime"));
            PHPVPN_CHECK(!result.dom.find("masterSetup")->find("createDefaultContracts"));
        }
    }
    auto missingObject = legacyDocument();
    std::erase_if(missingObject.objectValue(), [](const auto& item) { return item.first == "communicationContracts"; });
    auto draft = analyzeWizardDocument(Json::serialize(missingObject)).value();
    auto unchanged = buildWizardDocument(*crypto, draft, WizardMode::edit, draft.fields, false);
    PHPVPN_REQUIRE_OK(unchanged);
    PHPVPN_CHECK(!Json::parse(unchanged.value())->find("communicationContracts"));
    draft.fields.virtualIp = "10.99.99.17";
    auto added = buildWizardDocument(*crypto, draft, WizardMode::edit, draft.fields, true);
    PHPVPN_REQUIRE_OK(added);
    PHPVPN_CHECK_EQ(analyzeWizardDocument(added.value())->contractPeers.size(), 14u);
    auto moved = analyzeWizardDocument(test::oneContractDocument()).value();
    const auto existingKey = Json::serialize(*moved.dom.find("communicationContracts")->find("10.99.99.3"));
    moved.fields.virtualIp = "10.99.99.10";
    moved.fields.accessKey = defaultAccessKey(moved.fields.virtualIp);
    auto changedIdentity = buildWizardDocument(*crypto, moved, WizardMode::edit, moved.fields, true);
    PHPVPN_REQUIRE_OK(changedIdentity);
    auto resultingDom = Json::parse(changedIdentity.value()).value();
    PHPVPN_CHECK_EQ(Json::serialize(*resultingDom.find("communicationContracts")->find("10.99.99.3")), existingKey);
    PHPVPN_CHECK(!resultingDom.find("communicationContracts")->find("10.99.99.10"));
}

PHPVPN_TEST(fix2, automatic_ip_replaces_placeholder_only_in_automatic_mode_and_can_be_disabled) {
    for (bool automatic : {false, true}) {
        auto view = viewFor(makeDefaultEditorDocument());
        view.fields->autoDetectIpAddressInWizard = automatic;
        WizardModel model; model.load(view);
        complete(model); complete(model, "10.99.99.10");
        PHPVPN_CHECK_EQ(model.fields().virtualIp, std::string(automatic ? "10.99.99.10" : "10.99.99.2"));
        PHPVPN_CHECK_EQ(model.fields().accessKey, defaultAccessKey(model.fields().virtualIp));
        PHPVPN_CHECK_EQ(model.setIp("10.99.99.11"), !automatic);
        PHPVPN_CHECK(model.setAutoDetectIpAddress(true));
        PHPVPN_CHECK_EQ(model.fields().virtualIp, std::string("10.99.99.10"));
        PHPVPN_REQUIRE_OK(model.validateCurrent());
        model.interrupt(); model.load(view, true);
        PHPVPN_CHECK(!model.hasSuggestedIp());
    }
    WizardModel model; model.load(viewFor(makeDefaultEditorDocument()));
    complete(model); complete(model);
    PHPVPN_CHECK_EQ(model.step(), WizardStep::identity);
    PHPVPN_CHECK(!model.validateCurrent());
    PHPVPN_CHECK(model.setAutoDetectIpAddress(false));
    PHPVPN_CHECK(model.setIp("10.99.99.11"));
    PHPVPN_REQUIRE_OK(model.validateCurrent());
    PHPVPN_CHECK(model.setIp("192.168.0.2"));
    PHPVPN_CHECK(model.fields().accessKey.empty());
}

PHPVPN_TEST(fix2, contract_checkbox_and_counts_include_edit_repair_and_unsupported_cidr) {
    auto view = viewFor(makeDefaultEditorDocument());
    for (auto mode : {WizardMode::edit, WizardMode::repair, WizardMode::create, WizardMode::reset}) {
        view.mode = mode;
        const bool newDraft = mode == WizardMode::create || mode == WizardMode::reset;
        view.contractCount = newDraft ? 0 : 1;
        view.existingDefaultContractPeers = newDraft ? std::vector<Ipv4Address>{} : std::vector<Ipv4Address>{*Ipv4Address::parse("10.99.99.3")};
        WizardModel model; model.load(view);
        PHPVPN_CHECK(model.createContracts()); PHPVPN_CHECK(model.contractsAllowed());
        PHPVPN_CHECK_EQ(model.missingContractCount(), newDraft ? 13u : 12u);
        PHPVPN_CHECK_EQ(model.contractCount(), 13u);
        model.setCreateContracts(false);
        PHPVPN_CHECK_EQ(model.missingContractCount(), newDraft ? 13u : 12u);
        PHPVPN_CHECK_EQ(model.contractCount(), newDraft ? 0u : 1u);
        model.load(view); PHPVPN_CHECK(model.createContracts());
    }
    view.context->vpnCidr = *Ipv4Cidr::parse("10.99.99.0/25");
    view.mode = WizardMode::edit; view.contractCount = 1;
    WizardModel model; model.load(view);
    PHPVPN_CHECK(model.createContracts()); PHPVPN_CHECK(!model.contractsAllowed());
    PHPVPN_CHECK(!model.shouldAddDefaultContracts());
    PHPVPN_CHECK_EQ(model.contractCount(), 1u);
}

PHPVPN_TEST(fix2, ipc_fields_and_preview_have_closed_typed_schemas) {
    const auto view = viewFor(test::oneContractDocument());
    for (unsigned mask = 0; mask < 8; ++mask) {
        auto fields = *view.fields;
        fields.useDefaultServerSecretInWizard = (mask & 1) != 0;
        fields.autoDetectIpAddressInWizard = (mask & 2) != 0;
        fields.useDefaultAccessKeyInWizard = (mask & 4) != 0;
        auto encoded = WizardIpc::fieldsValue(fields);
        PHPVPN_CHECK_EQ(encoded.objectValue().size(), 8u);
        auto decoded = WizardIpc::parseFields(encoded);
        PHPVPN_REQUIRE_OK(decoded);
        PHPVPN_CHECK_EQ(decoded.value(), fields);
        for (auto flag : flags) {
            auto bad = encoded; member(bad, flag) = JsonValue::makeString("true");
            PHPVPN_CHECK(!WizardIpc::parseFields(bad));
            std::erase_if(bad.objectValue(), [&](const auto& item) { return item.first == flag; });
            PHPVPN_CHECK(!WizardIpc::parseFields(bad));
        }
    }
    auto encoded = WizardIpc::viewValue(view);
    PHPVPN_REQUIRE_OK(WizardIpc::parseView(encoded));
    for (const auto bad : {"null", "[\"10.99.99.3\",\"10.99.99.3\"]", "[\"10.99.99.16\"]", "[\"10.20.30.3\"]", "[3]"}) {
        auto malformed = encoded; member(malformed, "existingDefaultContractPeers") = Json::parse(bad).value();
        PHPVPN_CHECK(!WizardIpc::parseView(malformed));
    }
    auto historical = encoded;
    std::erase_if(historical.objectValue(), [](const auto& item) { return item.first == "existingDefaultContractPeers"; });
    std::erase_if(member(historical, "fields").objectValue(), [](const auto& item) { return item.first.ends_with("InWizard") || item.first == "httpHeaders"; });
    PHPVPN_REQUIRE_ERROR(WizardIpc::parseView(historical), ErrorCode::wizard_unsupported);
    member(historical, "contractCount") = JsonValue::makeString("wrong type");
    PHPVPN_REQUIRE_ERROR(WizardIpc::parseView(historical), ErrorCode::ipc_malformed);
    auto wrong = encoded;
    member(wrong, "contractCount") = JsonValue::makeInteger(14);
    member(wrong, "existingDefaultContractPeers") = Json::parse("[\"10.99.99.10\",\"10.99.99.2\"]").value();
    PHPVPN_REQUIRE_ERROR(WizardIpc::parseView(wrong), ErrorCode::ipc_malformed);
    auto allPeers = defaultContractCandidates(view.context->vpnCidr);
    auto full = view; full.contractCount = 14; full.existingDefaultContractPeers = allPeers;
    PHPVPN_REQUIRE_OK(WizardIpc::parseView(WizardIpc::viewValue(full)));
    full.existingDefaultContractPeers.push_back(allPeers.back());
    PHPVPN_REQUIRE_ERROR(WizardIpc::parseView(WizardIpc::viewValue(full)), ErrorCode::ipc_malformed);
    full = view; full.mode = WizardMode::create; full.availability = ConfigAvailability::missing; full.revisionToken.clear();
    PHPVPN_REQUIRE_ERROR(WizardIpc::parseView(WizardIpc::viewValue(full)), ErrorCode::ipc_malformed);
    auto oldFields = WizardIpc::fieldsValue(*view.fields);
    std::erase_if(oldFields.objectValue(), [](const auto& item) { return item.first.ends_with("InWizard") || item.first == "httpHeaders"; });
    PHPVPN_REQUIRE_ERROR(WizardIpc::parseFields(oldFields), ErrorCode::wizard_unsupported);
    oldFields.objectValue().emplace_back("accessKey", JsonValue::makeString(""));
    PHPVPN_REQUIRE_ERROR(WizardIpc::parseFields(oldFields), ErrorCode::ipc_malformed);
}
} // namespace phpvpn
