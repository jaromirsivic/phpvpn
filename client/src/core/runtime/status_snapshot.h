// Connection state, phase and the two safe snapshots the IPC contract exposes.
//
// The `get-status` snapshot holds the state, checkpoints and nine counters of
// client.md and must
// keep its public fields free of raw protocol data. The optional, bounded
// initial-Connect httpDiagnostic is the sole explicit local exception.
// The `get-diagnostics` snapshot never carries this raw HTTP object.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "core/base/error.h"
#include "core/runtime/traffic_counters.h"
#include "core/wizard/http_diagnostic.h"

namespace phpvpn {

enum class ConnectionState { disconnected, connecting, connected, recovering, error };

enum class ConnectionPhase { idle, health, info, access_key_check, communication_contracts_check, starting_tunnel, running, stopping };

enum class Tristate { unknown, yes, no };
enum class CheckpointResult { pending, success, failed };
const char* checkpointResultName(CheckpointResult value) noexcept;

const char* connectionStateName(ConnectionState state) noexcept;
const char* connectionPhaseName(ConnectionPhase phase) noexcept;
const char* tristateName(Tristate value) noexcept;

struct SafeError {
    std::string code;
    std::string description;
    bool recoverable = false;

    static SafeError from(const Error& error);
    static SafeError fromConnection(const Error& error, ConnectionPhase phase,
                                    bool healthSyntaxUsable = false);
};

struct StatusSnapshot {
    std::uint64_t connectionEpoch = 0;
    std::optional<std::string> connectAttemptId;
    std::optional<HttpDiagnostic> httpDiagnostic;
    ConnectionState state = ConnectionState::disconnected;
    ConnectionPhase phase = ConnectionPhase::idle;
    std::string stateChangedAtUtc;
    std::optional<std::string> connectionStartedAtUtc;
    CheckpointResult serverUrlCheck = CheckpointResult::pending;
    CheckpointResult serverSecretCheck = CheckpointResult::pending;
    CheckpointResult accessKeyCheck = CheckpointResult::pending;
    CheckpointResult communicationContractsCheck = CheckpointResult::pending;
    Tristate serverReachable = Tristate::unknown;
    Tristate serverVerified = Tristate::unknown;
    Tristate tunnelReady = Tristate::unknown;
    std::optional<SafeError> lastError;
    TrafficCountersSnapshot counters;
    std::uint64_t recoveryCount = 0;
    std::string clientVersion;
    int ipcVersion = 1;
};

struct QueueDiagnostics {
    std::uint64_t itemCount = 0;
    std::int64_t occupiedBytes = 0;
    std::int64_t limitBytes = 0;
    std::uint64_t peerQueueCount = 0;
};

struct DiagnosticsSnapshot {
    ConnectionState state = ConnectionState::disconnected;
    ConnectionPhase phase = ConnectionPhase::idle;
    std::string clientVersion;
    std::int64_t serviceUptimeSeconds = 0;

    bool configurationValid = false;
    std::vector<std::string> configurationWarnings;

    Tristate lastHealthVerified = Tristate::unknown;
    Tristate lastInfoVerified = Tristate::unknown;
    Tristate lastDataVerified = Tristate::unknown;
    std::string lastRecoveryAtUtc;
    std::uint64_t recoveryCount = 0;

    Tristate tunnelReady = Tristate::unknown;

    QueueDiagnostics outbound;
    QueueDiagnostics inbound;

    TrafficCountersSnapshot counters;
    std::uint64_t packetsDropped = 0;
    std::uint64_t batchesRejected = 0;
    std::uint64_t responsesRejected = 0;
    std::uint64_t peakResponseCiphertextBytes = 0;

    std::optional<SafeError> lastError;
};

}  // namespace phpvpn
