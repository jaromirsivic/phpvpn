#include "core/protocol/http_headers.h"
// Full configuration validation, plan_client.md chapter 8.2.
//
// The validator runs before every connection attempt and before every write
// through `set-config`. A validation error carries a stable code and a safe
// description; a key value never appears in an error or in the log.
#include <algorithm>
#include <limits>

#include "core/base/hex.h"
#include "core/config/config_document.h"
#include "core/protocol/json.h"

namespace phpvpn {
namespace {

constexpr std::size_t kKeyHexLength = 64;

// Rejects any member the schema does not define. plan_client.md keeps the field
// set closed so a typo becomes an error instead of a silently ignored value.
Status requireKnownMembers(const JsonValue& object,
                           std::span<const std::string_view> allowed,
                           std::string_view section) {
    for (const auto& member : object.objectValue()) {
        const bool known =
            std::find(allowed.begin(), allowed.end(), member.first) != allowed.end();
        if (!known) {
            return fail(ErrorCode::config_invalid_value,
                        std::string("unknown member in ") + std::string(section));
        }
    }
    return ok();
}

Result<std::string> requireString(const JsonValue& object,
                                  std::string_view key,
                                  std::string_view section) {
    const JsonValue* value = object.find(key);
    if (value == nullptr) {
        return Error(ErrorCode::config_invalid_value,
                     std::string("missing ") + std::string(key) + " in " + std::string(section));
    }
    if (!value->isString()) {
        return Error(ErrorCode::config_invalid_value,
                     std::string(key) + " must be a string in " + std::string(section));
    }
    return value->stringValue();
}

// Every runtime value is a positive integer representable in int64_t. Zero, a
// negative value, a fractional number and an unrepresentable value are refused.
Result<std::int64_t> optionalPositiveInteger(const JsonValue& object,
                                             std::string_view key,
                                             std::int64_t fallback) {
    const JsonValue* value = object.find(key);
    if (value == nullptr) {
        return fallback;
    }
    if (!value->isInteger()) {
        return Error(ErrorCode::config_invalid_limit,
                     std::string(key) + " must be a positive integer");
    }
    if (value->integerValue() <= 0) {
        return Error(ErrorCode::config_invalid_limit,
                     std::string(key) + " must be greater than zero");
    }
    return value->integerValue();
}

Result<SecureBuffer> parseHexKey(std::string_view text, std::string_view what) {
    if (text.size() != kKeyHexLength || !isLowercaseHex(text)) {
        return Error(ErrorCode::config_invalid_key,
                     std::string(what) + " must be 64 lowercase hexadecimal characters");
    }
    std::vector<std::uint8_t> bytes;
    if (!decodeLowercaseHex(text, bytes)) {
        return Error(ErrorCode::config_invalid_key, std::string(what) + " is not valid hex");
    }
    SecureBuffer key(std::span<const std::uint8_t>(bytes.data(), bytes.size()));
    secureWipe(bytes.data(), bytes.size());
    return key;
}

// True when the effective secret is publicly predictable: an empty configured
// value, a value made only of NUL bytes or only of ASCII `0`, matching the
// warning of protocol.md chapter 5.1.
bool isPredictableSecret(std::string_view configured) {
    if (configured.empty() || configured == kDefaultServerSecret) {
        return true;
    }
    bool allZeroBytes = true;
    bool allZeroDigits = true;
    for (char c : configured) {
        if (c != '\0') {
            allZeroBytes = false;
        }
        if (c != '0') {
            allZeroDigits = false;
        }
    }
    return allZeroBytes || allZeroDigits;
}

// Multiplies without overflow so an extreme peer count cannot wrap around and
// let an invalid queue budget pass.
bool multiplyChecked(std::int64_t a, std::int64_t b, std::int64_t& out) noexcept {
    if (a == 0 || b == 0) {
        out = 0;
        return true;
    }
    if (a > std::numeric_limits<std::int64_t>::max() / b) {
        return false;
    }
    out = a * b;
    return true;
}

}  // namespace

Result<ConfigValues> validateConfigValues(std::string_view json, ConfigWarnings& warnings, bool allowMissingIdentity) {
    warnings = ConfigWarnings{};

    std::string normalized;
    PHPVPN_TRY_ASSIGN(normalized, normalizeConfigText(json));
    json = normalized;

    JsonValue document;
    PHPVPN_TRY_ASSIGN(document, Json::parse(json));
    if (!document.isObject()) {
        return Error(ErrorCode::config_invalid_json, "the configuration must be a JSON object");
    }

    static constexpr std::string_view kTopLevel[] = {
        "masterSetup", "runtime", "communicationContracts"};
    PHPVPN_TRY(requireKnownMembers(document, kTopLevel, "the configuration"));

    const JsonValue* master = document.find("masterSetup");
    if (master == nullptr || !master->isObject()) {
        return Error(ErrorCode::config_invalid_value, "masterSetup must be an object");
    }

    static constexpr std::string_view kMasterMembers[] = {"httpHeaders", "serverUrl",
                                                          "trustAnyTlsCertificate",
                                                          "serverSecret",
                                                          "vpnCidr",
                                                          "virtualIp",
                                                          "accessKey",
                                                          "useDefaultServerSecretInWizard",
                                                          "autoDetectIpAddressInWizard",
                                                          "useDefaultAccessKeyInWizard"};
    PHPVPN_TRY(requireKnownMembers(*master, kMasterMembers, "masterSetup"));

    auto config = std::make_shared<ConfigValues>();
    config->rawJson.assign(json);
    PHPVPN_TRY_ASSIGN(config->httpHeaders, requireString(*master, "httpHeaders", "masterSetup"));
    PHPVPN_TRY(normalizeHttpHeaders(config->httpHeaders));
    for (const auto& [name, destination] : {
        std::pair{"useDefaultServerSecretInWizard", &config->wizardPreferences.useDefaultServerSecretInWizard},
        std::pair{"autoDetectIpAddressInWizard", &config->wizardPreferences.autoDetectIpAddressInWizard},
        std::pair{"useDefaultAccessKeyInWizard", &config->wizardPreferences.useDefaultAccessKeyInWizard}}) {
        if (const auto* value = master->find(name)) {
            if (!value->isBool()) return Error(ErrorCode::config_invalid_value, std::string(name) + " must be a boolean");
            *destination = value->boolValue();
        }
    }

    std::string serverUrlText;
    PHPVPN_TRY_ASSIGN(serverUrlText, requireString(*master, "serverUrl", "masterSetup"));
    PHPVPN_TRY_ASSIGN(config->serverUrl, parseServerUrl(serverUrlText));

    if (const JsonValue* trust = master->find("trustAnyTlsCertificate")) {
        if (!trust->isBool()) {
            return Error(ErrorCode::config_invalid_value,
                         "trustAnyTlsCertificate must be a boolean");
        }
        config->trustAnyTlsCertificate = trust->boolValue();
    } else {
        config->trustAnyTlsCertificate = kDefaultTrustAnyTlsCertificate;
    }

    // A string of any length is valid, including the empty string, which the
    // key derivation normalises to CHANGE_THIS_SERVER_SECRET.
    PHPVPN_TRY_ASSIGN(config->serverSecret, requireString(*master, "serverSecret", "masterSetup"));

    std::string cidrText = kDefaultVpnCidr;
    if (const JsonValue* cidr = master->find("vpnCidr")) {
        if (!cidr->isString()) {
            return Error(ErrorCode::config_invalid_cidr, "vpnCidr must be a string");
        }
        cidrText = cidr->stringValue();
    }
    const auto parsedCidr = Ipv4Cidr::parse(cidrText);
    if (!parsedCidr.has_value()) {
        return Error(ErrorCode::config_invalid_cidr,
                     "vpnCidr must be a canonical IPv4 network with a prefix length of 24 to 29");
    }
    config->vpnCidr = *parsedCidr;

    if (master->find("virtualIp") || !allowMissingIdentity) {
        std::string virtualIpText;
        PHPVPN_TRY_ASSIGN(virtualIpText, requireString(*master, "virtualIp", "masterSetup"));
        const auto virtualIp = Ipv4Address::parse(virtualIpText);
        if (!virtualIp || !config->vpnCidr.isValidClientAddress(*virtualIp))
            return Error(ErrorCode::config_invalid_address);
        config->virtualIp = *virtualIp;
    }
    if (master->find("accessKey") || !allowMissingIdentity) {
        std::string accessKeyText;
        PHPVPN_TRY_ASSIGN(accessKeyText, requireString(*master, "accessKey", "masterSetup"));
        SecureBuffer key;
        PHPVPN_TRY_ASSIGN(key, parseHexKey(accessKeyText, "accessKey"));
        config->accessKey = std::move(key);
    }

    RuntimeLimits limits;
    if (const JsonValue* runtime = document.find("runtime")) {
        if (!runtime->isObject()) {
            return Error(ErrorCode::config_invalid_value, "runtime must be an object");
        }
        static constexpr std::string_view kRuntimeMembers[] = {"maxOutboundQueueBytes",
                                                               "maxInboundQueueBytes",
                                                               "minOutboundPerPeerQueueBytes",
                                                               "minInboundPerPeerQueueBytes",
                                                               "maxQueuedPacketAgeMs",
                                                               "maxResponseBodyBytes",
                                                               "maxNdjsonLineBytes",
                                                               "maxDataRecordsPerResponse",
                                                               "responseTimeoutSeconds",
                                                               "timeSyncIntervalSeconds"};
        PHPVPN_TRY(requireKnownMembers(*runtime, kRuntimeMembers, "runtime"));

        PHPVPN_TRY_ASSIGN(limits.maxOutboundQueueBytes,
                          optionalPositiveInteger(*runtime, "maxOutboundQueueBytes",
                                                  kDefaultMaxOutboundQueueBytes));
        PHPVPN_TRY_ASSIGN(limits.maxInboundQueueBytes,
                          optionalPositiveInteger(*runtime, "maxInboundQueueBytes",
                                                  kDefaultMaxInboundQueueBytes));
        PHPVPN_TRY_ASSIGN(limits.minOutboundPerPeerQueueBytes,
                          optionalPositiveInteger(*runtime, "minOutboundPerPeerQueueBytes",
                                                  kDefaultMinOutboundPerPeerQueueBytes));
        PHPVPN_TRY_ASSIGN(limits.minInboundPerPeerQueueBytes,
                          optionalPositiveInteger(*runtime, "minInboundPerPeerQueueBytes",
                                                  kDefaultMinInboundPerPeerQueueBytes));
        PHPVPN_TRY_ASSIGN(limits.maxQueuedPacketAgeMs,
                          optionalPositiveInteger(*runtime, "maxQueuedPacketAgeMs",
                                                  kDefaultMaxQueuedPacketAgeMs));
        PHPVPN_TRY_ASSIGN(limits.maxResponseBodyBytes,
                          optionalPositiveInteger(*runtime, "maxResponseBodyBytes",
                                                  kDefaultMaxResponseBodyBytes));
        PHPVPN_TRY_ASSIGN(limits.maxNdjsonLineBytes,
                          optionalPositiveInteger(*runtime, "maxNdjsonLineBytes",
                                                  kDefaultMaxNdjsonLineBytes));
        PHPVPN_TRY_ASSIGN(limits.maxDataRecordsPerResponse,
                          optionalPositiveInteger(*runtime, "maxDataRecordsPerResponse",
                                                  kDefaultMaxDataRecordsPerResponse));
        PHPVPN_TRY_ASSIGN(limits.responseTimeoutSeconds,
                          optionalPositiveInteger(*runtime, "responseTimeoutSeconds",
                                                  kDefaultResponseTimeoutSeconds));
        PHPVPN_TRY_ASSIGN(limits.timeSyncIntervalSeconds,
                          optionalPositiveInteger(*runtime, "timeSyncIntervalSeconds",
                                                  kDefaultTimeSyncIntervalSeconds));
    }

    if (limits.maxNdjsonLineBytes > limits.maxResponseBodyBytes) {
        return Error(ErrorCode::config_invalid_limit,
                     "maxNdjsonLineBytes must not exceed maxResponseBodyBytes");
    }
    if (limits.responseTimeoutSeconds > kMaxHttpTimeoutSeconds)
        return Error(ErrorCode::config_invalid_limit, "responseTimeoutSeconds exceeds 2147483 seconds");
    config->runtime = limits;

    if (const JsonValue* contracts = document.find("communicationContracts")) {
        if (!contracts->isObject()) {
            return Error(ErrorCode::config_invalid_value,
                         "communicationContracts must be an object");
        }
        for (const auto& member : contracts->objectValue()) {
            const auto peer = Ipv4Address::parse(member.first);
            if (!peer.has_value() || !config->vpnCidr.isValidClientAddress(*peer)) {
                return Error(ErrorCode::config_invalid_address,
                             "a communication contract key is not a valid client address");
            }
            if (config->virtualIp && *peer == *config->virtualIp) {
                return Error(ErrorCode::config_invalid_address,
                             "a communication contract must not target the local virtualIp");
            }
            if (!member.second.isObject()) {
                return Error(ErrorCode::config_invalid_value,
                             "a communication contract must be an object");
            }
            static constexpr std::string_view kContractMembers[] = {"payloadKey"};
            PHPVPN_TRY(requireKnownMembers(member.second, kContractMembers,
                                           "a communication contract"));

            std::string payloadKeyText;
            PHPVPN_TRY_ASSIGN(payloadKeyText,
                              requireString(member.second, "payloadKey", "a communication contract"));

            CommunicationContract contract;
            contract.peer = *peer;
            PHPVPN_TRY_ASSIGN(contract.payloadKey, parseHexKey(payloadKeyText, "payloadKey"));
            config->contracts.push_back(std::move(contract));
        }
    }

    // Admission invariant of client.md: every possible peer queue keeps a
    // logical reservation in both directions, so the global limit has to cover
    // maxRemotePeers * the direction's peer minimum on each side.
    const auto peers = static_cast<std::int64_t>((config->vpnCidr.clientAddressCount() - 1));
    std::int64_t requiredBytes = 0;
    if (!multiplyChecked(peers, limits.minOutboundPerPeerQueueBytes, requiredBytes)) {
        return Error(ErrorCode::config_queue_budget,
                     "the per peer reservation overflows for this prefix");
    }
    if (limits.maxOutboundQueueBytes < requiredBytes) {
        return Error(ErrorCode::config_queue_budget,
                     "maxOutboundQueueBytes is smaller than the total per peer reservation");
    }
    if (!multiplyChecked(peers, limits.minInboundPerPeerQueueBytes, requiredBytes) ||
        limits.maxInboundQueueBytes < requiredBytes) {
        return Error(ErrorCode::config_queue_budget,
                     "maxInboundQueueBytes is smaller than the total per peer reservation");
    }

    warnings.trustsAnyTlsCertificate = config->trustAnyTlsCertificate;
    warnings.usesPlainHttp = !config->serverUrl.secure;
    warnings.serverSecretPredictable = isPredictableSecret(config->serverSecret);

    return std::move(*config);
}

Result<ClientConfigPtr> parseAndValidateConfig(std::string_view json, ConfigWarnings& warnings) {
    auto values = validateConfigValues(json, warnings, false);
    if (!values) return values.error();
    auto config = std::make_shared<ClientConfig>();
    config->serverUrl = std::move(values->serverUrl);
    config->serverSecret = std::move(values->serverSecret);
    config->trustAnyTlsCertificate = values->trustAnyTlsCertificate;
    config->vpnCidr = values->vpnCidr;
    config->virtualIp = *values->virtualIp;
    config->accessKey = std::move(*values->accessKey);
    config->runtime = values->runtime;
    config->contracts = std::move(values->contracts);
    config->rawJson = std::move(values->rawJson);
    config->httpHeaders = std::move(values->httpHeaders);
    return ClientConfigPtr(std::move(config));
}

}  // namespace phpvpn
