// Connection state, phase and the two safe snapshots the IPC contract exposes.
//
// The `get-status` snapshot holds exactly the nine items of client.md and must
// never contain a URL, configuration, an IP address, a key, an authentication or
// GCM tag, a ciphertext or a detailed network error. The `get-diagnostics`
// snapshot is wider but obeys the same prohibitions.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "core/base/error.h"

namespace phpvpn {

enum class ConnectionState { disconnected, connecting, connected, recovering, error };

enum class ConnectionPhase { idle, health, info, access_key_check, starting_tunnel, running, stopping };

enum class Tristate { unknown, yes, no };

const char* connectionStateName(ConnectionState state) noexcept;
const char* connectionPhaseName(ConnectionPhase phase) noexcept;
const char* tristateName(Tristate value) noexcept;

struct SafeError {
    std::string code;
    std::string description;
    bool recoverable = false;

    static SafeError from(const Error& error);
};

struct StatusSnapshot {
    ConnectionState state = ConnectionState::disconnected;
    ConnectionPhase phase = ConnectionPhase::idle;
    std::string stateChangedAtUtc;
    Tristate serverReachable = Tristate::unknown;
    Tristate serverVerified = Tristate::unknown;
    Tristate tunnelReady = Tristate::unknown;
    std::optional<SafeError> lastError;
    std::uint64_t packetsReceived = 0;
    std::uint64_t packetsSent = 0;
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

    std::uint64_t packetsReceived = 0;
    std::uint64_t packetsSent = 0;
    std::uint64_t packetsDropped = 0;
    std::uint64_t batchesRejected = 0;
    std::uint64_t responsesRejected = 0;
    std::uint64_t peakResponseCiphertextBytes = 0;

    std::optional<SafeError> lastError;
};

}  // namespace phpvpn
