#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <optional>
#include "core/wizard/http_diagnostic.h"

namespace phpvpn::tray {

enum class CheckpointResult { pending, success, failed };

struct TrafficCountersView {
    std::uint64_t dataPollsTotal = 0;
    std::uint64_t dataPollsEmpty = 0;
    std::uint64_t dataPollsWithPayload = 0;
    std::uint64_t serializedDataRequestBodyBytes = 0;
    std::uint64_t serializedDataResponseBodyBytes = 0;
    std::uint64_t ipv4PacketsSent = 0;
    std::uint64_t ipv4PacketsReceived = 0;
    std::uint64_t ipv4BytesSent = 0;
    std::uint64_t ipv4BytesReceived = 0;
};

struct CounterField {
    const char* key;
    const wchar_t* label;
    std::uint64_t TrafficCountersView::* member;
};
inline constexpr std::array<CounterField, 9> kCounterFields{{
    {"dataPollsTotal", L"Data polls total (dataPollsTotal)", &TrafficCountersView::dataPollsTotal},
    {"dataPollsEmpty", L"Empty data polls (dataPollsEmpty)", &TrafficCountersView::dataPollsEmpty},
    {"dataPollsWithPayload", L"Data polls with payload (dataPollsWithPayload)", &TrafficCountersView::dataPollsWithPayload},
    {"serializedDataRequestBodyBytes", L"Request body bytes (serializedDataRequestBodyBytes)", &TrafficCountersView::serializedDataRequestBodyBytes},
    {"serializedDataResponseBodyBytes", L"Response body bytes (serializedDataResponseBodyBytes)", &TrafficCountersView::serializedDataResponseBodyBytes},
    {"ipv4PacketsSent", L"IPv4 packets sent (ipv4PacketsSent)", &TrafficCountersView::ipv4PacketsSent},
    {"ipv4PacketsReceived", L"IPv4 packets received (ipv4PacketsReceived)", &TrafficCountersView::ipv4PacketsReceived},
    {"ipv4BytesSent", L"IPv4 bytes sent (ipv4BytesSent)", &TrafficCountersView::ipv4BytesSent},
    {"ipv4BytesReceived", L"IPv4 bytes received (ipv4BytesReceived)", &TrafficCountersView::ipv4BytesReceived}
}};

struct TrayStatus {
    std::uint64_t connectionEpoch = 0;
    std::optional<std::string> connectAttemptId;
    std::optional<phpvpn::HttpDiagnostic> httpDiagnostic;
    std::string state;
    std::string phase;
    CheckpointResult serverUrl = CheckpointResult::pending;
    CheckpointResult serverSecret = CheckpointResult::pending;
    CheckpointResult accessKey = CheckpointResult::pending;
    CheckpointResult communicationContracts = CheckpointResult::pending;
    std::string tunnelReady;
    bool hasError = false;
    std::string errorCode;
    std::string errorDescription;
    bool errorRecoverable = false;
    TrafficCountersView counters;
    std::optional<std::int64_t> connectionStartedAtUtcMs;
    bool valid = false;
};

TrayStatus parseStatusResponse(std::string_view json);
enum class TrayIconState : std::size_t { disconnected, connecting, connected };
TrayIconState selectTrayIcon(const TrayStatus& status, bool unavailable, bool localOperation);
// Invalid replies never replace the most recent authoritative snapshot.
bool applyStatusSnapshot(TrayStatus& current, const TrayStatus& next);

}  // namespace phpvpn::tray
