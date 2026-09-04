// Local IPC contract v1, client.md "Verejne lokalni IPC".

#include "core/ipc/ipc_protocol.h"

#include <string>

#include "core/protocol/json.h"
#include "harness.h"

namespace phpvpn {
namespace {

constexpr const char* kRequestId = "3f2504e0-4f89-41d3-9a0c-0305e82c3301";

}  // namespace

PHPVPN_TEST(ipc, parses_every_command_of_the_contract) {
    const char* commands[] = {"get-status", "connect",    "disconnect",     "get-info",
                              "get-config", "set-config", "get-diagnostics"};

    for (const char* name : commands) {
        std::string document = std::string(R"({"ipcVersion":1,"requestId":")") + kRequestId +
                               R"(","command":")" + name + "\"";
        if (std::string(name) == "set-config") {
            document += R"(,"config":"{}")";
        }
        document += "}";

        auto parsed = IpcProtocol::parseRequest(document);
        PHPVPN_REQUIRE_OK(parsed);
        PHPVPN_CHECK_EQ(std::string(ipcCommandName(parsed.value().command)), std::string(name));
        PHPVPN_CHECK_EQ(parsed.value().requestId, std::string(kRequestId));
    }
}

PHPVPN_TEST(ipc, rejects_an_unsupported_version) {
    const std::string document = std::string(R"({"ipcVersion":2,"requestId":")") + kRequestId +
                                 R"(","command":"get-status"})";
    auto parsed = IpcProtocol::parseRequest(document);
    PHPVPN_REQUIRE_ERROR(parsed, ErrorCode::ipc_unsupported_version);
}

PHPVPN_TEST(ipc, rejects_an_unknown_command) {
    const std::string document = std::string(R"({"ipcVersion":1,"requestId":")") + kRequestId +
                                 R"(","command":"shutdown"})";
    auto parsed = IpcProtocol::parseRequest(document);
    PHPVPN_REQUIRE_ERROR(parsed, ErrorCode::ipc_unknown_command);
}

PHPVPN_TEST(ipc, rejects_a_request_identifier_that_is_not_a_uuid_v4) {
    const char* invalid[] = {"", "not-a-uuid", "3f2504e0-4f89-11d3-9a0c-0305e82c3301",
                             "3F2504E0-4F89-41D3-9A0C-0305E82C3301"};
    for (const char* id : invalid) {
        const std::string document = std::string(R"({"ipcVersion":1,"requestId":")") + id +
                                     R"(","command":"get-status"})";
        auto parsed = IpcProtocol::parseRequest(document);
        PHPVPN_REQUIRE_ERROR(parsed, ErrorCode::ipc_malformed);
    }
}

PHPVPN_TEST(ipc, rejects_extra_duplicate_and_missing_members) {
    const std::string extra = std::string(R"({"ipcVersion":1,"requestId":")") + kRequestId +
                              R"(","command":"get-status","extra":1})";
    PHPVPN_REQUIRE_ERROR(IpcProtocol::parseRequest(extra), ErrorCode::ipc_malformed);

    // `config` belongs to `set-config` only.
    const std::string strayConfig = std::string(R"({"ipcVersion":1,"requestId":")") + kRequestId +
                                    R"(","command":"connect","config":"{}"})";
    PHPVPN_REQUIRE_ERROR(IpcProtocol::parseRequest(strayConfig), ErrorCode::ipc_malformed);

    const std::string missingConfig = std::string(R"({"ipcVersion":1,"requestId":")") + kRequestId +
                                      R"(","command":"set-config"})";
    PHPVPN_REQUIRE_ERROR(IpcProtocol::parseRequest(missingConfig), ErrorCode::ipc_malformed);

    const std::string duplicate = std::string(R"({"ipcVersion":1,"ipcVersion":1,"requestId":")") +
                                  kRequestId + R"(","command":"get-status"})";
    PHPVPN_REQUIRE_ERROR(IpcProtocol::parseRequest(duplicate), ErrorCode::ipc_malformed);

    PHPVPN_REQUIRE_ERROR(IpcProtocol::parseRequest("[]"), ErrorCode::ipc_malformed);
    PHPVPN_REQUIRE_ERROR(IpcProtocol::parseRequest("not json"), ErrorCode::ipc_malformed);
}

PHPVPN_TEST(ipc, rejects_a_message_above_the_accepted_size) {
    const std::string oversized(kIpcMaxMessageBytes + 1, 'a');
    PHPVPN_REQUIRE_ERROR(IpcProtocol::parseRequest(oversized), ErrorCode::ipc_message_too_large);
}

PHPVPN_TEST(ipc, round_trips_a_built_request) {
    const std::string document = IpcProtocol::buildRequest(IpcCommand::set_config, kRequestId,
                                                           R"({"masterSetup":{}})");
    auto parsed = IpcProtocol::parseRequest(document);
    PHPVPN_REQUIRE_OK(parsed);
    PHPVPN_CHECK(parsed.value().command == IpcCommand::set_config);
    PHPVPN_CHECK_EQ(parsed.value().config, std::string(R"({"masterSetup":{}})"));

    const std::string simple = IpcProtocol::buildRequest(IpcCommand::get_status, kRequestId);
    auto simpleParsed = IpcProtocol::parseRequest(simple);
    PHPVPN_REQUIRE_OK(simpleParsed);
    PHPVPN_CHECK(simpleParsed.value().command == IpcCommand::get_status);
    PHPVPN_CHECK(simpleParsed.value().config.empty());
}

PHPVPN_TEST(ipc, builds_an_acknowledgement_with_the_envelope) {
    const std::string text = IpcProtocol::buildAcknowledgement(kRequestId);
    auto document = Json::parse(text);
    PHPVPN_REQUIRE_OK(document);
    PHPVPN_CHECK_EQ(document.value().objectValue().size(), std::size_t(3));
    PHPVPN_CHECK_EQ(document.value().find("ipcVersion")->integerValue(), std::int64_t(1));
    PHPVPN_CHECK_EQ(document.value().find("requestId")->stringValue(), std::string(kRequestId));
    PHPVPN_CHECK_EQ(document.value().find("result")->stringValue(), std::string("ok"));
}

PHPVPN_TEST(ipc, builds_a_status_response_with_the_documented_fields) {
    StatusSnapshot snapshot;
    snapshot.state = ConnectionState::connected;
    snapshot.phase = ConnectionPhase::running;
    snapshot.stateChangedAtUtc = "2026-01-01T00:00:00.000Z";
    snapshot.serverReachable = Tristate::yes;
    snapshot.serverVerified = Tristate::yes;
    snapshot.tunnelReady = Tristate::yes;
    snapshot.counters.ipv4PacketsReceived = 12;
    snapshot.counters.ipv4PacketsSent = 34;

    auto document = Json::parse(IpcProtocol::buildStatusResponse(kRequestId, snapshot));
    PHPVPN_REQUIRE_OK(document);

    const JsonValue* status = document.value().find("status");
    PHPVPN_CHECK(status != nullptr);
    PHPVPN_CHECK_EQ(document.value().find("ipcVersion")->integerValue(),
                    std::int64_t(kIpcVersion));
    PHPVPN_CHECK_EQ(status->find("state")->stringValue(), std::string("connected"));
    PHPVPN_CHECK_EQ(status->find("phase")->stringValue(), std::string("running"));
    PHPVPN_CHECK_EQ(status->find("serverVerified")->stringValue(), std::string("yes"));
    PHPVPN_CHECK_EQ(jsonUnsignedOrZero(status->find("ipv4PacketsSent")), std::uint64_t(34));
    // Without a failure the field is present and null, never absent.
    PHPVPN_CHECK(status->find("lastError")->isNull());
}

PHPVPN_TEST(ipc, builds_an_error_response_without_any_internal_detail) {
    const Error error(ErrorCode::response_mac_invalid,
                      "hmac mismatch for nonce 0a1b2c and key material");
    auto document = Json::parse(IpcProtocol::buildError(kRequestId, error));
    PHPVPN_REQUIRE_OK(document);

    const JsonValue* payload = document.value().find("error");
    PHPVPN_CHECK(payload != nullptr);
    PHPVPN_CHECK_EQ(payload->find("code")->stringValue(),
                    std::string(errorCodeName(ErrorCode::response_mac_invalid)));
    // The internal detail must not leak into the description that reaches the UI.
    PHPVPN_CHECK(payload->find("description")->stringValue().find("0a1b2c") == std::string::npos);
    PHPVPN_CHECK(payload->find("recoverable")->isBool());
}

PHPVPN_TEST(ipc, builds_a_diagnostics_response_with_the_queue_and_counter_groups) {
    DiagnosticsSnapshot snapshot;
    snapshot.state = ConnectionState::disconnected;
    snapshot.phase = ConnectionPhase::idle;
    snapshot.configurationValid = true;
    snapshot.configurationWarnings.push_back("the client trusts any TLS certificate");
    snapshot.outbound.itemCount = 3;
    snapshot.outbound.occupiedBytes = 4096;
    snapshot.outbound.limitBytes = 67108864;
    snapshot.inbound.limitBytes = 67108864;
    snapshot.packetsDropped = 7;

    auto document = Json::parse(IpcProtocol::buildDiagnosticsResponse(kRequestId, snapshot));
    PHPVPN_REQUIRE_OK(document);

    const JsonValue* diagnostics = document.value().find("diagnostics");
    PHPVPN_CHECK(diagnostics != nullptr);
    PHPVPN_CHECK(diagnostics->find("configurationValid")->boolValue());
    PHPVPN_CHECK_EQ(diagnostics->find("configurationWarnings")->arrayValue().size(),
                    std::size_t(1));
    PHPVPN_CHECK_EQ(diagnostics->find("outboundQueue")->find("occupiedBytes")->integerValue(),
                    std::int64_t(4096));
    PHPVPN_CHECK_EQ(diagnostics->find("counters")->find("packetsDropped")->integerValue(),
                    std::int64_t(7));
}

}  // namespace phpvpn
