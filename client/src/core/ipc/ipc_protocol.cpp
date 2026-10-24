#include "core/protocol/response_parser.h"
#include "core/protocol/http_headers.h"
#include "core/ipc/ipc_protocol.h"

#include <array>

#include "core/protocol/json.h"
#include "core/protocol/uuid.h"
#include "core/ipc/wizard_ipc.h"

namespace phpvpn {
namespace {

struct CommandName {
    IpcCommand command;
    const char* name;
};

constexpr std::array<CommandName, 9> kCommandNames = {{
    {IpcCommand::get_status, "get-status"},
    {IpcCommand::connect, "connect"},
    {IpcCommand::disconnect, "disconnect"},
    {IpcCommand::get_info, "get-info"},
    {IpcCommand::get_server_info, "get-server-info"},
    {IpcCommand::get_config, "get-config"},
    {IpcCommand::set_config, "set-config"},
    {IpcCommand::get_diagnostics, "get-diagnostics"},
    {IpcCommand::dismiss_connect_diagnostic, "dismiss-connect-diagnostic"},
}};

JsonValue tristateValue(Tristate value) {
    return JsonValue::makeString(tristateName(value));
}

JsonValue safeErrorValue(const std::optional<SafeError>& error) {
    if (!error.has_value()) {
        return JsonValue::makeNull();
    }
    JsonObject members;
    members.emplace_back("code", JsonValue::makeString(error->code));
    members.emplace_back("description", JsonValue::makeString(error->description));
    members.emplace_back("recoverable", JsonValue::makeBool(error->recoverable));
    return JsonValue::makeObject(std::move(members));
}

JsonValue queueValue(const QueueDiagnostics& queue) {
    JsonObject members;
    members.emplace_back("itemCount", JsonValue::makeInteger(static_cast<std::int64_t>(queue.itemCount)));
    members.emplace_back("occupiedBytes", JsonValue::makeInteger(queue.occupiedBytes));
    members.emplace_back("limitBytes", JsonValue::makeInteger(queue.limitBytes));
    members.emplace_back("peerQueueCount",
                         JsonValue::makeInteger(static_cast<std::int64_t>(queue.peerQueueCount)));
    members.emplace_back("activeSubBucketCount", JsonValue::makeUnsignedInteger(queue.activeSubBucketCount));
    members.emplace_back("inFlightBytes", JsonValue::makeInteger(queue.inFlightBytes));
    members.emplace_back("queuedItemCount", JsonValue::makeUnsignedInteger(queue.queuedItemCount));
    return JsonValue::makeObject(std::move(members));
}

void appendTrafficCounters(JsonObject& target, const TrafficCountersSnapshot& counters) {
    target.emplace_back("dataPollsTotal", JsonValue::makeUnsignedInteger(counters.dataPollsTotal));
    target.emplace_back("dataPollsEmpty", JsonValue::makeUnsignedInteger(counters.dataPollsEmpty));
    target.emplace_back("dataPollsWithPayload", JsonValue::makeUnsignedInteger(counters.dataPollsWithPayload));
    target.emplace_back("serializedDataRequestBodyBytes", JsonValue::makeUnsignedInteger(counters.serializedDataRequestBodyBytes));
    target.emplace_back("serializedDataResponseBodyBytes", JsonValue::makeUnsignedInteger(counters.serializedDataResponseBodyBytes));
    target.emplace_back("ipv4PacketsSent", JsonValue::makeUnsignedInteger(counters.ipv4PacketsSent));
    target.emplace_back("ipv4PacketsReceived", JsonValue::makeUnsignedInteger(counters.ipv4PacketsReceived));
    target.emplace_back("ipv4BytesSent", JsonValue::makeUnsignedInteger(counters.ipv4BytesSent));
    target.emplace_back("ipv4BytesReceived", JsonValue::makeUnsignedInteger(counters.ipv4BytesReceived));
}

std::int64_t asCounter(std::uint64_t value) noexcept {
    // The counters are aggregated locally and never approach the signed limit,
    // but the conversion is still clamped so no negative number can be emitted.
    constexpr std::uint64_t limit = 0x7FFFFFFFFFFFFFFFull;
    return static_cast<std::int64_t>(value > limit ? limit : value);
}

}  // namespace

const char* ipcCommandName(IpcCommand command) noexcept {
    for (const CommandName& entry : kCommandNames) {
        if (entry.command == command) {
            return entry.name;
        }
    }
    return "get-status";
}

std::optional<IpcCommand> parseIpcCommand(std::string_view name) noexcept {
    for (const CommandName& entry : kCommandNames) {
        if (name == entry.name) {
            return entry.command;
        }
    }
    return std::nullopt;
}

Result<IpcRequest> IpcProtocol::parseRequest(std::string_view json) {
    if (json.size() > kIpcMaxMessageBytes) {
        return Error(ErrorCode::ipc_message_too_large, "the request exceeds the accepted size");
    }

    auto document = Json::parse(json);
    if (!document.ok()) {
        return Error(ErrorCode::ipc_malformed, "the request is not a valid JSON object");
    }
    const JsonValue& root = document.value();
    if (!root.isObject()) {
        return Error(ErrorCode::ipc_malformed, "the request is not a JSON object");
    }

    const JsonValue* version = root.find("ipcVersion");
    const JsonValue* requestId = root.find("requestId");
    const JsonValue* command = root.find("command");
    const JsonValue* config = root.find("config");

    if (version == nullptr || !version->isInteger()) {
        return Error(ErrorCode::ipc_malformed, "the contract version is missing");
    }
    if (version->integerValue() != kIpcVersion) {
        return Error(ErrorCode::ipc_unsupported_version, "the contract version is not supported");
    }
    if (requestId == nullptr || !requestId->isString() ||
        !Uuid::isValidV4(requestId->stringValue())) {
        return Error(ErrorCode::ipc_malformed, "the request identifier is not a UUIDv4");
    }
    if (command == nullptr || !command->isString()) {
        return Error(ErrorCode::ipc_malformed, "the command is missing");
    }

    const auto parsedCommand = parseIpcCommand(command->stringValue());
    if (!parsedCommand.has_value()) {
        return Error(ErrorCode::ipc_unknown_command, "the command is not part of this contract");
    }

    // `config` is allowed for `set-config` only, and every other member is
    // rejected as an extra field.
    const std::size_t expectedMembers =
        *parsedCommand == IpcCommand::dismiss_connect_diagnostic ? 5u : *parsedCommand == IpcCommand::set_config ? 4u : 3u;
    if (root.objectValue().size() != expectedMembers) {
        return Error(ErrorCode::ipc_malformed, "the request carries unexpected members");
    }
    if (*parsedCommand == IpcCommand::set_config) {
        if (config == nullptr || !config->isString()) {
            return Error(ErrorCode::ipc_malformed, "the configuration document is missing");
        }
    } else if (config != nullptr) {
        return Error(ErrorCode::ipc_malformed, "the request carries unexpected members");
    }

    IpcRequest request;
    request.command = *parsedCommand;
    request.requestId = requestId->stringValue();
    if (*parsedCommand == IpcCommand::dismiss_connect_diagnostic) {
        const auto* epoch = root.find("connectionEpoch");
        if (!epoch || (!epoch->isUnsignedInteger() && !epoch->isInteger()) ||
            (epoch->isInteger() && epoch->integerValue() <= 0) || jsonUnsignedOrZero(epoch) == 0)
            return Error(ErrorCode::ipc_malformed);
        request.connectionEpoch = jsonUnsignedOrZero(epoch);
        const auto* attempt = root.find("connectAttemptId");
        if (!attempt || !attempt->isString() || !Uuid::isValidV4(attempt->stringValue())) return Error(ErrorCode::ipc_malformed);
        request.connectAttemptId = attempt->stringValue();
    }
    if (config != nullptr) {
        request.config = config->stringValue();
    }
    return request;
}

std::string IpcProtocol::buildRequest(IpcCommand command,
                                      std::string_view requestId,
                                      std::string_view config) {
    JsonObject members;
    members.emplace_back("ipcVersion", JsonValue::makeInteger(kIpcVersion));
    members.emplace_back("requestId", JsonValue::makeString(std::string(requestId)));
    members.emplace_back("command", JsonValue::makeString(ipcCommandName(command)));
    if (command == IpcCommand::set_config) {
        members.emplace_back("config", JsonValue::makeString(std::string(config)));
    }
    if (command == IpcCommand::dismiss_connect_diagnostic) {
        auto payload = Json::parse(config);
        for (const auto* name : {"connectionEpoch", "connectAttemptId"}) {
            const auto* value = payload && payload->isObject() ? payload->find(name) : nullptr;
            members.emplace_back(name, value ? *value : JsonValue::makeNull());
        }
    }
    return Json::serialize(JsonValue::makeObject(std::move(members)));
}
std::string IpcProtocol::dismissDiagnosticPayload(std::uint64_t epoch, std::string_view attemptId) {
    return Json::serialize(JsonValue::makeObject({{"connectionEpoch", JsonValue::makeUnsignedInteger(epoch)},
        {"connectAttemptId", JsonValue::makeString(std::string(attemptId))}}));
}

std::string IpcProtocol::buildServerInfoResponse(std::string_view requestId, const ServerInfoSnapshot& snapshot) {
    auto local = parseMaskedContracts(maskedContractsJson(snapshot.localMaskedCommunicationContracts), snapshot.localAddress);
    auto info = ResponseParser::parseInfoPlaintext(snapshot.infoPlaintext, snapshot.localAddress);
    if (!local || !info) return buildError(requestId, Error(ErrorCode::response_invalid));
    auto response = Json::serialize(JsonValue::makeObject({{"ipcVersion", JsonValue::makeInteger(kIpcVersion)},
        {"requestId", JsonValue::makeString(std::string(requestId))}, {"result", JsonValue::makeObject({
        {"infoPlaintext", JsonValue::makeString(snapshot.infoPlaintext)}, {"localAddress", JsonValue::makeString(snapshot.localAddress.toString())},
        {"localMaskedCommunicationContracts", maskedContractsJson(snapshot.localMaskedCommunicationContracts)}})}}));
    if (response.size() > kIpcMaxMessageBytes) return buildError(requestId, Error(ErrorCode::ipc_message_too_large));
    return response;
}
std::string IpcProtocol::buildAcknowledgement(std::string_view requestId) {
    JsonObject members;
    members.emplace_back("ipcVersion", JsonValue::makeInteger(kIpcVersion));
    members.emplace_back("requestId", JsonValue::makeString(std::string(requestId)));
    members.emplace_back("result", JsonValue::makeString("ok"));
    return Json::serialize(JsonValue::makeObject(std::move(members)));
}

std::string IpcProtocol::buildStatusResponse(std::string_view requestId,
                                             const StatusSnapshot& snapshot) {
    JsonObject status;
    status.emplace_back("connectionEpoch", JsonValue::makeUnsignedInteger(snapshot.connectionEpoch));
    status.emplace_back("connectAttemptId", snapshot.connectAttemptId ? JsonValue::makeString(*snapshot.connectAttemptId) : JsonValue::makeNull());
    if (snapshot.httpDiagnostic) status.emplace_back("httpDiagnostic", WizardIpc::httpDiagnosticValue(*snapshot.httpDiagnostic));
    status.emplace_back("state", JsonValue::makeString(connectionStateName(snapshot.state)));
    status.emplace_back("phase", JsonValue::makeString(connectionPhaseName(snapshot.phase)));
    status.emplace_back("stateChangedAt", JsonValue::makeString(snapshot.stateChangedAtUtc));
    status.emplace_back("connectionStartedAt", snapshot.connectionStartedAtUtc
        ? JsonValue::makeString(*snapshot.connectionStartedAtUtc) : JsonValue::makeNull());
    status.emplace_back("serverUrlCheck", JsonValue::makeString(checkpointResultName(snapshot.serverUrlCheck)));
    status.emplace_back("serverSecretCheck", JsonValue::makeString(checkpointResultName(snapshot.serverSecretCheck)));
    status.emplace_back("accessKeyCheck", JsonValue::makeString(checkpointResultName(snapshot.accessKeyCheck)));
    status.emplace_back("communicationContractsCheck", JsonValue::makeString(checkpointResultName(snapshot.communicationContractsCheck)));
    status.emplace_back("serverReachable", tristateValue(snapshot.serverReachable));
    status.emplace_back("serverVerified", tristateValue(snapshot.serverVerified));
    status.emplace_back("tunnelReady", tristateValue(snapshot.tunnelReady));
    status.emplace_back("lastError", safeErrorValue(snapshot.lastError));
    appendTrafficCounters(status, snapshot.counters);
    status.emplace_back("recoveryCount", JsonValue::makeInteger(asCounter(snapshot.recoveryCount)));
    status.emplace_back("clientVersion", JsonValue::makeString(snapshot.clientVersion));
    status.emplace_back("ipcVersion", JsonValue::makeInteger(snapshot.ipcVersion));

    JsonObject members;
    members.emplace_back("ipcVersion", JsonValue::makeInteger(kIpcVersion));
    members.emplace_back("requestId", JsonValue::makeString(std::string(requestId)));
    members.emplace_back("status", JsonValue::makeObject(std::move(status)));
    return Json::serialize(JsonValue::makeObject(std::move(members)));
}

std::string IpcProtocol::buildDiagnosticsResponse(std::string_view requestId,
                                                  const DiagnosticsSnapshot& snapshot) {
    JsonArray warnings;
    for (const std::string& warning : snapshot.configurationWarnings) {
        warnings.push_back(JsonValue::makeString(warning));
    }

    JsonObject counters;
    appendTrafficCounters(counters, snapshot.counters);
    counters.emplace_back("packetsDropped", JsonValue::makeInteger(asCounter(snapshot.packetsDropped)));
    counters.emplace_back("batchesRejected", JsonValue::makeInteger(asCounter(snapshot.batchesRejected)));
    counters.emplace_back("responsesRejected",
                          JsonValue::makeInteger(asCounter(snapshot.responsesRejected)));
    counters.emplace_back("peakResponseCiphertextBytes",
                          JsonValue::makeInteger(asCounter(snapshot.peakResponseCiphertextBytes)));

    counters.emplace_back("queueExpiredPackets", JsonValue::makeUnsignedInteger(snapshot.queueExpiredPackets));
    counters.emplace_back("outboundQueueExpiredPackets", JsonValue::makeUnsignedInteger(snapshot.outboundQueueExpiredPackets));
    counters.emplace_back("inboundQueueExpiredPackets", JsonValue::makeUnsignedInteger(snapshot.inboundQueueExpiredPackets));
    JsonObject diagnostics;
    diagnostics.emplace_back("state", JsonValue::makeString(connectionStateName(snapshot.state)));
    diagnostics.emplace_back("phase", JsonValue::makeString(connectionPhaseName(snapshot.phase)));
    diagnostics.emplace_back("clientVersion", JsonValue::makeString(snapshot.clientVersion));
    diagnostics.emplace_back("serviceUptimeSeconds",
                             JsonValue::makeInteger(snapshot.serviceUptimeSeconds));
    diagnostics.emplace_back("configurationValid", JsonValue::makeBool(snapshot.configurationValid));
    diagnostics.emplace_back("configurationWarnings", JsonValue::makeArray(std::move(warnings)));
    diagnostics.emplace_back("lastHealthVerified", tristateValue(snapshot.lastHealthVerified));
    diagnostics.emplace_back("lastInfoVerified", tristateValue(snapshot.lastInfoVerified));
    diagnostics.emplace_back("lastDataVerified", tristateValue(snapshot.lastDataVerified));
    diagnostics.emplace_back("lastRecoveryAt", JsonValue::makeString(snapshot.lastRecoveryAtUtc));
    diagnostics.emplace_back("recoveryCount", JsonValue::makeInteger(asCounter(snapshot.recoveryCount)));
    diagnostics.emplace_back("lastSoftRecoveryAt", JsonValue::makeString(snapshot.lastSoftRecoveryAtUtc));
    diagnostics.emplace_back("softRecoveryCount", JsonValue::makeInteger(asCounter(snapshot.softRecoveryCount)));
    diagnostics.emplace_back("tunnelReady", tristateValue(snapshot.tunnelReady));
    diagnostics.emplace_back("outboundQueue", queueValue(snapshot.outbound));
    diagnostics.emplace_back("inboundQueue", queueValue(snapshot.inbound));
    diagnostics.emplace_back("counters", JsonValue::makeObject(std::move(counters)));
    diagnostics.emplace_back("lastError", safeErrorValue(snapshot.lastError));

    JsonObject members;
    members.emplace_back("ipcVersion", JsonValue::makeInteger(kIpcVersion));
    members.emplace_back("requestId", JsonValue::makeString(std::string(requestId)));
    members.emplace_back("diagnostics", JsonValue::makeObject(std::move(diagnostics)));
    return Json::serialize(JsonValue::makeObject(std::move(members)));
}

std::string IpcProtocol::buildError(std::string_view requestId, const Error& error) {
    JsonObject payload;
    payload.emplace_back("code", JsonValue::makeString(error.name()));
    // Only the curated description travels over the pipe, never the internal
    // detail, a system message or a path. Header validation has a closed safe grammar.
    payload.emplace_back("description", JsonValue::makeString(httpHeadersErrorDescription(error)));
    payload.emplace_back("recoverable", JsonValue::makeBool(error.recoverable()));

    JsonObject members;
    members.emplace_back("ipcVersion", JsonValue::makeInteger(kIpcVersion));
    members.emplace_back("requestId", JsonValue::makeString(std::string(requestId)));
    members.emplace_back("error", JsonValue::makeObject(std::move(payload)));
    return Json::serialize(JsonValue::makeObject(std::move(members)));
}

}  // namespace phpvpn
