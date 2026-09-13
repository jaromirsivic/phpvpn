#include "core/protocol/http_headers.h"
#include "core/ipc/wizard_ipc.h"
#include "core/ipc/ipc_protocol.h"
#include "core/protocol/uuid.h"
#include "core/wizard/wizard_defaults.h"
#include <algorithm>
#include <array>
#include <limits>

namespace phpvpn {
namespace {
using J = JsonValue;
J str(std::string_view value) { return J::makeString(std::string(value)); }
J nullable(const std::optional<std::string>& value) { return value ? str(*value) : J::makeNull(); }
bool text(const J* value) { return value && value->isString(); }
bool uuid(const J* value) { return text(value) && Uuid::isValidV4(value->stringValue()); }
bool number(const J* value, bool positive = false) {
    return value && ((value->isInteger() && value->integerValue() >= (positive ? 1 : 0)) ||
        (value->isUnsignedInteger() && (!positive || value->unsignedValue() > 0)));
}
bool nullText(const J* value) { return value && (value->isNull() || value->isString()); }
bool token(const J* value) {
    return text(value) && !value->stringValue().empty() &&
        value->stringValue().find('\0') == std::string::npos;
}
bool absoluteConfigPath(const J* value) {
    if (!text(value)) return false;
    std::string_view path = value->stringValue();
    if (std::any_of(path.begin(), path.end(), [](unsigned char c) { return c < 32; })) return false;
    const auto slash = [](char c) { return c == '/' || c == '\\'; };
    const auto drive = [&](std::string_view candidate) {
        return candidate.size() > 3 && ((candidate[0] >= 'A' && candidate[0] <= 'Z') ||
            (candidate[0] >= 'a' && candidate[0] <= 'z')) && candidate[1] == ':' && slash(candidate[2]);
    };
    if (drive(path)) return true;
    if (path.starts_with("\\\\?\\")) {
        path.remove_prefix(4);
        if (drive(path)) return true;
        if (!path.starts_with("UNC\\")) return false;
        path.remove_prefix(4);
    } else {
        if (path.size() < 3 || !slash(path[0]) || !slash(path[1]) || path[2] == '.' || path[2] == '?') return false;
        path.remove_prefix(2);
    }
    const auto serverEnd = path.find_first_of("\\/");
    if (serverEnd == 0 || serverEnd == std::string_view::npos) return false;
    path.remove_prefix(serverEnd + 1);
    const auto shareEnd = path.find_first_of("\\/");
    return shareEnd != 0 && shareEnd != std::string_view::npos && shareEnd + 1 < path.size();
}
bool oneOf(const J* value, std::initializer_list<std::string_view> allowed) {
    return text(value) && std::find(allowed.begin(), allowed.end(), value->stringValue()) != allowed.end();
}
bool shape(const J& value, std::initializer_list<std::string_view> names) {
    if (!value.isObject() || value.objectValue().size() != names.size()) return false;
    for (auto name : names) if (!value.find(name)) return false;
    return true;
}
Error malformed() { return Error(ErrorCode::ipc_malformed); }
J diagnosticMessageValue(const BoundedHttpMessage& m) {
    return J::makeObject({{"originalSizeBytes", J::makeUnsignedInteger(m.originalSizeBytes)},
        {"prefix", str(encodeDiagnosticOctets(m.prefix))}, {"separator", str(m.truncated() ? kHttpDiagnosticSeparator : "")},
        {"suffix", str(encodeDiagnosticOctets(m.suffix))}});
}
constexpr std::array requestSources = {"winhttp", "merged", "application"};
constexpr std::array responseSources = {"winhttp", "available", "unavailable"};
J diagnosticValue(const HttpDiagnostic& d) {
    return J::makeObject({{"format", str("http-octets-v1")}, {"action", str(httpActionName(d.action))},
        {"responseComplete", J::makeBool(d.responseComplete)},
        {"requestHeadersSource", str(requestSources.at(static_cast<std::size_t>(d.requestHeadersSource)))},
        {"responseHeadersSource", str(responseSources.at(static_cast<std::size_t>(d.responseHeadersSource)))},
        {"request", diagnosticMessageValue(d.request)}, {"response", diagnosticMessageValue(d.response)}});
}
Result<BoundedHttpMessage> parseDiagnosticMessage(const J& value) {
    if (!shape(value, {"originalSizeBytes", "prefix", "separator", "suffix"}) || !number(value.find("originalSizeBytes"), true) ||
        !text(value.find("prefix")) || !text(value.find("separator")) || !text(value.find("suffix"))) return malformed();
    BoundedHttpMessage m;
    m.originalSizeBytes = jsonUnsignedOrZero(value.find("originalSizeBytes"));
    if (value.find("separator")->stringValue() != (m.truncated() ? kHttpDiagnosticSeparator : "")) return malformed();
    auto prefix = decodeDiagnosticOctets(value.find("prefix")->stringValue(), m.truncated() ? kHttpDiagnosticPrefix : static_cast<std::size_t>(m.originalSizeBytes));
    auto suffix = decodeDiagnosticOctets(value.find("suffix")->stringValue(), m.truncated() ? kHttpDiagnosticTail : 0);
    if (!prefix || !suffix) return malformed();
    m.prefix = std::move(*prefix); m.suffix = std::move(*suffix);
    return m;
}
Result<HttpDiagnostic> parseDiagnostic(const J& value) {
    if (!shape(value, {"format", "action", "responseComplete", "requestHeadersSource", "responseHeadersSource", "request", "response"}) ||
        !oneOf(value.find("format"), {"http-octets-v1"}) || !oneOf(value.find("action"), {"health", "info"}) ||
        !value.find("responseComplete")->isBool() || !oneOf(value.find("requestHeadersSource"), {"winhttp", "merged", "application"}) ||
        !oneOf(value.find("responseHeadersSource"), {"winhttp", "available", "unavailable"})) return malformed();
    HttpDiagnostic d;
    d.action = value.find("action")->stringValue() == "health" ? HttpAction::health : HttpAction::info;
    d.responseComplete = value.find("responseComplete")->boolValue();
    for (std::size_t i = 0; i < requestSources.size(); ++i) if (value.find("requestHeadersSource")->stringValue() == requestSources[i]) d.requestHeadersSource = static_cast<RequestHeadersSource>(i);
    for (std::size_t i = 0; i < responseSources.size(); ++i) if (value.find("responseHeadersSource")->stringValue() == responseSources[i]) d.responseHeadersSource = static_cast<ResponseHeadersSource>(i);
    PHPVPN_TRY_ASSIGN(d.request, parseDiagnosticMessage(*value.find("request")));
    PHPVPN_TRY_ASSIGN(d.response, parseDiagnosticMessage(*value.find("response")));
    return d;
}
constexpr std::array names = {
    "get-config-state", "get-config-document", "get-default-config", "create-default-config",
    "wizard-open", "wizard-reset", "wizard-keepalive", "wizard-check-url", "wizard-check-secret",
    "wizard-check-access", "wizard-get-operation", "wizard-cancel-operation", "wizard-cancel-attempt",
    "wizard-resume", "wizard-finish", "wizard-get-commit", "wizard-close"};
constexpr std::array kinds = {"url", "secret", "access", "finish"};
constexpr std::array states = {"pending", "running", "cancelling", "succeeded", "failed", "cancelled", "interrupted"};
constexpr std::array modes = {"create", "edit", "repair", "reset"};
template<std::size_t N> std::optional<std::size_t> index(const J* value, const std::array<const char*, N>& values) {
    if (!text(value)) return {};
    for (std::size_t i = 0; i < N; ++i) if (value->stringValue() == values[i]) return i;
    return {};
}
std::vector<std::string_view> requestFields(WizardCommand command) {
    std::vector<std::string_view> result = {"ipcVersion", "requestId", "command"};
    if (command >= WizardCommand::reset) { result.push_back("serviceInstanceId"); result.push_back("sessionId"); }
    if (command == WizardCommand::check_url || command == WizardCommand::check_secret ||
        command == WizardCommand::check_access || command == WizardCommand::cancel_attempt) {
        result.push_back("clientAttemptId"); result.push_back("attemptSequence");
    }
    if (command == WizardCommand::check_url || command == WizardCommand::check_secret) result.push_back("serverUrl");
    if (command == WizardCommand::check_secret) result.push_back("serverSecret");
    if (command == WizardCommand::check_access || command == WizardCommand::finish) result.push_back("fields");
    if (command == WizardCommand::get_operation || command == WizardCommand::cancel_operation) result.push_back("operationId");
    if (command == WizardCommand::finish || command == WizardCommand::get_commit) result.push_back("commitId");
    if (command == WizardCommand::finish) result.push_back("createDefaultContracts");
    if (command == WizardCommand::resume) result.push_back("connectionEpoch");
    return result;
}
bool availability(const J* value) {
    return oneOf(value, {"missing", "unreadable", "encoding_error", "too_large", "invalid", "repairable", "valid"});
}
bool readableAvailability(const J* value) { return oneOf(value, {"valid", "repairable", "invalid"}); }
bool snapshotMetadata(const J& value) {
    if (!availability(value.find("availability")) || !absoluteConfigPath(value.find("configPath"))) return false;
    const bool readable = readableAvailability(value.find("availability"));
    const auto* revision = value.find("revisionToken");
    const auto* reason = value.find("reasonCode");
    return revision && reason && (readable ? token(revision) : revision->isNull()) &&
        (oneOf(value.find("availability"), {"valid"}) ? reason->isNull() : token(reason));
}
bool documentMatches(const J* document, const J* declared) {
    if (!text(document) || !normalizeConfigText(document->stringValue())) return false;
    // The service already removed the one permitted file BOM. Any remaining
    // BOM is part of invalid JSON and must remain editable as such.
    const bool jsonSyntax = Json::parse(document->stringValue()).ok();
    ConfigWarnings warnings;
    const bool valid = jsonSyntax && parseAndValidateConfig(document->stringValue(), warnings).ok();
    if (oneOf(declared, {"valid"})) return valid;
    const bool repairable = jsonSyntax && !valid && analyzeWizardDocument(document->stringValue()).ok();
    return oneOf(declared, {"repairable"}) ? repairable : oneOf(declared, {"invalid"}) && !valid && !repairable;
}
std::optional<ConnectionState> vpnState(const J* value) {
    if (!text(value)) return {};
    for (auto state : {ConnectionState::disconnected, ConnectionState::connecting, ConnectionState::connected,
         ConnectionState::recovering, ConnectionState::error})
        if (value->stringValue() == connectionStateName(state)) return state;
    return {};
}
std::optional<ConnectionPhase> vpnPhase(const J* value) {
    if (!text(value)) return {};
    for (auto phase : {ConnectionPhase::idle, ConnectionPhase::health, ConnectionPhase::info,
         ConnectionPhase::access_key_check, ConnectionPhase::starting_tunnel, ConnectionPhase::running, ConnectionPhase::stopping})
        if (value->stringValue() == connectionPhaseName(phase)) return phase;
    return {};
}
Result<WizardReceipt> parseReceipt(const J& value) {
    if (!shape(value, {"commitId", "revisionToken", "commitState"}) || !uuid(value.find("commitId")) ||
        !token(value.find("revisionToken")) ||
        !oneOf(value.find("commitState"), {"confirmed"})) return malformed();
    return WizardReceipt{value.find("commitId")->stringValue(), value.find("revisionToken")->stringValue()};
}
}

const char* WizardIpc::commandName(WizardCommand command) { return names.at(static_cast<std::size_t>(command)); }
JsonValue WizardIpc::httpDiagnosticValue(const HttpDiagnostic& diagnostic) { return diagnosticValue(diagnostic); }
Result<HttpDiagnostic> WizardIpc::parseHttpDiagnostic(const JsonValue& value) { return parseDiagnostic(value); }
std::optional<WizardCommand> WizardIpc::command(std::string_view name) {
    for (std::size_t i = 0; i < names.size(); ++i) if (name == names[i]) return static_cast<WizardCommand>(i);
    return {};
}
Result<WizardRequest> WizardIpc::parseRequest(std::string_view json) {
    if (json.size() > kIpcMaxMessageBytes) return Error(ErrorCode::ipc_message_too_large);
    if (!isStrictUtf8(json)) return malformed();
    auto root = Json::parse(json);
    if (!root || !root->isObject()) return malformed();
    if (!number(root->find("ipcVersion"))) return malformed();
    if (jsonUnsignedOrZero(root->find("ipcVersion")) != 1) return Error(ErrorCode::ipc_unsupported_version);
    if (!uuid(root->find("requestId")) || !text(root->find("command"))) return malformed();
    auto selected = command(root->find("command")->stringValue());
    if (!selected) return Error(ErrorCode::ipc_unknown_command);
    const auto expected = requestFields(*selected);
    if (root->objectValue().size() != expected.size()) return malformed();
    for (auto key : expected) if (!root->find(key)) return malformed();
    WizardRequest request;
    request.command = *selected;
    request.requestId = root->find("requestId")->stringValue();
    for (const auto& [name, destination] : std::initializer_list<std::pair<const char*, std::string*>>{
        {"serviceInstanceId", &request.serviceInstanceId}, {"sessionId", &request.sessionId},
        {"clientAttemptId", &request.clientAttemptId}, {"operationId", &request.operationId}, {"commitId", &request.commitId}}) {
        if (const auto* value = root->find(name)) {
            if (!uuid(value)) return malformed();
            *destination = value->stringValue();
        }
    }
    for (const auto& [name, destination] : std::initializer_list<std::pair<const char*, std::string*>>{
        {"serverUrl", &request.fields.serverUrl}, {"serverSecret", &request.fields.serverSecret}}) {
        if (const auto* value = root->find(name)) {
            if (!text(value)) return malformed();
            *destination = value->stringValue();
        }
    }
    if (const auto* value = root->find("fields")) PHPVPN_TRY_ASSIGN(request.fields, parseFields(*value));
    if (const auto* value = root->find("attemptSequence")) {
        if (!number(value, true)) return malformed();
        request.attemptSequence = jsonUnsignedOrZero(value);
    }
    if (const auto* value = root->find("connectionEpoch")) {
        if (!number(value)) return malformed();
        request.connectionEpoch = jsonUnsignedOrZero(value);
    }
    if (const auto* value = root->find("createDefaultContracts")) {
        if (!value->isBool()) return malformed();
        request.createDefaultContracts = value->boolValue();
    }
    return request;
}
Result<std::string> WizardIpc::buildRequest(const WizardRequest& request) {
    JsonObject members;
    for (auto key : requestFields(request.command)) {
        J value;
        if (key == "ipcVersion") value = J::makeInteger(1);
        else if (key == "command") value = str(commandName(request.command));
        else if (key == "requestId") value = str(request.requestId);
        else if (key == "serviceInstanceId") value = str(request.serviceInstanceId);
        else if (key == "sessionId") value = str(request.sessionId);
        else if (key == "clientAttemptId") value = str(request.clientAttemptId);
        else if (key == "operationId") value = str(request.operationId);
        else if (key == "commitId") value = str(request.commitId);
        else if (key == "serverUrl") value = str(request.fields.serverUrl);
        else if (key == "serverSecret") value = str(request.fields.serverSecret);
        else if (key == "fields") value = fieldsValue(request.fields);
        else if (key == "attemptSequence") value = J::makeUnsignedInteger(request.attemptSequence);
        else if (key == "connectionEpoch") value = J::makeUnsignedInteger(request.connectionEpoch);
        else if (key == "createDefaultContracts") value = J::makeBool(request.createDefaultContracts);
        members.emplace_back(key, std::move(value));
    }
    auto json = Json::serialize(J::makeObject(std::move(members)));
    PHPVPN_TRY(parseRequest(json));
    return json;
}
J WizardIpc::fieldsValue(const WizardFields& fields) {
    return J::makeObject({{"serverUrl", str(fields.serverUrl)}, {"serverSecret", str(fields.serverSecret)},
        {"virtualIp", str(fields.virtualIp)}, {"accessKey", str(fields.accessKey)},
        {"useDefaultServerSecretInWizard", J::makeBool(fields.useDefaultServerSecretInWizard)},
        {"autoDetectIpAddressInWizard", J::makeBool(fields.autoDetectIpAddressInWizard)},
        {"useDefaultAccessKeyInWizard", J::makeBool(fields.useDefaultAccessKeyInWizard)}});
}
Result<WizardFields> WizardIpc::parseFields(const J& value) {
    if (shape(value, {"serverUrl", "serverSecret", "virtualIp", "accessKey"})) {
        for (const auto& member : value.objectValue()) if (!member.second.isString()) return malformed();
        return Error(ErrorCode::wizard_unsupported);
    }
    if (!shape(value, {"serverUrl", "serverSecret", "virtualIp", "accessKey",
        "useDefaultServerSecretInWizard", "autoDetectIpAddressInWizard", "useDefaultAccessKeyInWizard"})) return malformed();
    for (auto name : {"serverUrl", "serverSecret", "virtualIp", "accessKey"}) if (!value.find(name)->isString()) return malformed();
    for (auto name : {"useDefaultServerSecretInWizard", "autoDetectIpAddressInWizard", "useDefaultAccessKeyInWizard"})
        if (!value.find(name)->isBool()) return malformed();
    return WizardFields{value.find("serverUrl")->stringValue(), value.find("serverSecret")->stringValue(),
        value.find("virtualIp")->stringValue(), value.find("accessKey")->stringValue(),
        value.find("useDefaultServerSecretInWizard")->boolValue(), value.find("autoDetectIpAddressInWizard")->boolValue(),
        value.find("useDefaultAccessKeyInWizard")->boolValue()};
}
J WizardIpc::contextValue(const WizardContext& context) {
    const auto& r = context.runtime;
    return J::makeObject({{"httpHeaders", str(context.httpHeaders)}, {"vpnCidr", str(context.vpnCidr.toString())}, {"trustAnyTlsCertificate", J::makeBool(context.trustAnyTlsCertificate)},
        {"maxOutboundQueueBytes", J::makeInteger(r.maxOutboundQueueBytes)}, {"maxInboundQueueBytes", J::makeInteger(r.maxInboundQueueBytes)},
        {"minPerPeerQueueBytes", J::makeInteger(r.minPerPeerQueueBytes)}, {"maxResponseBodyBytes", J::makeInteger(r.maxResponseBodyBytes)},
        {"maxNdjsonLineBytes", J::makeInteger(r.maxNdjsonLineBytes)}, {"maxDataRecordsPerResponse", J::makeInteger(r.maxDataRecordsPerResponse)},
        {"responseTimeoutSeconds", J::makeInteger(r.responseTimeoutSeconds)}, {"timeSyncIntervalSeconds", J::makeInteger(r.timeSyncIntervalSeconds)}});
}
Result<WizardContext> WizardIpc::parseContext(const J& value) {
    if (!shape(value, {"httpHeaders", "vpnCidr", "trustAnyTlsCertificate", "maxOutboundQueueBytes", "maxInboundQueueBytes", "minPerPeerQueueBytes",
        "maxResponseBodyBytes", "maxNdjsonLineBytes", "maxDataRecordsPerResponse", "responseTimeoutSeconds", "timeSyncIntervalSeconds"}) ||
        !text(value.find("vpnCidr")) || !value.find("trustAnyTlsCertificate")->isBool()) return malformed();
    auto cidr = Ipv4Cidr::parse(value.find("vpnCidr")->stringValue());
    if (!cidr) return malformed();
    if (!text(value.find("httpHeaders")) || !normalizeHttpHeaders(value.find("httpHeaders")->stringValue())) return malformed();
    WizardContext context;
    context.httpHeaders = value.find("httpHeaders")->stringValue();
    context.vpnCidr = *cidr;
    context.trustAnyTlsCertificate = value.find("trustAnyTlsCertificate")->boolValue();
    auto& r = context.runtime;
    for (const auto& [name, target] : std::initializer_list<std::pair<const char*, std::int64_t*>>{
        {"maxOutboundQueueBytes", &r.maxOutboundQueueBytes}, {"maxInboundQueueBytes", &r.maxInboundQueueBytes},
        {"minPerPeerQueueBytes", &r.minPerPeerQueueBytes}, {"maxResponseBodyBytes", &r.maxResponseBodyBytes},
        {"maxNdjsonLineBytes", &r.maxNdjsonLineBytes}, {"maxDataRecordsPerResponse", &r.maxDataRecordsPerResponse},
        {"responseTimeoutSeconds", &r.responseTimeoutSeconds}, {"timeSyncIntervalSeconds", &r.timeSyncIntervalSeconds}}) {
        const auto* field = value.find(name);
        if (!number(field, true) || jsonUnsignedOrZero(field) > static_cast<std::uint64_t>((std::numeric_limits<std::int64_t>::max)())) return malformed();
        *target = static_cast<std::int64_t>(jsonUnsignedOrZero(field));
    }
    if (r.responseTimeoutSeconds > kMaxHttpTimeoutSeconds || r.maxNdjsonLineBytes > r.maxResponseBodyBytes) return malformed();
    const auto peers = static_cast<std::int64_t>(context.vpnCidr.clientAddressCount() - 1);
    // Divide before multiplying: hostile limits cannot wrap the reservation.
    if (r.minPerPeerQueueBytes > (std::numeric_limits<std::int64_t>::max)() / peers) return malformed();
    const auto reserved = peers * r.minPerPeerQueueBytes;
    if (r.maxInboundQueueBytes < reserved || r.maxOutboundQueueBytes < reserved) return malformed();
    return context;
}
J WizardIpc::viewValue(const WizardView& view) {
    JsonArray peers;
    for (const auto peer : view.existingDefaultContractPeers) peers.push_back(str(peer.toString()));
    return J::makeObject({{"sessionId", str(view.sessionId)}, {"serviceInstanceId", str(view.serviceInstanceId)},
        {"revisionToken", view.revisionToken.empty() ? J::makeNull() : str(view.revisionToken)},
        {"availability", str(configAvailabilityName(view.availability))}, {"mode", str(modes.at(static_cast<std::size_t>(view.mode)))},
        {"fields", view.fields ? fieldsValue(*view.fields) : J::makeNull()}, {"context", view.context ? contextValue(*view.context) : J::makeNull()},
        {"contractCount", J::makeUnsignedInteger(view.contractCount)}, {"existingDefaultContractPeers", J::makeArray(std::move(peers))},
        {"connectionEpoch", J::makeUnsignedInteger(view.connectionEpoch)},
        {"vpnState", str(connectionStateName(view.vpnState))}, {"vpnPhase", str(connectionPhaseName(view.vpnPhase))}});
}
Result<WizardView> WizardIpc::parseView(const J& value) {
    if (shape(value, {"sessionId", "serviceInstanceId", "revisionToken", "availability", "mode", "fields", "context", "contractCount", "connectionEpoch", "vpnState", "vpnPhase"})) {
        // Diagnose only a fully valid historical view. Never downgrade or use it.
        J candidate = value;
        for (auto& [name, fields] : candidate.objectValue()) if (name == "fields" && !fields.isNull()) {
            if (parseFields(fields).code() != ErrorCode::wizard_unsupported) return malformed();
            for (auto flag : {"useDefaultServerSecretInWizard", "autoDetectIpAddressInWizard", "useDefaultAccessKeyInWizard"})
                fields.objectValue().emplace_back(flag, J::makeBool(false));
        }
        candidate.objectValue().emplace_back("existingDefaultContractPeers", J::makeArray({}));
        return parseView(candidate) ? Error(ErrorCode::wizard_unsupported) : malformed();
    }
    if (!shape(value, {"sessionId", "serviceInstanceId", "revisionToken", "availability", "mode", "fields", "context", "contractCount", "existingDefaultContractPeers", "connectionEpoch", "vpnState", "vpnPhase"}) ||
        !uuid(value.find("sessionId")) || !uuid(value.find("serviceInstanceId")) || !nullText(value.find("revisionToken")) ||
        !availability(value.find("availability")) || !number(value.find("contractCount")) || !number(value.find("connectionEpoch"))) return malformed();
    const auto mode = index(value.find("mode"), modes);
    const auto state = vpnState(value.find("vpnState"));
    const auto phase = vpnPhase(value.find("vpnPhase"));
    if (!mode || !state || !phase) return malformed();
    WizardView view;
    view.sessionId = value.find("sessionId")->stringValue();
    view.serviceInstanceId = value.find("serviceInstanceId")->stringValue();
    if (value.find("revisionToken")->isString()) view.revisionToken = value.find("revisionToken")->stringValue();
    view.mode = static_cast<WizardMode>(*mode);
    for (auto a : {ConfigAvailability::missing, ConfigAvailability::unreadable, ConfigAvailability::encoding_error,
        ConfigAvailability::too_large, ConfigAvailability::invalid, ConfigAvailability::repairable, ConfigAvailability::valid})
        if (value.find("availability")->stringValue() == configAvailabilityName(a)) view.availability = a;
    if (!value.find("fields")->isNull()) {
        WizardFields fields;
        PHPVPN_TRY_ASSIGN(fields, parseFields(*value.find("fields")));
        view.fields = std::move(fields);
    }
    if (!value.find("context")->isNull()) {
        WizardContext context;
        PHPVPN_TRY_ASSIGN(context, parseContext(*value.find("context")));
        view.context = context;
    }
    if (view.fields.has_value() != view.context.has_value()) return malformed();
    if (readableAvailability(value.find("availability")) ? !token(value.find("revisionToken")) :
        !value.find("revisionToken")->isNull()) return malformed();
    view.contractCount = jsonUnsignedOrZero(value.find("contractCount"));
    const auto* peers = value.find("existingDefaultContractPeers");
    if (!peers->isArray() || peers->arrayValue().size() > 14 || peers->arrayValue().size() > view.contractCount) return malformed();
    for (const auto& peerValue : peers->arrayValue()) {
        if (!peerValue.isString() || !view.context || !supportsDefaultContracts(view.context->vpnCidr)) return malformed();
        const auto peer = Ipv4Address::parse(peerValue.stringValue());
        if (!peer || (peer->value() & 0xffffff00) != view.context->vpnCidr.network().value() ||
            (peer->value() & 255) < 2 || (peer->value() & 255) > 15 ||
            (!view.existingDefaultContractPeers.empty() && ! (view.existingDefaultContractPeers.back() < *peer))) return malformed();
        view.existingDefaultContractPeers.push_back(*peer);
    }
    view.connectionEpoch = jsonUnsignedOrZero(value.find("connectionEpoch"));
    view.vpnState = *state; view.vpnPhase = *phase;
    if (!view.fields && (view.mode != WizardMode::reset || view.contractCount != 0 ||
        oneOf(value.find("availability"), {"missing", "valid", "repairable"}))) return malformed();
    if (view.mode == WizardMode::create && view.availability != ConfigAvailability::missing) return malformed();
    if (view.mode == WizardMode::edit && view.availability != ConfigAvailability::valid) return malformed();
    if (view.mode == WizardMode::repair && view.availability != ConfigAvailability::repairable) return malformed();
    if ((view.mode == WizardMode::create || view.mode == WizardMode::reset) && view.contractCount != 0) return malformed();
    return view;
}
WizardError WizardIpc::safeError(const Error& error, std::optional<int> status,
    std::optional<std::string> field, std::optional<std::string> url, std::optional<std::string> path) {
    std::string action = "Correct the reported issue, then choose Verify & continue for a new check.";
    if (error.code == ErrorCode::config_directory_security_invalid)
        action = "Ask an administrator to restart the PHPVPN service to restore the folder permissions, then inspect the file with Edit Config. Startup may already have recreated a missing file. Restarting the service disconnects the VPN.";
    else if ((std::string_view(error.name()).starts_with("config_") && (!field || *field == "configuration")) ||
        error.code == ErrorCode::journal_corrupt || error.code == ErrorCode::journal_ambiguous ||
        error.code == ErrorCode::journal_missing)
        action = "Check the reported issue and the configuration file state, then invoke the action again when resolved. Contact an administrator if needed.";
    else if (error.code == ErrorCode::server_unauthorized)
        action = "Check SERVER_SECRET and ACCESS_KEYS for this virtual IP in the server file using your hosting control panel, or contact its administrator.";
    else if (error.code == ErrorCode::busy) action = "Wait for the current operation to finish, then try again.";
    else if (error.code == ErrorCode::wizard_session_expired) action = "Open a new Configuration Wizard.";
    else if (error.code == ErrorCode::wizard_commit_unknown) action = "Check the existing commit receipt. Do not start another write automatically.";
    else if (error.code == ErrorCode::wizard_connection_interrupted) action = "Choose Disconnect & continue to disconnect the VPN for everyone, then repeat the checks.";
    else if (error.code == ErrorCode::wizard_contract_self_conflict) action = "Choose another virtual IP, or correct the contract through Edit Config before opening a new wizard.";
    else if (error.code == ErrorCode::wizard_unsupported) action = "Update both the PHPVPN service and tray to the same version, then open Configuration Wizard again.";
    return {error.name(), httpHeadersErrorDescription(error), error.recoverable(), std::move(field), status,
        std::move(action), std::move(url), std::move(path)};
}
J WizardIpc::errorValue(const WizardError& error) {
    JsonObject members{{"code", str(error.code)}, {"description", str(error.description)},
        {"recoverable", J::makeBool(error.recoverable)}, {"field", nullable(error.field)},
        {"httpStatus", error.httpStatus ? J::makeInteger(*error.httpStatus) : J::makeNull()},
        {"suggestedAction", str(error.suggestedAction)}};
    if (error.serverUrl) members.emplace_back("serverUrl", str(*error.serverUrl));
    if (error.configPath) members.emplace_back("configPath", str(*error.configPath));
    return J::makeObject(std::move(members));
}
Result<WizardError> WizardIpc::parseError(const J& value) {
    if (!value.isObject()) return malformed();
    if (value.objectValue().size() != 6u + (value.find("serverUrl") ? 1u : 0u) + (value.find("configPath") ? 1u : 0u)) return malformed();
    for (auto key : {"code", "description", "recoverable", "field", "httpStatus", "suggestedAction"}) if (!value.find(key)) return malformed();
    for (const auto& [key, item] : value.objectValue()) {
        (void)item;
        if (key != "code" && key != "description" && key != "recoverable" && key != "field" && key != "httpStatus" &&
            key != "suggestedAction" && key != "serverUrl" && key != "configPath") return malformed();
    }
    if (!text(value.find("code")) || !text(value.find("description")) || !value.find("recoverable")->isBool() ||
        !text(value.find("suggestedAction")) || (!value.find("field")->isNull() &&
        !oneOf(value.find("field"), {"serverUrl", "serverSecret", "virtualIp", "accessKey", "configuration"}))) return malformed();
    const auto* status = value.find("httpStatus");
    if (!status->isNull() && (!number(status) || jsonUnsignedOrZero(status) < 100 || jsonUnsignedOrZero(status) > 599)) return malformed();
    WizardError error;
    error.code = value.find("code")->stringValue(); error.description = value.find("description")->stringValue();
    error.recoverable = value.find("recoverable")->boolValue(); error.suggestedAction = value.find("suggestedAction")->stringValue();
    if (!value.find("field")->isNull()) error.field = value.find("field")->stringValue();
    if (!status->isNull()) error.httpStatus = static_cast<int>(jsonUnsignedOrZero(status));
    for (const auto& [name, target] : std::initializer_list<std::pair<const char*, std::optional<std::string>*>>{
        {"serverUrl", &error.serverUrl}, {"configPath", &error.configPath}}) {
        if (const auto* item = value.find(name)) { if (!text(item)) return malformed(); *target = item->stringValue(); }
    }
    if (error.configPath && !absoluteConfigPath(value.find("configPath"))) return malformed();
    return error;
}
J WizardIpc::receiptValue(const WizardReceipt& receipt) {
    return J::makeObject({{"commitId", str(receipt.commitId)}, {"revisionToken", str(receipt.revisionToken)}, {"commitState", str("confirmed")}});
}
J WizardIpc::operationValue(const WizardOperationResult& result) {
    J payload;
    if (result.state == WizardOperationState::succeeded) {
        if (result.kind == WizardOperationKind::finish && result.receipt) payload = receiptValue(*result.receipt);
        else if (result.kind == WizardOperationKind::secret)
            payload = J::makeObject({{"verified", J::makeBool(true)}, {"suggestedIp", nullable(result.suggestedIp)}});
        else if (result.kind == WizardOperationKind::access)
            payload = J::makeObject({{"verified", J::makeBool(true)}, {"discardedRecordCount", J::makeUnsignedInteger(result.discardedRecordCount)}});
        else payload = J::makeObject({{"verified", J::makeBool(true)}});
    }
    auto value = J::makeObject({{"operationId", str(result.operationId)}, {"kind", str(kinds.at(static_cast<std::size_t>(result.kind)))},
        {"operationState", str(states.at(static_cast<std::size_t>(result.state)))}, {"connectionEpoch", J::makeUnsignedInteger(result.connectionEpoch)},
        {"clientAttemptId", nullable(result.clientAttemptId)}, {"attemptSequence", result.attemptSequence ? J::makeUnsignedInteger(*result.attemptSequence) : J::makeNull()},
        {"error", result.error ? errorValue(*result.error) : J::makeNull()}, {"payload", std::move(payload)}});
    if (result.httpDiagnostic) value.objectValue().emplace_back("httpDiagnostic", diagnosticValue(*result.httpDiagnostic));
    return value;
}
Result<WizardOperationResult> WizardIpc::parseOperation(const J& value) {
    const auto* diagnostic = value.find("httpDiagnostic");
    const bool validShape = diagnostic
        ? shape(value, {"operationId", "kind", "operationState", "connectionEpoch", "clientAttemptId", "attemptSequence", "error", "payload", "httpDiagnostic"})
        : shape(value, {"operationId", "kind", "operationState", "connectionEpoch", "clientAttemptId", "attemptSequence", "error", "payload"});
    if (!validShape ||
        !uuid(value.find("operationId")) || !number(value.find("connectionEpoch"))) return malformed();
    const auto kind = index(value.find("kind"), kinds), state = index(value.find("operationState"), states);
    if (!kind || !state) return malformed();
    WizardOperationResult result;
    result.operationId = value.find("operationId")->stringValue();
    result.kind = static_cast<WizardOperationKind>(*kind); result.state = static_cast<WizardOperationState>(*state);
    result.connectionEpoch = jsonUnsignedOrZero(value.find("connectionEpoch"));
    if (result.kind == WizardOperationKind::finish) {
        if (!value.find("clientAttemptId")->isNull() || !value.find("attemptSequence")->isNull()) return malformed();
    } else {
        if (!uuid(value.find("clientAttemptId")) || !number(value.find("attemptSequence"), true)) return malformed();
        result.clientAttemptId = value.find("clientAttemptId")->stringValue();
        result.attemptSequence = jsonUnsignedOrZero(value.find("attemptSequence"));
    }
    if (!value.find("error")->isNull()) {
        WizardError error;
        PHPVPN_TRY_ASSIGN(error, parseError(*value.find("error")));
        result.error = std::move(error);
    }
    const auto& payload = *value.find("payload");
    if (diagnostic) {
        if (result.state != WizardOperationState::failed || !result.error || !payload.isNull() ||
            (result.kind != WizardOperationKind::url && result.kind != WizardOperationKind::secret)) return malformed();
        const auto& error = *result.error;
        if (error.serverUrl || error.configPath || error.code.size() > 128 || error.description.size() > 4096 || error.suggestedAction.size() > 4096 ||
            error.field != (result.kind == WizardOperationKind::url ? "serverUrl" : "serverSecret")) return malformed();
        HttpDiagnostic parsed;
        PHPVPN_TRY_ASSIGN(parsed, parseDiagnostic(*diagnostic));
        if (result.kind == WizardOperationKind::url && parsed.action != HttpAction::health) return malformed();
        result.httpDiagnostic = std::move(parsed);
    }
    if (result.state != WizardOperationState::succeeded) {
        if (!payload.isNull()) return malformed();
        if (result.state == WizardOperationState::failed && !result.error) return malformed();
        if ((result.state == WizardOperationState::pending || result.state == WizardOperationState::running) && result.error) return malformed();
        return result;
    }
    if (result.error) return malformed();
    if (result.kind == WizardOperationKind::finish) {
        WizardReceipt receipt;
        PHPVPN_TRY_ASSIGN(receipt, parseReceipt(payload));
        result.receipt = std::move(receipt);
    } else {
        if (!payload.find("verified") || !payload.find("verified")->isBool() || !payload.find("verified")->boolValue()) return malformed();
        if (result.kind == WizardOperationKind::url && !shape(payload, {"verified"})) return malformed();
        if (result.kind == WizardOperationKind::secret) {
            if (!shape(payload, {"verified", "suggestedIp"}) || !nullText(payload.find("suggestedIp"))) return malformed();
            if (payload.find("suggestedIp")->isString()) {
                if (!Ipv4Address::parse(payload.find("suggestedIp")->stringValue())) return malformed();
                result.suggestedIp = payload.find("suggestedIp")->stringValue();
            }
        }
        if (result.kind == WizardOperationKind::access) {
            if (!shape(payload, {"verified", "discardedRecordCount"}) || !number(payload.find("discardedRecordCount"))) return malformed();
            result.discardedRecordCount = jsonUnsignedOrZero(payload.find("discardedRecordCount"));
        }
    }
    return result;
}
std::string WizardIpc::buildResult(std::string_view requestId, J value) {
    auto json = Json::serialize(J::makeObject({{"ipcVersion", J::makeInteger(1)}, {"requestId", str(requestId)}, {"result", std::move(value)}}));
    if (json.size() > kIpcMaxMessageBytes) return buildError(requestId, safeError(Error(ErrorCode::ipc_message_too_large)));
    return json;
}
std::string WizardIpc::buildError(std::string_view requestId, const WizardError& error) {
    return Json::serialize(J::makeObject({{"ipcVersion", J::makeInteger(1)}, {"requestId", str(requestId)}, {"error", errorValue(error)}}));
}
Result<WizardReply> WizardIpc::parseReply(WizardCommand command, std::string_view requestId, std::string_view json) {
    if (json.size() > kIpcMaxMessageBytes) return Error(ErrorCode::ipc_message_too_large);
    if (!isStrictUtf8(json)) return malformed();
    auto root = Json::parse(json);
    if (!root || !number(root->find("ipcVersion")) || jsonUnsignedOrZero(root->find("ipcVersion")) != 1 ||
        !uuid(root->find("requestId")) || root->find("requestId")->stringValue() != requestId) return malformed();
    if (shape(*root, {"ipcVersion", "requestId", "error"})) {
        const auto& legacy = *root->find("error");
        // The old service has exactly this three-member error envelope. Only
        // its correlated unknown-command error permits compatibility mapping.
        // Never render its supplied description or relax the modern parser.
        if (shape(legacy, {"code", "description", "recoverable"}) &&
            oneOf(legacy.find("code"), {"ipc_unknown_command"}) && text(legacy.find("description")) &&
            legacy.find("recoverable")->isBool() && !legacy.find("recoverable")->boolValue())
            return WizardReply{J::makeNull(), safeError(Error(ErrorCode::wizard_unsupported))};
        WizardError error;
        PHPVPN_TRY_ASSIGN(error, parseError(*root->find("error")));
        return WizardReply{J::makeNull(), std::move(error)};
    }
    if (!shape(*root, {"ipcVersion", "requestId", "result"})) return malformed();
    const auto& value = *root->find("result");
    bool valid = false;
    switch (command) {
        case WizardCommand::open: case WizardCommand::reset: case WizardCommand::resume: {
            auto view = parseView(value);
            if (!view) return view.error();
            if (command == WizardCommand::reset && (view->mode != WizardMode::reset || !view->fields)) return malformed();
            if (command == WizardCommand::open && view->mode == WizardMode::reset && view->fields) return malformed();
            valid = true; break;
        }
        case WizardCommand::get_operation:
            PHPVPN_TRY(parseOperation(value)); valid = true; break;
        case WizardCommand::check_url: case WizardCommand::check_secret: case WizardCommand::check_access: case WizardCommand::finish:
            valid = shape(value, {"operationId", "operationState"}) && uuid(value.find("operationId")) && oneOf(value.find("operationState"), {"pending"}); break;
        case WizardCommand::cancel_operation:
            valid = shape(value, {"operationId", "operationState"}) && uuid(value.find("operationId")) &&
                oneOf(value.find("operationState"), {"cancelling", "cancelled", "succeeded", "failed", "interrupted"}); break;
        case WizardCommand::cancel_attempt:
            valid = shape(value, {"attemptSequence", "attemptState"}) && number(value.find("attemptSequence"), true) && oneOf(value.find("attemptState"), {"cancelling", "cancelled", "closed"}); break;
        case WizardCommand::keepalive:
            valid = shape(value, {"alive", "leaseDurationMs"}) && value.find("alive")->isBool() && value.find("alive")->boolValue() &&
                number(value.find("leaseDurationMs")) && jsonUnsignedOrZero(value.find("leaseDurationMs")) == kWizardLeaseMs; break;
        case WizardCommand::close:
            valid = shape(value, {"closed"}) && value.find("closed")->isBool() && value.find("closed")->boolValue(); break;
        case WizardCommand::get_default_config:
            if (shape(value, {"document"}) && text(value.find("document"))) {
                ConfigWarnings warnings;
                valid = parseAndValidateConfig(value.find("document")->stringValue(), warnings).ok();
            }
            break;
        case WizardCommand::get_config_document:
            valid = shape(value, {"configPath", "availability", "reasonCode", "document", "revisionToken"}) && snapshotMetadata(value) &&
                (oneOf(value.find("availability"), {"missing"}) ? value.find("document")->isNull() :
                    documentMatches(value.find("document"), value.find("availability")));
            break;
        case WizardCommand::create_default_config:
            valid = shape(value, {"created", "configPath", "availability", "document", "revisionToken"}) && value.find("created")->isBool() &&
                absoluteConfigPath(value.find("configPath")) && token(value.find("revisionToken")) &&
                documentMatches(value.find("document"), value.find("availability")) &&
                (!value.find("created")->boolValue() || oneOf(value.find("availability"), {"valid"})); break;
        case WizardCommand::get_config_state:
            valid = shape(value, {"availability", "reasonCode", "configPath", "revisionToken", "serviceInstanceId", "connectionEpoch", "vpnState", "vpnPhase"}) &&
                snapshotMetadata(value) &&
                uuid(value.find("serviceInstanceId")) && number(value.find("connectionEpoch")) && vpnState(value.find("vpnState")).has_value() && vpnPhase(value.find("vpnPhase")).has_value(); break;
        case WizardCommand::get_commit:
            if (!shape(value, {"commitState", "receipt", "error"}) || !oneOf(value.find("commitState"), {"pending", "confirmed", "failed", "unknown"})) return malformed();
            if (oneOf(value.find("commitState"), {"confirmed"})) {
                PHPVPN_TRY(parseReceipt(*value.find("receipt")));
                valid = value.find("error")->isNull();
            } else if (oneOf(value.find("commitState"), {"failed"})) {
                PHPVPN_TRY(parseError(*value.find("error")));
                valid = value.find("receipt")->isNull();
            } else valid = value.find("receipt")->isNull() && value.find("error")->isNull();
            break;
    }
    if (!valid) return malformed();
    return WizardReply{value, {}};
}
} // namespace phpvpn
