#include "harness.h"
#include "core/ipc/wizard_ipc.h"
#include "core/ipc/ipc_protocol.h"
#include "core/config/config_defaults.h"
#include <algorithm>
#include <limits>

namespace phpvpn {
namespace {
constexpr char id[] = "00000000-0000-4000-8000-000000000001";
constexpr char other[] = "00000000-0000-4000-8000-000000000002";
WizardRequest requestFor(WizardCommand command) {
    WizardRequest request;
    request.command = command;
    request.requestId = request.serviceInstanceId = request.sessionId = request.clientAttemptId = request.operationId = request.commitId = id;
    request.attemptSequence = 1;
    request.fields = analyzeWizardDocument(makeDefaultEditorDocument())->fields;
    request.createDefaultContracts = true;
    return request;
}
using J = JsonValue;
void set(J& value, std::string_view key, J replacement) {
    for (auto& item : value.objectValue()) if (item.first == key) { item.second = std::move(replacement); return; }
    value.objectValue().emplace_back(key, std::move(replacement));
}
J string(std::string text) { return J::makeString(std::move(text)); }
bool accepts(WizardCommand command, const J& value) {
    return WizardIpc::parseReply(command, id, WizardIpc::buildResult(id, value)).ok();
}
J documentReply(std::string availability, std::optional<std::string> document, std::optional<std::string> revision) {
    return J::makeObject({{"configPath", string("C:/ProgramData/PHPVPN/config.json")},
        {"availability", string(availability)}, {"reasonCode", availability == "valid" ? J::makeNull() : string("config_invalid_value")},
        {"document", document ? string(*document) : J::makeNull()},
        {"revisionToken", revision ? string(*revision) : J::makeNull()}});
}
}
PHPVPN_TEST(wizard_ipc, http_headers_all_requests_strict_roundtrip_and_legacy) {
    for (const auto command : {WizardCommand::check_url, WizardCommand::check_secret, WizardCommand::check_access, WizardCommand::finish}) {
        auto request = requestFor(command);
        request.fields.httpHeaders = "Cookie: synthetic=1\r\nX: \"a\\b\"\r\nX-Utf8: \xc5\xbe";
        auto encoded = WizardIpc::buildRequest(request);
        PHPVPN_REQUIRE_OK(encoded);
        PHPVPN_CHECK_EQ(WizardIpc::parseRequest(*encoded)->fields.httpHeaders, request.fields.httpHeaders);
        auto dom = Json::parse(*encoded).value();
        auto* fields = &dom;
        if (command == WizardCommand::check_access || command == WizardCommand::finish)
            for (auto& item : dom.objectValue()) if (item.first == "fields") fields = &item.second;
        for (const auto bad : {J::makeNull(), J::makeInteger(1), string("HOST: synthetic-secret"), string("X: a\nx: b"), string(std::string("X: \xff"))}) {
            set(*fields, "httpHeaders", bad);
            PHPVPN_CHECK(!WizardIpc::parseRequest(Json::serialize(dom)));
        }
        std::erase_if(fields->objectValue(), [](const auto& item) { return item.first == "httpHeaders"; });
        PHPVPN_REQUIRE_ERROR(WizardIpc::parseRequest(Json::serialize(dom)), ErrorCode::wizard_unsupported);
        fields->objectValue().emplace_back("unknown", J::makeBool(true));
        PHPVPN_REQUIRE_ERROR(WizardIpc::parseRequest(Json::serialize(dom)), ErrorCode::ipc_malformed);
        request.fields.httpHeaders = "X: " + std::string(kIpcMaxMessageBytes, '\t');
        PHPVPN_REQUIRE_ERROR(WizardIpc::buildRequest(request), ErrorCode::ipc_message_too_large);
    }
    WizardView view;
    const auto doc = analyzeWizardDocument(makeDefaultEditorDocument()).value();
    view.sessionId = view.serviceInstanceId = id; view.revisionToken = "revision";
    view.availability = ConfigAvailability::valid; view.mode = WizardMode::edit;
    view.fields = doc.fields; view.context = doc.context;
    view.fields->httpHeaders = "Cookie: changed";
    auto encoded = WizardIpc::viewValue(view);
    const auto decoded = WizardIpc::parseView(encoded);
    PHPVPN_REQUIRE_OK(decoded);
    PHPVPN_CHECK_EQ(decoded->fields->httpHeaders, "Cookie: changed");
    PHPVPN_CHECK_EQ(decoded->context->httpHeaders, kDefaultHttpHeaders);
    for (auto& item : encoded.objectValue()) if (item.first == "fields")
        std::erase_if(item.second.objectValue(), [](const auto& f) { return f.first == "httpHeaders"; });
    PHPVPN_REQUIRE_ERROR(WizardIpc::parseView(encoded), ErrorCode::wizard_unsupported);
    set(encoded, "contractCount", string("wrong"));
    PHPVPN_REQUIRE_ERROR(WizardIpc::parseView(encoded), ErrorCode::ipc_malformed);
}
PHPVPN_TEST(wizard_ipc, all_seventeen_request_schemas_are_closed_and_strict) {
    for (int i = 0; i < 17; ++i) {
        const auto command = static_cast<WizardCommand>(i);
        const auto request = requestFor(command);
        auto built = WizardIpc::buildRequest(request);
        PHPVPN_REQUIRE_OK(built);
        auto parsed = WizardIpc::parseRequest(*built);
        PHPVPN_REQUIRE_OK(parsed);
        PHPVPN_CHECK_EQ(parsed->command, command);
        auto dom = Json::parse(*built).value();
        for (const auto& member : dom.objectValue()) {
            auto missing = dom;
            std::erase_if(missing.objectValue(), [&](const auto& item) { return item.first == member.first; });
            PHPVPN_CHECK(!WizardIpc::parseRequest(Json::serialize(missing)));
            auto wrong = dom;
            for (auto& item : wrong.objectValue()) if (item.first == member.first) item.second = JsonValue::makeNull();
            PHPVPN_CHECK(!WizardIpc::parseRequest(Json::serialize(wrong)));
        }
        dom.objectValue().emplace_back("path", JsonValue::makeString("C:/outside.json"));
        PHPVPN_CHECK(!WizardIpc::parseRequest(Json::serialize(dom)));
        auto duplicate = *built;
        duplicate.insert(1, "\"command\":\"wizard-close\",");
        PHPVPN_CHECK(!WizardIpc::parseRequest(duplicate));
    }
}
namespace {
WizardOperationResult diagnosticOperation() {
    WizardOperationResult op;
    op.operationId = id; op.clientAttemptId = other; op.attemptSequence = 1;
    op.state = WizardOperationState::failed;
    op.error = WizardIpc::safeError(Error(ErrorCode::response_invalid), 200, "serverUrl");
    HttpDiagnostic d;
    d.action = HttpAction::health; d.responseComplete = true;
    BoundedHttpCapture request, response;
    request.append("POST http://localhost/?action=health HTTP/?\r\n\r\n{}");
    response.append("HTTP/1.1 200 OK\r\n\r\n<script>fixture</script>");
    d.request = request.finish(); d.response = response.finish(); op.httpDiagnostic = std::move(d);
    return op;
}
}
PHPVPN_TEST(wizard_ipc, diagnostic_schema_closed_roundtrip_and_illegal_placement) {
    const auto op = diagnosticOperation();
    const auto valid = WizardIpc::operationValue(op);
    PHPVPN_CHECK(accepts(WizardCommand::get_operation, valid));
    auto roundtrip = WizardIpc::parseOperation(valid);
    PHPVPN_REQUIRE_OK(roundtrip);
    PHPVPN_CHECK_EQ(roundtrip->httpDiagnostic->request.prefix, op.httpDiagnostic->request.prefix);
    for (const auto* level : {"httpDiagnostic", "request", "response"}) {
        auto base = *valid.find("httpDiagnostic");
        const auto object = std::string_view(level) == "httpDiagnostic" ? base : *base.find(level);
        for (const auto& [key, ignored] : object.objectValue()) {
            (void)ignored;
            auto changed = object;
            std::erase_if(changed.objectValue(), [&](const auto& item) { return item.first == key; });
            auto d = base; if (std::string_view(level) == "httpDiagnostic") d = changed; else set(d, level, changed);
            auto value = valid; set(value, "httpDiagnostic", d);
            PHPVPN_CHECK(!accepts(WizardCommand::get_operation, value));
        }
        auto changed = object; changed.objectValue().emplace_back("extra", J::makeNull());
        auto d = base; if (std::string_view(level) == "httpDiagnostic") d = changed; else set(d, level, changed);
        auto value = valid; set(value, "httpDiagnostic", d);
        PHPVPN_CHECK(!accepts(WizardCommand::get_operation, value));
    }
    for (const auto& [key, replacement] : std::vector<std::pair<std::string, J>>{
        {"httpDiagnostic", J::makeNull()}, {"kind", string("access")}, {"kind", string("finish")},
        {"operationState", string("succeeded")}, {"operationState", string("running")}, {"operationState", string("cancelled")},
        {"operationState", string("interrupted")}, {"error", J::makeNull()}, {"payload", J::makeObject({})}}) {
        auto value = valid; set(value, key, replacement); PHPVPN_CHECK(!accepts(WizardCommand::get_operation, value));
    }
    for (const auto& [key, replacement] : std::vector<std::pair<std::string, J>>{
        {"format", string("unknown")}, {"action", string("data")}, {"action", string("info")},
        {"responseComplete", J::makeInteger(1)}, {"requestHeadersSource", string("available")}, {"responseHeadersSource", string("application")}}) {
        auto value = valid, d = *valid.find("httpDiagnostic"); set(d, key, replacement); set(value, "httpDiagnostic", d);
        PHPVPN_CHECK(!accepts(WizardCommand::get_operation, value));
    }
    for (const auto& [key, replacement] : std::vector<std::pair<std::string, J>>{
        {"serverUrl", string("http://localhost/")}, {"configPath", string("C:/fixture.json")},
        {"code", string(std::string(129, 'a'))}, {"description", string(std::string(4097, 'a'))},
        {"suggestedAction", string(std::string(4097, 'a'))}, {"field", string("serverSecret")}}) {
        auto value = valid, error = *valid.find("error"); set(error, key, replacement); set(value, "error", error);
        PHPVPN_CHECK(!accepts(WizardCommand::get_operation, value));
    }
    auto secret = op; secret.kind = WizardOperationKind::secret; secret.error->field = "serverSecret"; secret.httpDiagnostic->action = HttpAction::info;
    PHPVPN_CHECK(accepts(WizardCommand::get_operation, WizardIpc::operationValue(secret)));
    auto old = op; old.httpDiagnostic.reset(); PHPVPN_CHECK_EQ(WizardIpc::operationValue(old).objectValue().size(), 8u);
    PHPVPN_CHECK(accepts(WizardCommand::get_operation, WizardIpc::operationValue(old)));
}
PHPVPN_TEST(wizard_ipc, diagnostic_octet_lengths_numbers_and_separator_are_exact) {
    auto op = diagnosticOperation();
    op.httpDiagnostic->response = {65537, std::string(64512, 'a'), std::string(1024, 'b')};
    auto valid = WizardIpc::operationValue(op);
    PHPVPN_CHECK(accepts(WizardCommand::get_operation, valid));
    const auto real = Json::parse("1.5").value();
    for (const auto& [key, replacement] : std::vector<std::pair<std::string, J>>{
        {"originalSizeBytes", J::makeInteger(-1)}, {"originalSizeBytes", J::makeInteger(0)}, {"originalSizeBytes", real},
        {"originalSizeBytes", string("65537")}, {"originalSizeBytes", J::makeInteger(65536)},
        {"prefix", string(std::string(64511, 'a'))}, {"prefix", string(std::string(64513, 'a'))},
        {"prefix", string(std::string(64511, 'a') + "\xc4\x80")}, {"suffix", string(std::string(1023, 'b'))},
        {"separator", string("\n...\n...\n...\n...\n")}, {"separator", string("")}}) {
        auto value = valid, d = *valid.find("httpDiagnostic"), r = *d.find("response");
        set(r, key, replacement); set(d, "response", r); set(value, "httpDiagnostic", d);
        PHPVPN_CHECK(!accepts(WizardCommand::get_operation, value));
    }
    auto json = WizardIpc::buildResult(id, valid);
    const auto pos = json.find("65537"); json.replace(pos, 5, "18446744073709551616");
    PHPVPN_CHECK(!WizardIpc::parseReply(WizardCommand::get_operation, id, json));
    json = WizardIpc::buildResult(id, valid);
    const auto format = json.find("\"format\":"); json.insert(format, "\"format\":\"http-octets-v1\",");
    PHPVPN_CHECK(!WizardIpc::parseReply(WizardCommand::get_operation, id, json));
}
PHPVPN_TEST(wizard_ipc, diagnostic_worst_case_envelope_has_proven_headroom) {
    auto op = diagnosticOperation();
    const auto max = (std::numeric_limits<std::uint64_t>::max)();
    op.connectionEpoch = max; op.attemptSequence = max;
    op.httpDiagnostic->request = {max, std::string(64512, '\1'), std::string(1024, '\1')};
    op.httpDiagnostic->response = op.httpDiagnostic->request;
    op.error->code.assign(128, '\1'); op.error->description.assign(4096, '\1'); op.error->suggestedAction.assign(4096, '\1');
    auto encoded = WizardIpc::buildResult(id, WizardIpc::operationValue(op));
    PHPVPN_CHECK(encoded.size() <= 852232);
    PHPVPN_CHECK(encoded.size() < kIpcMaxMessageBytes);
    auto reply = WizardIpc::parseReply(WizardCommand::get_operation, id, encoded);
    PHPVPN_REQUIRE_OK(reply);
    PHPVPN_CHECK(!reply->error);
    auto parsed = WizardIpc::parseOperation(reply->result); PHPVPN_REQUIRE_OK(parsed);
    PHPVPN_CHECK_EQ(parsed->httpDiagnostic->request.originalSizeBytes, max);
    PHPVPN_CHECK_EQ(parsed->httpDiagnostic->request.prefix, op.httpDiagnostic->request.prefix);
}
PHPVPN_TEST(wizard_ipc, diagnostic_safe_error_catalog_satisfies_envelope_bounds) {
    for (int value = static_cast<int>(ErrorCode::none); value <= static_cast<int>(ErrorCode::state_conflict); ++value) {
        auto operation = diagnosticOperation();
        operation.error = WizardIpc::safeError(Error(static_cast<ErrorCode>(value)), 200, "serverUrl");
        PHPVPN_CHECK(operation.error->code.size() <= 128);
        PHPVPN_CHECK(operation.error->description.size() <= 4096);
        PHPVPN_CHECK(operation.error->suggestedAction.size() <= 4096);
        PHPVPN_CHECK(!operation.error->serverUrl && !operation.error->configPath);
        PHPVPN_CHECK(accepts(WizardCommand::get_operation, WizardIpc::operationValue(operation)));
    }
}
PHPVPN_TEST(wizard_ipc, sequence_full_unsigned_range_and_exact_secret_round_trip) {
    auto request = requestFor(WizardCommand::check_access);
    request.attemptSequence = (std::numeric_limits<std::uint64_t>::max)();
    request.fields.serverSecret = std::string("before\0after\\n\n", 15);
    auto built = WizardIpc::buildRequest(request);
    PHPVPN_REQUIRE_OK(built);
    auto parsed = WizardIpc::parseRequest(*built);
    PHPVPN_REQUIRE_OK(parsed);
    PHPVPN_CHECK_EQ(parsed->attemptSequence, request.attemptSequence);
    PHPVPN_CHECK_EQ(parsed->fields.serverSecret, request.fields.serverSecret);
    request.attemptSequence = 0;
    PHPVPN_CHECK(!WizardIpc::buildRequest(request));
    request.attemptSequence = 1;
    built = WizardIpc::buildRequest(request);
    auto dom = Json::parse(*built).value();
    for (const auto bad : {JsonValue::makeInteger(-1), JsonValue::makeReal(1.5), JsonValue::makeString("1")}) {
        for (auto& field : dom.objectValue()) if (field.first == "attemptSequence") field.second = bad;
        PHPVPN_CHECK(!WizardIpc::parseRequest(Json::serialize(dom)));
    }
}
PHPVPN_TEST(wizard_ipc, views_context_and_operation_payloads_are_validated) {
    const auto draft = makeNewWizardDocument();
    WizardView view;
    view.sessionId = view.serviceInstanceId = id;
    view.fields = draft.fields; view.context = draft.context;
    auto encoded = WizardIpc::viewValue(view);
    PHPVPN_REQUIRE_OK(WizardIpc::parseView(encoded));
    encoded.objectValue().emplace_back("secretHint", JsonValue::makeString("unexpected"));
    PHPVPN_CHECK(!WizardIpc::parseView(encoded));
    for (int i = 0; i < 4; ++i) {
        WizardOperationResult operation;
        operation.operationId = id;
        operation.kind = static_cast<WizardOperationKind>(i);
        operation.state = WizardOperationState::succeeded;
        if (i == 3) operation.receipt = WizardReceipt{id, "opaque-revision"};
        else { operation.clientAttemptId = id; operation.attemptSequence = 3; }
        const auto value = WizardIpc::operationValue(operation);
        auto parsed = WizardIpc::parseOperation(value);
        PHPVPN_REQUIRE_OK(parsed);
        PHPVPN_CHECK_EQ(parsed->kind, operation.kind);
        const auto response = WizardIpc::buildResult(id, value);
        PHPVPN_REQUIRE_OK(WizardIpc::parseReply(WizardCommand::get_operation, id, response));
        PHPVPN_CHECK(!WizardIpc::parseReply(WizardCommand::get_operation, other, response));
        auto changed = value;
        for (auto& item : changed.objectValue()) if (item.first == "payload") item.second = JsonValue::makeNull();
        PHPVPN_CHECK(!WizardIpc::parseOperation(changed));
    }
}
PHPVPN_TEST(wizard_ipc, errors_omit_raw_details_and_enforce_allowlisted_fields) {
    const auto error = WizardIpc::safeError(Error(ErrorCode::server_unauthorized, "PRIVATE-KEY-AND-RAW-BODY"), 401,
        "accessKey", "http://127.0.0.1/phpvpn.php", "C:/ProgramData/PHPVPN/config.json");
    const auto text = WizardIpc::buildError(id, error);
    PHPVPN_CHECK(text.find("PRIVATE-KEY-AND-RAW-BODY") == std::string::npos);
    auto reply = WizardIpc::parseReply(WizardCommand::check_access, id, text);
    PHPVPN_REQUIRE_OK(reply);
    PHPVPN_CHECK(reply->error.has_value());
    auto value = WizardIpc::errorValue(error);
    value.objectValue().emplace_back("ciphertext", JsonValue::makeString("hidden"));
    PHPVPN_CHECK(!WizardIpc::parseError(value));
}

PHPVPN_TEST(wizard_ipc, configuration_errors_have_safe_action_and_authoritative_path) {
    for (const auto code : {ErrorCode::config_directory_security_invalid, ErrorCode::config_write_failed,
        ErrorCode::config_unreadable, ErrorCode::config_read_failed, ErrorCode::config_too_large,
        ErrorCode::config_encoding_error, ErrorCode::journal_corrupt, ErrorCode::journal_ambiguous}) {
        const std::string path = "C:/ProgramData/PHPVPN/config.json";
        const auto error = WizardIpc::safeError(Error(code, "PRIVATE-KEY-AND-RAW-JOURNAL"), {}, {}, {}, path);
        const auto encoded = WizardIpc::buildError(id, error);
        PHPVPN_CHECK(encoded.find("PRIVATE-KEY-AND-RAW-JOURNAL") == std::string::npos);
        PHPVPN_CHECK(encoded.find("Next") == std::string::npos);
        auto parsed = WizardIpc::parseReply(WizardCommand::create_default_config, id, encoded);
        PHPVPN_REQUIRE_OK(parsed);
        PHPVPN_CHECK(parsed->error.has_value());
        PHPVPN_CHECK_EQ(parsed->error->code, std::string(errorCodeName(code)));
        PHPVPN_CHECK_EQ(*parsed->error->configPath, path);
        if (code == ErrorCode::config_directory_security_invalid) {
            PHPVPN_CHECK(error.description.find("folder permissions") != std::string::npos);
            PHPVPN_CHECK(error.suggestedAction.find("administrator to restart") != std::string::npos);
            PHPVPN_CHECK(error.suggestedAction.find("Edit Config") != std::string::npos);
            PHPVPN_CHECK(error.suggestedAction.find("disconnects the VPN") != std::string::npos);
        } else {
            PHPVPN_CHECK(error.suggestedAction.find("restart") == std::string::npos);
        }
    }
    PHPVPN_CHECK_EQ(WizardIpc::safeError(Error(ErrorCode::transport_connect_failed)).suggestedAction,
        std::string("Correct the reported issue, then choose Verify & continue for a new check."));
    PHPVPN_CHECK(WizardIpc::safeError(Error(ErrorCode::config_invalid_url), {}, "serverUrl").suggestedAction.find("Verify & continue") != std::string::npos);
    PHPVPN_CHECK_EQ(WizardIpc::safeError(Error(ErrorCode::wizard_connection_interrupted)).suggestedAction,
        std::string("Choose Disconnect & continue to disconnect the VPN for everyone, then repeat the checks."));
}
PHPVPN_TEST(wizard_ipc, maximum_document_escaping_and_message_limit) {
    const std::string control(kMaxConfigFileBytes, '\x01');
    const auto response = WizardIpc::buildResult(id, JsonValue::makeObject({{"document", JsonValue::makeString(control)}}));
    PHPVPN_CHECK(response.size() < kIpcMaxMessageBytes);
    PHPVPN_CHECK(!WizardIpc::parseReply(WizardCommand::get_default_config, id, response));
    const auto actualInvalidText = documentReply("invalid", control, "opaque-revision");
    PHPVPN_CHECK(accepts(WizardCommand::get_config_document, actualInvalidText));
    auto excessive = actualInvalidText;
    set(excessive, "document", string(std::string(kMaxConfigFileBytes + 1, 'x')));
    PHPVPN_CHECK(!accepts(WizardCommand::get_config_document, excessive));
    auto request = requestFor(WizardCommand::check_secret);
    request.fields.serverSecret = std::string(kIpcMaxMessageBytes, '\x01');
    PHPVPN_REQUIRE_ERROR(WizardIpc::buildRequest(request), ErrorCode::ipc_message_too_large);
}

PHPVPN_TEST(wizard_ipc, only_exact_correlated_legacy_unknown_command_maps_to_unsupported) {
    const auto legacy = IpcProtocol::buildError(id, Error(ErrorCode::ipc_unknown_command));
    auto reply = WizardIpc::parseReply(WizardCommand::open, id, legacy);
    PHPVPN_REQUIRE_OK(reply);
    PHPVPN_CHECK(reply->error.has_value());
    PHPVPN_CHECK_EQ(reply->error->code, std::string("wizard_unsupported"));
    PHPVPN_CHECK(reply->error->suggestedAction.find("Update") != std::string::npos);
    PHPVPN_CHECK(!WizardIpc::parseReply(WizardCommand::open, other, legacy));
    auto root = Json::parse(legacy).value();
    auto error = *root.find("error");
    set(error, "description", string("UNTRUSTED RAW DESCRIPTION"));
    set(root, "error", error);
    auto sanitized = WizardIpc::parseReply(WizardCommand::open, id, Json::serialize(root));
    PHPVPN_REQUIRE_OK(sanitized);
    PHPVPN_CHECK(sanitized->error->description.find("UNTRUSTED") == std::string::npos);
    PHPVPN_CHECK(!WizardIpc::parseError(error)); // General parser remains strict.
    for (const auto bad : {string("true"), J::makeBool(true), J::makeNull()}) {
        auto changed = error;
        set(changed, "recoverable", bad);
        set(root, "error", changed);
        PHPVPN_CHECK(!WizardIpc::parseReply(WizardCommand::open, id, Json::serialize(root)));
    }
    auto extra = error;
    set(extra, "serverUrl", string("http://127.0.0.1/phpvpn.php"));
    set(root, "error", extra);
    PHPVPN_CHECK(!WizardIpc::parseReply(WizardCommand::open, id, Json::serialize(root)));
    for (const auto code : {ErrorCode::internal, ErrorCode::ipc_malformed, ErrorCode::ipc_unsupported_version}) {
        PHPVPN_CHECK(!WizardIpc::parseReply(WizardCommand::open, id, IpcProtocol::buildError(id, Error(code))));
    }
    set(root, "error", error);
    set(root, "ipcVersion", J::makeInteger(2));
    PHPVPN_CHECK(!WizardIpc::parseReply(WizardCommand::open, id, Json::serialize(root)));
    set(root, "ipcVersion", J::makeInteger(1));
    set(root, "result", J::makeNull());
    PHPVPN_CHECK(!WizardIpc::parseReply(WizardCommand::open, id, Json::serialize(root)));
}

PHPVPN_TEST(wizard_ipc, configuration_documents_enforce_availability_nulls_and_actual_content) {
    const auto valid = documentReply("valid", makeDefaultEditorDocument(), "opaque-revision");
    PHPVPN_CHECK(accepts(WizardCommand::get_config_document, valid));
    auto repairDom = Json::parse(makeDefaultEditorDocument()).value();
    for (auto& top : repairDom.objectValue()) if (top.first == "masterSetup")
        std::erase_if(top.second.objectValue(), [](const auto& item) { return item.first == "accessKey"; });
    const auto repair = documentReply("repairable", Json::serialize(repairDom), "opaque-revision");
    PHPVPN_CHECK(accepts(WizardCommand::get_config_document, repair));
    // A readable but syntactically invalid UTF-8 document remains editable.
    const auto invalid = documentReply("invalid", " {\r\nbroken UTF-8 text: \xc4\x8d\r\n", "opaque-revision");
    PHPVPN_CHECK(accepts(WizardCommand::get_config_document, invalid));
    const auto missing = documentReply("missing", {}, {});
    PHPVPN_CHECK(accepts(WizardCommand::get_config_document, missing));
    for (const auto availability : {"missing", "unreadable", "encoding_error", "too_large", "repairable", "invalid"}) {
        auto changed = valid;
        set(changed, "availability", string(availability));
        set(changed, "reasonCode", string("config_invalid_value"));
        PHPVPN_CHECK(!accepts(WizardCommand::get_config_document, changed));
    }
    for (const auto bad : {J::makeNull(), string("")}) {
        auto changed = valid;
        set(changed, "revisionToken", bad);
        PHPVPN_CHECK(!accepts(WizardCommand::get_config_document, changed));
    }
    auto changed = missing;
    set(changed, "document", string(""));
    PHPVPN_CHECK(!accepts(WizardCommand::get_config_document, changed));
    changed = valid;
    set(changed, "reasonCode", string("config_missing"));
    PHPVPN_CHECK(!accepts(WizardCommand::get_config_document, changed));
    changed = invalid;
    set(changed, "reasonCode", J::makeNull());
    PHPVPN_CHECK(!accepts(WizardCommand::get_config_document, changed));
    set(changed, "reasonCode", string("config_invalid_json"));
    set(changed, "document", string(std::string("text\0tail", 9)));
    PHPVPN_CHECK(accepts(WizardCommand::get_config_document, changed));
    set(changed, "document", string(std::string("\xef\xbb\xbf") + makeDefaultEditorDocument()));
    PHPVPN_CHECK(accepts(WizardCommand::get_config_document, changed));
    set(changed, "availability", string("valid"));
    set(changed, "reasonCode", J::makeNull());
    PHPVPN_CHECK(!accepts(WizardCommand::get_config_document, changed));
    for (const auto& member : valid.objectValue()) {
        auto absent = valid;
        std::erase_if(absent.objectValue(), [&](const auto& item) { return item.first == member.first; });
        PHPVPN_CHECK(!accepts(WizardCommand::get_config_document, absent));
    }
}

PHPVPN_TEST(wizard_ipc, configuration_paths_state_and_create_results_are_coherent) {
    auto state = documentReply("valid", makeDefaultEditorDocument(), "revision");
    std::erase_if(state.objectValue(), [](const auto& item) { return item.first == "document"; });
    set(state, "serviceInstanceId", string(id));
    set(state, "connectionEpoch", J::makeUnsignedInteger((std::numeric_limits<std::uint64_t>::max)()));
    set(state, "vpnState", string("disconnected"));
    set(state, "vpnPhase", string("idle"));
    PHPVPN_CHECK(accepts(WizardCommand::get_config_state, state));
    for (const auto path : {"", "config.json", "C:config.json", "\\config.json", "%ProgramData%/PHPVPN/config.json", "\\\\server", "\\\\.\\pipe\\anything"}) {
        auto changed = state;
        set(changed, "configPath", string(path));
        PHPVPN_CHECK(!accepts(WizardCommand::get_config_state, changed));
    }
    for (const auto path : {"D:\\Apps\\PHPVPN\\config.json", "\\\\server\\share\\config.json", "\\\\?\\C:\\ProgramData\\PHPVPN\\config.json"}) {
        auto changed = state;
        set(changed, "configPath", string(path));
        PHPVPN_CHECK(accepts(WizardCommand::get_config_state, changed));
    }
    for (const auto availability : {"missing", "unreadable", "encoding_error", "too_large"}) {
        auto changed = state;
        set(changed, "availability", string(availability));
        set(changed, "reasonCode", string("config_unreadable"));
        PHPVPN_CHECK(!accepts(WizardCommand::get_config_state, changed));
        set(changed, "revisionToken", J::makeNull());
        PHPVPN_CHECK(accepts(WizardCommand::get_config_state, changed));
    }
    auto created = documentReply("valid", makeDefaultEditorDocument(), "revision");
    std::erase_if(created.objectValue(), [](const auto& item) { return item.first == "reasonCode"; });
    set(created, "created", J::makeBool(true));
    PHPVPN_CHECK(accepts(WizardCommand::create_default_config, created));
    set(created, "availability", string("invalid"));
    set(created, "document", string("broken-json"));
    PHPVPN_CHECK(!accepts(WizardCommand::create_default_config, created));
    set(created, "created", J::makeBool(false)); // External create race opens actual invalid text.
    PHPVPN_CHECK(accepts(WizardCommand::create_default_config, created));
    set(created, "revisionToken", string(""));
    PHPVPN_CHECK(!accepts(WizardCommand::create_default_config, created));
}

PHPVPN_TEST(wizard_ipc, context_rejects_impossible_budgets_and_overflow) {
    const auto context = makeNewWizardDocument().context;
    const auto encoded = WizardIpc::contextValue(context);
    PHPVPN_REQUIRE_OK(WizardIpc::parseContext(encoded));
    const auto defaults = WizardIpc::parseContext(encoded);
    PHPVPN_CHECK(defaults->runtime.maxOutboundQueueBytes == 134217728);
    PHPVPN_CHECK(defaults->runtime.maxInboundQueueBytes == 134217728);
    PHPVPN_CHECK(defaults->runtime.minOutboundPerPeerQueueBytes == 262144);
    PHPVPN_CHECK(defaults->runtime.minInboundPerPeerQueueBytes == 262144);
    PHPVPN_CHECK(defaults->runtime.maxQueuedPacketAgeMs == 5000);
    const auto reservation = static_cast<std::int64_t>(context.vpnCidr.clientAddressCount() - 1) * context.runtime.minOutboundPerPeerQueueBytes;
    for (const auto key : {"maxInboundQueueBytes", "maxOutboundQueueBytes"}) {
        auto changed = encoded;
        set(changed, key, J::makeInteger(reservation - 1));
        PHPVPN_CHECK(!WizardIpc::parseContext(changed));
        set(changed, key, J::makeInteger(reservation));
        PHPVPN_REQUIRE_OK(WizardIpc::parseContext(changed));
    }
    auto changed = encoded;
    set(changed, "minOutboundPerPeerQueueBytes", J::makeInteger((std::numeric_limits<std::int64_t>::max)()));
    PHPVPN_CHECK(!WizardIpc::parseContext(changed));
    changed = encoded;
    set(changed, "minInboundPerPeerQueueBytes", J::makeInteger((std::numeric_limits<std::int64_t>::max)()));
    PHPVPN_CHECK(!WizardIpc::parseContext(changed));
    changed = encoded;
    set(changed, "minOutboundPerPeerQueueBytes", J::makeInteger(16));
    set(changed, "minInboundPerPeerQueueBytes", J::makeInteger(17));
    set(changed, "maxQueuedPacketAgeMs", J::makeInteger(1234));
    const auto asymmetric = WizardIpc::parseContext(changed);
    PHPVPN_REQUIRE_OK(asymmetric);
    PHPVPN_CHECK(asymmetric->runtime.minOutboundPerPeerQueueBytes == 16);
    PHPVPN_CHECK(asymmetric->runtime.minInboundPerPeerQueueBytes == 17);
    PHPVPN_CHECK(asymmetric->runtime.maxQueuedPacketAgeMs == 1234);
    changed = encoded;
    set(changed, "maxNdjsonLineBytes", J::makeInteger(context.runtime.maxResponseBodyBytes + 1));
    PHPVPN_CHECK(!WizardIpc::parseContext(changed));
    for (const auto& member : encoded.objectValue()) {
        if (member.first == "vpnCidr" || member.first == "trustAnyTlsCertificate") continue;
        for (const auto bad : {J::makeInteger(0), J::makeInteger(-1), J::makeReal(1.5),
            J::makeUnsignedInteger((std::numeric_limits<std::uint64_t>::max)())}) {
            changed = encoded;
            set(changed, member.first, bad);
            PHPVPN_CHECK(!WizardIpc::parseContext(changed));
        }
    }
    changed = encoded;
    set(changed, "responseTimeoutSeconds", J::makeInteger(kMaxHttpTimeoutSeconds));
    PHPVPN_REQUIRE_OK(WizardIpc::parseContext(changed));
    set(changed, "responseTimeoutSeconds", J::makeInteger(kMaxHttpTimeoutSeconds + 1));
    PHPVPN_CHECK(!WizardIpc::parseContext(changed));
}

PHPVPN_TEST(wizard_ipc, full_default_template_and_view_reset_semantics) {
    PHPVPN_CHECK(accepts(WizardCommand::get_default_config, J::makeObject({{"document", string(makeDefaultEditorDocument())}})));
    for (const auto content : {"", "{}", "{\"masterSetup\":{}}", "syntactically broken"}) {
        PHPVPN_CHECK(!accepts(WizardCommand::get_default_config, J::makeObject({{"document", string(content)}})));
    }
    const auto draft = makeNewWizardDocument();
    WizardView view;
    view.sessionId = view.serviceInstanceId = id;
    view.mode = WizardMode::reset;
    view.availability = ConfigAvailability::unreadable;
    PHPVPN_CHECK(accepts(WizardCommand::open, WizardIpc::viewValue(view)));
    PHPVPN_CHECK(!accepts(WizardCommand::reset, WizardIpc::viewValue(view)));
    view.fields = draft.fields; view.context = draft.context;
    PHPVPN_CHECK(accepts(WizardCommand::reset, WizardIpc::viewValue(view)));
    PHPVPN_CHECK(!accepts(WizardCommand::open, WizardIpc::viewValue(view)));
    view.revisionToken = "stale-but-readable";
    PHPVPN_CHECK(!WizardIpc::parseView(WizardIpc::viewValue(view)));
    view.availability = ConfigAvailability::valid;
    PHPVPN_CHECK(accepts(WizardCommand::reset, WizardIpc::viewValue(view)));
    view.mode = WizardMode::repair;
    PHPVPN_CHECK(!WizardIpc::parseView(WizardIpc::viewValue(view)));
    view.mode = WizardMode::edit;
    view.revisionToken.clear();
    PHPVPN_CHECK(!WizardIpc::parseView(WizardIpc::viewValue(view)));
    for (const auto state : {"pending", "running"}) {
        PHPVPN_CHECK(!accepts(WizardCommand::cancel_operation,
            J::makeObject({{"operationId", string(id)}, {"operationState", string(state)}})));
    }
    PHPVPN_CHECK(accepts(WizardCommand::cancel_operation,
        J::makeObject({{"operationId", string(id)}, {"operationState", string("succeeded")}})));
}
} // namespace phpvpn
