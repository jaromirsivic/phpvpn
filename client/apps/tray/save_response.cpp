#include "save_response.h"
#include "core/ipc/ipc_protocol.h"
#include "core/protocol/http_headers.h"
#include "core/protocol/json.h"
#include "core/protocol/uuid.h"

namespace phpvpn::tray {
SaveResult classifySaveResponse(const SaveOperation& operation, std::string_view requestId,
    std::string_view response, bool writeAttempted, std::uint64_t completedAtMs) {
    SaveResult result{operation, SaveOutcome::unknown, ErrorCode::response_invalid, {}};
    if (!writeAttempted) { result.outcome = SaveOutcome::notSent; result.error = ErrorCode::ipc_unavailable; return result; }
    if (!operation.deadlineMs || completedAtMs >= operation.deadlineMs || !Uuid::isValidV4(requestId) ||
        response.size() > kIpcMaxMessageBytes) return result;
    auto parsed = Json::parse(response);
    if (!parsed || !parsed->isObject() || parsed->objectValue().size() != 3) return result;
    const auto* version = parsed->find("ipcVersion");
    const auto* id = parsed->find("requestId");
    if (!version || !version->isInteger() || version->integerValue() != kIpcVersion ||
        !id || !id->isString() || id->stringValue() != requestId) return result;
    if (const auto* value = parsed->find("result")) {
        if (value->isString() && value->stringValue() == "ok") {
            result.outcome = SaveOutcome::confirmed; result.error = ErrorCode::none;
        }
        return result;
    }
    const auto* error = parsed->find("error");
    if (!error || !error->isObject() || error->objectValue().size() != 3) return result;
    const auto* code = error->find("code");
    const auto* description = error->find("description");
    const auto* recoverable = error->find("recoverable");
    if (!code || !code->isString() || !description || !description->isString() || !recoverable || !recoverable->isBool()) return result;
    // Only these existing errors prove rejection before ConfigFiles::publish.
    // config_write_failed, file/security/recovery errors and unknown codes do
    // not prove an unchanged disk and deliberately remain unknown.
    for (const auto candidate : {ErrorCode::busy, ErrorCode::ipc_malformed, ErrorCode::ipc_message_too_large,
        ErrorCode::ipc_unsupported_version, ErrorCode::ipc_unknown_command,
        ErrorCode::config_encoding_error, ErrorCode::config_too_large, ErrorCode::config_invalid_json,
        ErrorCode::config_invalid_value, ErrorCode::config_invalid_url, ErrorCode::config_invalid_cidr,
        ErrorCode::config_invalid_address, ErrorCode::config_invalid_key, ErrorCode::config_invalid_limit,
        ErrorCode::config_queue_budget, ErrorCode::wizard_contract_self_conflict}) {
        if (code->stringValue() == errorCodeName(candidate) && (description->stringValue() == errorCodeDescription(candidate) ||
            (candidate == ErrorCode::config_invalid_value && isHttpHeadersErrorDescription(description->stringValue()))) &&
            recoverable->boolValue() == errorCodeIsRecoverable(candidate)) {
            result.outcome = SaveOutcome::rejected; result.error = candidate;
            if (candidate == ErrorCode::config_invalid_value && isHttpHeadersErrorDescription(description->stringValue()))
                result.validationDescription = description->stringValue();
            break;
        }
    }
    return result;
}
}  // namespace phpvpn::tray
