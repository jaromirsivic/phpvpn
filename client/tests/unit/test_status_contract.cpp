#include "harness.h"

#include <algorithm>
#include <array>
#include <limits>

#include "core/ipc/ipc_protocol.h"
#include "core/protocol/json.h"
#include "../../apps/tray/status_model.h"
#include "../../apps/tray/progress_model.h"
#include "platform/iface/transport.h"

namespace {
using namespace phpvpn;
constexpr char kRequestId[] = "3f2a7c1e-9b44-4b0e-8d2f-5c6a1e7b9d03";

void reverseObjects(JsonValue& value) {
    if (!value.isObject()) return;
    auto& members = value.objectValue();
    std::reverse(members.begin(), members.end());
    for (auto& member : members) reverseObjects(member.second);
}

StatusSnapshot fixture(std::uint64_t value) {
    StatusSnapshot snapshot;
    snapshot.clientVersion = "1.0.0";
    snapshot.stateChangedAtUtc = "2026-09-04T18:52:31.412Z";
    snapshot.connectionStartedAtUtc = "2026-09-04T18:52:30.000Z";
    snapshot.state = ConnectionState::recovering;
    snapshot.phase = ConnectionPhase::access_key_check;
    snapshot.serverUrlCheck = CheckpointResult::success;
    snapshot.serverSecretCheck = CheckpointResult::success;
    snapshot.accessKeyCheck = CheckpointResult::failed;
    snapshot.lastError = SafeError::fromConnection(Error(ErrorCode::transport_timeout),
                                                    ConnectionPhase::access_key_check);
    snapshot.counters = {value, value, 0, value, value, value, value, value, value};
    return snapshot;
}
}

PHPVPN_TEST(StatusContract, ServiceToTrayFullUnsignedRangeAndObjectOrder) {
    const auto signedMax = static_cast<std::uint64_t>((std::numeric_limits<std::int64_t>::max)());
    const std::array values{signedMax - 1, signedMax, signedMax + 1,
                            (std::numeric_limits<std::uint64_t>::max)()};
    for (const auto value : values) {
        const auto snapshot = fixture(value);
        auto document = Json::parse(IpcProtocol::buildStatusResponse(kRequestId, snapshot));
        PHPVPN_REQUIRE_OK(document);
        const auto* status = document.value().find("status");
        PHPVPN_CHECK(status != nullptr);
        if (!status) return;
        PHPVPN_CHECK_EQ(status->objectValue().size(), 25U);
        PHPVPN_CHECK(status->find("packetsSent") == nullptr);
        PHPVPN_CHECK(status->find("packetsReceived") == nullptr);
        for (int order = 0; order != 2; ++order) {
            const auto parsed = tray::parseStatusResponse(Json::serialize(document.value()));
            PHPVPN_CHECK(parsed.valid);
            PHPVPN_CHECK_EQ(parsed.state, "recovering");
            PHPVPN_CHECK(parsed.connectionStartedAtUtcMs.has_value());
            PHPVPN_CHECK_EQ(parsed.serverUrl, tray::CheckpointResult::success);
            PHPVPN_CHECK_EQ(parsed.serverSecret, tray::CheckpointResult::success);
            PHPVPN_CHECK_EQ(parsed.accessKey, tray::CheckpointResult::failed);
            PHPVPN_CHECK_EQ(parsed.errorCode, "communication_timeout");
            PHPVPN_CHECK_EQ(parsed.errorDescription, snapshot.lastError->description);
            PHPVPN_CHECK(parsed.errorRecoverable);
            for (const auto& field : tray::kCounterFields) {
                const auto expected = std::string_view(field.key) == "dataPollsWithPayload" ? 0 : value;
                PHPVPN_CHECK_EQ(parsed.counters.*(field.member), expected);
            }
            reverseObjects(document.value());
        }
    }
}

PHPVPN_TEST(StatusContract, InvalidReplyDoesNotResetServiceCountersInTray) {
    const auto snapshot = fixture(123);
    auto current = tray::parseStatusResponse(IpcProtocol::buildStatusResponse(kRequestId, snapshot));
    PHPVPN_CHECK(current.valid);
    auto document = Json::parse(IpcProtocol::buildStatusResponse(kRequestId, snapshot));
    PHPVPN_REQUIRE_OK(document);
    for (auto& outer : document.value().objectValue()) {
        if (outer.first != "status") continue;
        for (auto& member : outer.second.objectValue()) {
            if (member.first == "ipv4BytesReceived") member.second = JsonValue::makeInteger(-1);
        }
    }
    const auto invalid = tray::parseStatusResponse(Json::serialize(document.value()));
    PHPVPN_CHECK(!invalid.valid);
    PHPVPN_CHECK(!tray::applyStatusSnapshot(current, invalid));
    PHPVPN_CHECK_EQ(current.counters.ipv4BytesReceived, 123U);
    PHPVPN_CHECK_EQ(current.accessKey, tray::CheckpointResult::failed);
}

PHPVPN_TEST(StatusContract, ConnectHttpDiagnosticStrictSchemaBoundsAndCorrelation) {
    StatusSnapshot s;
    s.clientVersion = "test"; s.stateChangedAtUtc = "2026-09-13T08:00:00.000Z";
    s.connectionEpoch = 7; s.state = ConnectionState::error;
    s.connectAttemptId = kRequestId;
    s.serverUrlCheck = CheckpointResult::failed;
    s.lastError = SafeError{"endpoint_unavailable_or_invalid", "Safe public error", false};
    HttpDiagnostic d; d.action = HttpAction::health; d.responseComplete = true;
    for (std::size_t size : {65535u, 65536u, 65537u, 200000u}) {
        BoundedHttpCapture capture; capture.append(std::string(size, '\1'));
        d.request = d.response = capture.finish(); s.httpDiagnostic = d;
        const auto wire = IpcProtocol::buildStatusResponse(kRequestId, s);
        PHPVPN_CHECK(wire.size() < kIpcMaxMessageBytes);
        const auto parsed = tray::parseStatusResponse(wire);
        PHPVPN_CHECK(parsed.valid && parsed.httpDiagnostic);
        if (parsed.httpDiagnostic) {
            PHPVPN_CHECK_EQ(parsed.connectionEpoch, 7u);
            PHPVPN_CHECK_EQ(parsed.httpDiagnostic->response.originalSizeBytes, size);
            PHPVPN_CHECK_EQ(parsed.httpDiagnostic->response.prefix, d.response.prefix);
            PHPVPN_CHECK_EQ(parsed.httpDiagnostic->response.suffix, d.response.suffix);
        }
    }
    auto valid = Json::parse(IpcProtocol::buildStatusResponse(kRequestId, s)).value();
    auto set = [](JsonValue& object, std::string_view key, JsonValue value) {
        for (auto& entry : object.objectValue()) if (entry.first == key) { entry.second = std::move(value); return; }
        object.objectValue().emplace_back(key, std::move(value));
    };
    auto statusOf = [](JsonValue& doc) -> JsonValue& { return doc.objectValue().back().second; };
    for (const auto& [key, value] : std::vector<std::pair<std::string, JsonValue>>{
        {"connectionEpoch", JsonValue::makeInteger(0)}, {"connectionEpoch", JsonValue::makeInteger(-1)},
        {"state", JsonValue::makeString("recovering")}, {"phase", JsonValue::makeString("info")},
        {"accessKeyCheck", JsonValue::makeString("failed")}, {"lastError", JsonValue::makeNull()},
        {"httpDiagnostic", JsonValue::makeNull()}}) {
        auto invalid = valid; set(statusOf(invalid), key, value);
        PHPVPN_CHECK(!tray::parseStatusResponse(Json::serialize(invalid)).valid);
    }
    auto invalid = valid;
    auto diagnostic = *statusOf(invalid).find("httpDiagnostic");
    set(diagnostic, "unexpected", JsonValue::makeBool(true)); set(statusOf(invalid), "httpDiagnostic", diagnostic);
    PHPVPN_CHECK(!tray::parseStatusResponse(Json::serialize(invalid)).valid);
    PHPVPN_CHECK(!tray::parseStatusResponse(std::string(kIpcMaxMessageBytes + 1, ' ')).valid);
    s.httpDiagnostic.reset();
    PHPVPN_CHECK(!Json::parse(IpcProtocol::buildStatusResponse(kRequestId, s))->find("status")->find("httpDiagnostic"));
    auto dismiss = IpcProtocol::parseRequest(IpcProtocol::buildRequest(IpcCommand::dismiss_connect_diagnostic, kRequestId,
        IpcProtocol::dismissDiagnosticPayload(7, kRequestId)));
    PHPVPN_REQUIRE_OK(dismiss); PHPVPN_CHECK_EQ(dismiss->connectionEpoch, 7u);
    for (const auto* payload : {"0", "-1", "7.5", "null", "\"7\"", "18446744073709551616"})
        PHPVPN_CHECK(!IpcProtocol::parseRequest(IpcProtocol::buildRequest(IpcCommand::dismiss_connect_diagnostic, kRequestId, payload)));
}

PHPVPN_TEST(StatusContract, ConnectHttpDiagnosticModelRetiresOldAndClearedAttempts) {
    tray::TrayStatus s; s.valid = true; s.connectionEpoch = 7; s.state = "error"; s.phase = "idle";
    s.connectAttemptId = kRequestId;
    s.hasError = true; s.errorCode = "endpoint_unavailable_or_invalid";
    s.httpDiagnostic.emplace();
    tray::ProgressModel model;
    model.onSnapshot(s, 0); PHPVPN_CHECK(model.httpDiagnostic());
    auto old = s; old.connectionEpoch = 6; old.httpDiagnostic.reset();
    model.onSnapshot(old, 0); PHPVPN_CHECK(model.httpDiagnostic());
    auto cleared = s; cleared.httpDiagnostic.reset();
    model.onSnapshot(cleared, 0); PHPVPN_CHECK(!model.httpDiagnostic());
    model.onSnapshot(s, 0); PHPVPN_CHECK(!model.httpDiagnostic());
    model = tray::ProgressModel{}; model.onSnapshot(s, 0);
    model.onSnapshot(tray::TrayStatus{}, 0); PHPVPN_CHECK(!model.httpDiagnostic());
    model.onSnapshot(s, 0); PHPVPN_CHECK(!model.httpDiagnostic());
    model = tray::ProgressModel{}; model.onSnapshot(s, 0);
    auto newer = s; newer.connectionEpoch = 8; newer.state = "connecting"; newer.httpDiagnostic.reset();
    model.onSnapshot(newer, 0); PHPVPN_CHECK(!model.httpDiagnostic()); PHPVPN_CHECK(model.view().closeRequested);
    model.onSnapshot(s, 0); PHPVPN_CHECK(!model.httpDiagnostic());
    model = tray::ProgressModel{}; model.onSnapshot(s, 0); model.onClose();
    model.onSnapshot(s, 0); PHPVPN_CHECK(!model.httpDiagnostic());
    model = tray::ProgressModel{}; model.onSnapshot(s, 0);
    auto restarted = s; restarted.connectAttemptId = "7f2a7c1e-9b44-4b0e-8d2f-5c6a1e7b9d03";
    model.onSnapshot(restarted, 0); PHPVPN_CHECK(!model.httpDiagnostic()); PHPVPN_CHECK(model.view().closeRequested);
    model = tray::ProgressModel{}; model.onSnapshot(s, 0);
    restarted.connectionEpoch = 1;
    model.onSnapshot(restarted, 0); PHPVPN_CHECK(!model.httpDiagnostic());
    model.onSnapshot(s, 0); PHPVPN_CHECK(!model.httpDiagnostic());
}
