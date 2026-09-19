#include "core/config/config_document.h"
#include <algorithm>
#include "core/base/hex.h"
#include "core/config/config_defaults.h"
#include "core/wizard/wizard_defaults.h"

namespace phpvpn {
const char* configAvailabilityName(ConfigAvailability value) noexcept {
    switch (value) {
        case ConfigAvailability::missing: return "missing";
        case ConfigAvailability::unreadable: return "unreadable";
        case ConfigAvailability::encoding_error: return "encoding_error";
        case ConfigAvailability::too_large: return "too_large";
        case ConfigAvailability::invalid: return "invalid";
        case ConfigAvailability::repairable: return "repairable";
        case ConfigAvailability::valid: return "valid";
    }
    return "invalid";
}
bool isStrictUtf8(std::string_view text) noexcept {
    for (std::size_t i = 0; i < text.size();) {
        auto byte = static_cast<unsigned char>(text[i++]);
        if (byte < 0x80) continue;
        unsigned count = 0, code = 0, minimum = 0;
        if (byte >= 0xc2 && byte <= 0xdf) { count = 1; code = byte & 0x1f; minimum = 0x80; }
        else if (byte >= 0xe0 && byte <= 0xef) { count = 2; code = byte & 0xf; minimum = 0x800; }
        else if (byte >= 0xf0 && byte <= 0xf4) { count = 3; code = byte & 7; minimum = 0x10000; }
        else return false;
        if (count > text.size() - i) return false;
        while (count--) {
            byte = static_cast<unsigned char>(text[i++]);
            if ((byte & 0xc0) != 0x80) return false;
            code = (code << 6) | (byte & 0x3f);
        }
        if (code < minimum || code > 0x10ffff || (code >= 0xd800 && code <= 0xdfff)) return false;
    }
    return true;
}
Result<std::string> normalizeConfigText(std::string_view bytes) {
    if (bytes.size() > kMaxConfigFileBytes) return Error(ErrorCode::config_too_large);
    // Reject invalid byte sequences, without guessing a different encoding.
    // A raw U+0000 is valid UTF-8 but invalid JSON. Preserve it for the editor.
    if (!isStrictUtf8(bytes))
        return Error(ErrorCode::config_encoding_error);
    if (bytes.starts_with("\xef\xbb\xbf")) bytes.remove_prefix(3);
    return std::string(bytes);
}
Result<WizardDocument> analyzeWizardDocument(std::string_view json) {
    ConfigWarnings warnings;
    auto values = validateConfigValues(json, warnings, true);
    if (!values) return values.error();
    WizardDocument document;
    PHPVPN_TRY_ASSIGN(document.dom, Json::parse(values->rawJson));
    document.fields.serverUrl = values->serverUrl.original;
    document.fields.serverSecret = values->serverSecret;
    document.context = {values->vpnCidr, values->trustAnyTlsCertificate, values->runtime, values->httpHeaders};
    document.hasVirtualIp = values->virtualIp.has_value();
    document.hasAccessKey = values->accessKey.has_value();
    document.fields.useDefaultServerSecretInWizard = values->wizardPreferences.useDefaultServerSecretInWizard.value_or(values->serverSecret == kDefaultServerSecret);
    document.fields.autoDetectIpAddressInWizard = values->wizardPreferences.autoDetectIpAddressInWizard.value_or(!document.hasVirtualIp);
    document.fields.useDefaultAccessKeyInWizard = values->wizardPreferences.useDefaultAccessKeyInWizard.value_or(!document.hasAccessKey);
    if (values->virtualIp) document.fields.virtualIp = values->virtualIp->toString();
    if (values->accessKey) document.fields.accessKey = toLowercaseHex(values->accessKey->span());
    for (const auto& contract : values->contracts) document.contractPeers.push_back(contract.peer);
    return document;
}
Status validateWizardIdentity(const WizardDocument& document, const WizardFields& fields) {
    const auto ip = Ipv4Address::parse(fields.virtualIp);
    if (!ip || !document.context.vpnCidr.isValidClientAddress(*ip)) return Error(ErrorCode::config_invalid_address);
    if (std::find(document.contractPeers.begin(), document.contractPeers.end(), *ip) != document.contractPeers.end())
        return Error(ErrorCode::wizard_contract_self_conflict);
    if (fields.accessKey.size() != 64 || !isLowercaseHex(fields.accessKey)) return Error(ErrorCode::config_invalid_key);
    return ok();
}
Result<bool> readAutoConnectOnStartup(const JsonValue& document) {
    const auto* master = document.find("masterSetup");
    if (!document.isObject() || !master || !master->isObject())
        return Error(ErrorCode::config_invalid_value, "masterSetup must be an object");
    const auto* value = master->find("autoConnectOnStartup");
    if (!value) return false;
    if (!value->isBool())
        return Error(ErrorCode::config_invalid_value, "autoConnectOnStartup must be a boolean");
    return value->boolValue();
}

Result<std::string> buildWizardDocument(CryptoProvider& crypto, const WizardDocument& original,
    WizardMode mode, const WizardFields& fields, bool createDefaultContracts) {
    PHPVPN_TRY(validateWizardIdentity(original, fields));
    JsonValue dom = original.dom;
    bool autoConnect = false;
    PHPVPN_TRY_ASSIGN(autoConnect, readAutoConnectOnStartup(dom));
    auto set = [](JsonValue& object, const std::string& key, JsonValue value) {
        for (auto& member : object.objectValue()) {
            if (member.first == key) { member.second = std::move(value); return; }
        }
        object.objectValue().emplace_back(key, std::move(value));
    };
    for (auto& member : dom.objectValue()) {
        if (member.first != "masterSetup") continue;
        set(member.second, "serverUrl", JsonValue::makeString(fields.serverUrl));
        set(member.second, "serverSecret", JsonValue::makeString(fields.serverSecret));
        set(member.second, "virtualIp", JsonValue::makeString(fields.virtualIp));
        set(member.second, "accessKey", JsonValue::makeString(fields.accessKey));
        set(member.second, "useDefaultServerSecretInWizard", JsonValue::makeBool(fields.useDefaultServerSecretInWizard));
        set(member.second, "autoDetectIpAddressInWizard", JsonValue::makeBool(fields.autoDetectIpAddressInWizard));
        set(member.second, "useDefaultAccessKeyInWizard", JsonValue::makeBool(fields.useDefaultAccessKeyInWizard));
        // Identity fields may have been appended by create/repair. Keep the
        // documented final pair in order without changing unrelated members.
        if (mode == WizardMode::create || mode == WizardMode::reset)
            std::erase_if(member.second.objectValue(), [](const auto& item) {
                return item.first == "useDefaultAccessKeyInWizard" || item.first == "autoConnectOnStartup";
            });
        set(member.second, "useDefaultAccessKeyInWizard", JsonValue::makeBool(fields.useDefaultAccessKeyInWizard));
        set(member.second, "autoConnectOnStartup", JsonValue::makeBool(autoConnect));
    }
    if (createDefaultContracts && !supportsDefaultContracts(original.context.vpnCidr)) return Error(ErrorCode::invalid_argument);
    const auto own = *Ipv4Address::parse(fields.virtualIp);
    const auto missing = createDefaultContracts ? missingDefaultContractPeers(original.context.vpnCidr, own, original.contractPeers) : std::vector<Ipv4Address>{};
    if (!missing.empty() || mode == WizardMode::create || mode == WizardMode::reset) {
        JsonObject contracts;
        if (const auto* existing = dom.find("communicationContracts")) contracts = existing->objectValue();
        for (const auto peer : missing) {
            std::string key;
            PHPVPN_TRY_ASSIGN(key, defaultPayloadKey(crypto, own, peer));
            contracts.emplace_back(peer.toString(), JsonValue::makeObject({{"payloadKey", JsonValue::makeString(key)}}));
        }
        set(dom, "communicationContracts", JsonValue::makeObject(std::move(contracts)));
    }
    const auto text = Json::serializePretty(dom);
    ConfigWarnings warnings;
    PHPVPN_TRY(parseAndValidateConfig(text, warnings));
    return text;
}
}
