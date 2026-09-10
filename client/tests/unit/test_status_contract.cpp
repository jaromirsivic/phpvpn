#include "harness.h"

#include <algorithm>
#include <array>
#include <limits>

#include "core/ipc/ipc_protocol.h"
#include "core/protocol/json.h"
#include "../../apps/tray/status_model.h"

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
        PHPVPN_CHECK_EQ(status->objectValue().size(), 23U);
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
