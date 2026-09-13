#pragma once
#include "core/wizard/wizard_types.h"

namespace phpvpn {
struct WizardReply {
    JsonValue result;
    std::optional<WizardError> error;
};
class WizardIpc {
public:
    static JsonValue httpDiagnosticValue(const HttpDiagnostic& diagnostic);
    static Result<HttpDiagnostic> parseHttpDiagnostic(const JsonValue& value);
    static const char* commandName(WizardCommand command);
    static std::optional<WizardCommand> command(std::string_view name);
    static Result<WizardRequest> parseRequest(std::string_view json);
    static Result<std::string> buildRequest(const WizardRequest& request);
    static Result<WizardReply> parseReply(WizardCommand command, std::string_view requestId, std::string_view json);
    static std::string buildResult(std::string_view requestId, JsonValue value);
    static std::string buildError(std::string_view requestId, const WizardError& error);
    static WizardError safeError(const Error& error, std::optional<int> status = {},
        std::optional<std::string> field = {}, std::optional<std::string> url = {},
        std::optional<std::string> path = {});
    static JsonValue fieldsValue(const WizardFields& fields);
    static Result<WizardFields> parseFields(const JsonValue& value);
    static JsonValue contextValue(const WizardContext& context);
    static Result<WizardContext> parseContext(const JsonValue& value);
    static JsonValue viewValue(const WizardView& view);
    static Result<WizardView> parseView(const JsonValue& value);
    static JsonValue errorValue(const WizardError& error);
    static Result<WizardError> parseError(const JsonValue& value);
    static JsonValue receiptValue(const WizardReceipt& receipt);
    static JsonValue operationValue(const WizardOperationResult& result);
    static Result<WizardOperationResult> parseOperation(const JsonValue& value);
};
} // namespace phpvpn
