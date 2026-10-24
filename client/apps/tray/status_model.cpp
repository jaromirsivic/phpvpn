#include "status_model.h"

#include <initializer_list>
#include <limits>

#include "core/protocol/json.h"
#include "core/protocol/iso8601.h"
#include "core/protocol/uuid.h"
#include "core/ipc/wizard_ipc.h"
#include "core/ipc/ipc_protocol.h"
#include "platform/iface/transport.h"

namespace phpvpn::tray {
TrayIconState selectTrayIcon(const TrayStatus& status, bool unavailable, bool localOperation) {
    if (unavailable || !status.valid) return TrayIconState::disconnected;
    if (localOperation || status.phase == "stopping" || status.state == "connecting" || status.state == "recovering" ||
        (status.state == "connected" && status.phase == "reconnecting"))
        return TrayIconState::connecting;
    if (status.state == "connected" && status.phase == "running" && status.tunnelReady == "yes" && !status.hasError)
        return TrayIconState::connected;
    return TrayIconState::disconnected;
}
namespace {
bool stringIn(const JsonValue* value, std::initializer_list<std::string_view> allowed) {
    if (!value || !value->isString()) return false;
    for (const auto text : allowed) if (value->stringValue() == text) return true;
    return false;
}
bool nonemptyString(const JsonValue* value) {
    return value && value->isString() && !value->stringValue().empty();
}
bool unsignedNumber(const JsonValue* value) {
    return value && (value->isUnsignedInteger() ||
                     (value->isInteger() && value->integerValue() >= 0));
}
bool versionOne(const JsonValue* value) {
    return value && value->isInteger() && value->integerValue() == 1;
}
CheckpointResult checkpoint(const JsonValue* value) {
    if (value->stringValue() == "success") return CheckpointResult::success;
    if (value->stringValue() == "failed") return CheckpointResult::failed;
    return CheckpointResult::pending;
}
}  // namespace

TrayStatus parseStatusResponse(std::string_view json) {
    TrayStatus result;
    if (json.size() > kIpcMaxMessageBytes) return result;
    const auto document = Json::parse(json);
    if (!document.ok() || !document.value().isObject()) return result;
    const auto& root = document.value();
    if (root.objectValue().size() != 3 || !versionOne(root.find("ipcVersion")) ||
        !nonemptyString(root.find("requestId")) || !Uuid::isValidV4(root.find("requestId")->stringValue())) return result;
    const auto* status = root.find("status");
    // client.md, get-status: a complete v1 snapshot, never a partial update.
    if (!status || !status->isObject() || status->objectValue().size() != (status->find("httpDiagnostic") ? 27u : 26u) ||
        !unsignedNumber(status->find("connectionEpoch")) ||
        !versionOne(status->find("ipcVersion")) ||
        !nonemptyString(status->find("clientVersion")) ||
        !nonemptyString(status->find("stateChangedAt")) ||
        !unsignedNumber(status->find("recoveryCount")) ||
        !stringIn(status->find("state"), {"disconnected", "connecting", "connected", "recovering", "error"}) ||
        !stringIn(status->find("phase"), {"idle", "health", "info", "access-key-check", "communication-contracts-check", "starting-tunnel", "running", "reconnecting", "stopping"}) ||
        !stringIn(status->find("serverReachable"), {"unknown", "yes", "no"}) ||
        !stringIn(status->find("serverVerified"), {"unknown", "yes", "no"}) ||
        !stringIn(status->find("tunnelReady"), {"unknown", "yes", "no"})) return result;
    for (const char* name : {"serverUrlCheck", "serverSecretCheck", "accessKeyCheck", "communicationContractsCheck"}) {
        if (!stringIn(status->find(name), {"pending", "success", "failed"})) return result;
    }
    if (!Iso8601::isValid(status->find("stateChangedAt")->stringValue())) return result;
    const auto* started = status->find("connectionStartedAt");
    if (!started) return result;
    if (!started->isNull()) {
        if (!started->isString()) return result;
        result.connectionStartedAtUtcMs = Iso8601::parse(started->stringValue());
        if (!result.connectionStartedAtUtcMs) return TrayStatus{};
    }
    const auto& state = status->find("state")->stringValue();
    if ((state == "connected" && !result.connectionStartedAtUtcMs) ||
        ((state == "disconnected" || state == "connecting") && result.connectionStartedAtUtcMs)) return TrayStatus{};
    for (const auto& field : kCounterFields) {
        const auto* value = status->find(field.key);
        if (!unsignedNumber(value)) return TrayStatus{};
        result.counters.*(field.member) = jsonUnsignedOrZero(value);
    }
    const auto& counters = result.counters;
    if (counters.dataPollsEmpty > std::numeric_limits<std::uint64_t>::max() - counters.dataPollsWithPayload ||
        counters.dataPollsTotal != counters.dataPollsEmpty + counters.dataPollsWithPayload) return TrayStatus{};
    const auto* error = status->find("lastError");
    if (!error) return TrayStatus{};
    if (!error->isNull()) {
        if (!error->isObject() || error->objectValue().size() != 3 ||
            !nonemptyString(error->find("code")) || !nonemptyString(error->find("description")) ||
            !error->find("recoverable") || !error->find("recoverable")->isBool()) return TrayStatus{};
        // The service owns the safe public text. Never infer it from checkpoints.
        result.hasError = true;
        result.errorCode = error->find("code")->stringValue();
        result.errorDescription = error->find("description")->stringValue();
        result.errorRecoverable = error->find("recoverable")->boolValue();
    }
    result.state = status->find("state")->stringValue();
    result.phase = status->find("phase")->stringValue();
    result.tunnelReady = status->find("tunnelReady")->stringValue();
    result.serverUrl = checkpoint(status->find("serverUrlCheck"));
    result.serverSecret = checkpoint(status->find("serverSecretCheck"));
    result.accessKey = checkpoint(status->find("accessKeyCheck"));
    result.communicationContracts = checkpoint(status->find("communicationContractsCheck"));
    result.connectionEpoch = jsonUnsignedOrZero(status->find("connectionEpoch"));
    const auto* attemptId = status->find("connectAttemptId");
    if (!attemptId) return TrayStatus{};
    if (!attemptId->isNull()) {
        if (!attemptId->isString() || !Uuid::isValidV4(attemptId->stringValue())) return TrayStatus{};
        result.connectAttemptId = attemptId->stringValue();
    }
    if ((result.connectionEpoch != 0) != result.connectAttemptId.has_value()) return TrayStatus{};
    if (const auto* diagnostic = status->find("httpDiagnostic")) {
        if (!result.connectionEpoch || result.state != "error" || result.phase != "idle" || !result.hasError ||
            result.errorRecoverable || result.accessKey != CheckpointResult::pending || result.connectionStartedAtUtcMs)
            return TrayStatus{};
        auto parsed = WizardIpc::parseHttpDiagnostic(*diagnostic);
        if (!parsed) return TrayStatus{};
        if (parsed->action == HttpAction::health) {
            if (!((result.serverUrl == CheckpointResult::failed && result.serverSecret == CheckpointResult::pending) ||
                (result.serverUrl == CheckpointResult::success && result.serverSecret == CheckpointResult::failed))) return TrayStatus{};
        } else if (result.serverUrl != CheckpointResult::success || result.serverSecret != CheckpointResult::failed) return TrayStatus{};
        if (result.errorCode != "endpoint_unavailable_or_invalid" && result.errorCode != "server_verification_failed" &&
            result.errorCode != "communication_failed") return TrayStatus{};
        result.httpDiagnostic = std::move(*parsed);
    }
    result.valid = true;
    return result;
}

bool applyStatusSnapshot(TrayStatus& current, const TrayStatus& next) {
    if (!next.valid) return false;
    current = next;
    return true;
}
}  // namespace phpvpn::tray
