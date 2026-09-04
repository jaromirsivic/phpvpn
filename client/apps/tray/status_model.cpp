#include "status_model.h"

#include <initializer_list>
#include <limits>

#include "core/protocol/json.h"
#include "core/protocol/iso8601.h"
#include "core/protocol/uuid.h"

namespace phpvpn::tray {
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
    const auto document = Json::parse(json);
    if (!document.ok() || !document.value().isObject()) return result;
    const auto& root = document.value();
    if (root.objectValue().size() != 3 || !versionOne(root.find("ipcVersion")) ||
        !nonemptyString(root.find("requestId")) || !Uuid::isValidV4(root.find("requestId")->stringValue())) return result;
    const auto* status = root.find("status");
    // client.md, get-status: a complete v1 snapshot, never a partial update.
    if (!status || !status->isObject() || status->objectValue().size() != 22 ||
        !versionOne(status->find("ipcVersion")) ||
        !nonemptyString(status->find("clientVersion")) ||
        !nonemptyString(status->find("stateChangedAt")) ||
        !unsignedNumber(status->find("recoveryCount")) ||
        !stringIn(status->find("state"), {"disconnected", "connecting", "connected", "recovering", "error"}) ||
        !stringIn(status->find("phase"), {"idle", "health", "info", "access-key-check", "starting-tunnel", "running", "stopping"}) ||
        !stringIn(status->find("serverReachable"), {"unknown", "yes", "no"}) ||
        !stringIn(status->find("serverVerified"), {"unknown", "yes", "no"}) ||
        !stringIn(status->find("tunnelReady"), {"unknown", "yes", "no"})) return result;
    for (const char* name : {"serverUrlCheck", "serverSecretCheck", "accessKeyCheck"}) {
        if (!stringIn(status->find(name), {"pending", "success", "failed"})) return result;
    }
    if (!Iso8601::isValid(status->find("stateChangedAt")->stringValue())) return result;
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
    result.valid = true;
    return result;
}

bool applyStatusSnapshot(TrayStatus& current, const TrayStatus& next) {
    if (!next.valid) return false;
    current = next;
    return true;
}
}  // namespace phpvpn::tray
