// Owner of the connection lifecycle, client.md "Stavovy automat" and "Obnova".
//
// The engine holds the only mutable runtime state of the service. Every IPC
// command is a request that is either answered from a published snapshot or
// turned into a command for the network thread, so a command never blocks on a
// network operation.
//
// The connection sequence is fixed: health, info, access key check, tunnel
// start, running. A transient failure of a `data` exchange while running
// (transport error, server 5xx, unverifiable response) enters soft recovery:
// the tunnel, its I/O roles and the queues stay in place, the phase becomes
// `reconnecting` and the exchange is retried after 250 ms, 500 ms, 1, 2 and
// then 5 seconds with jitter. A 401 first refreshes the verified time once.
// When no exchange verifies within kSoftRecoveryGraceMs, or the failure is
// recoverable but not transient, the connection is torn down and moves to
// `recovering` with an exponential backoff of 1, 2, 4, 8, 16, 32 and then 60
// seconds, plus a jitter of up to 20 percent so that many clients do not retry
// in lockstep. A non recoverable failure moves it to `error` and waits for the
// user.
#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "core/config/config_store.h"
#include "core/queues/packet_queues.h"
#include "core/runtime/network_worker.h"
#include "core/runtime/status_snapshot.h"
#include "core/runtime/tunnel_session.h"
#include "core/runtime/tunnel_workers.h"
#include "platform/iface/clock.h"
#include "platform/iface/netconfig.h"
#include "platform/iface/transport.h"
#include "platform/iface/tunnel.h"

namespace phpvpn {

// Budget of one soft recovery episode: continuous failed `data` exchanges
// beyond this window release the tunnel and start the full recovery.
inline constexpr std::int64_t kSoftRecoveryGraceMs = 90000;

struct EnginePaths {
    std::filesystem::path configFile;
    std::filesystem::path journalFile;
    std::wstring installDirectory;
    // Internal host dependency injection. Neither JSON nor public IPC changes
    // the production adapter identity.
    std::wstring adapterName = L"PHPVPN";
    std::wstring adapterGuid = L"{7d1b4d2f-1f34-4d0e-9a3e-2f6c9b5a41d8}";
};
struct ConnectionIdentity {
    std::uint64_t epoch = 0;
    ConnectionState state = ConnectionState::disconnected;
    ConnectionPhase phase = ConnectionPhase::idle;
    bool connectPending = false;
};

class VpnEngine {
public:
    VpnEngine(CryptoProvider& crypto,
              Transport& transport,
              Clock& clock,
              TunnelDriver& tunnelDriver,
              NetConfig& netConfig,
              EnginePaths paths,
              ConfigStore& configStore);
    ~VpnEngine();

    VpnEngine(const VpnEngine&) = delete;
    VpnEngine& operator=(const VpnEngine&) = delete;

    // Starts the network thread. The engine stays disconnected until `connect`.
    Status start();

    // Stops every worker and releases the adapter. Safe to call more than once.
    void shutdown();

    // IPC surface. Connect waits only for the network writer's acceptance
    // publication, never for network I/O or the bootstrap result.
    Status connect();
    std::uint64_t startupControlToken() const noexcept { return controlGeneration_.load(); }
    Status connectAtStartup(std::uint64_t token, std::uint64_t epoch);
    void publishStartupError(std::uint64_t token, std::uint64_t epoch, const Error& error);
    Status disconnect();
    void dismissConnectDiagnostic(std::uint64_t epoch, std::string_view attemptId);
    StatusSnapshot status() const;
    DiagnosticsSnapshot diagnostics() const;

    // Exact stored configuration document, or an error when none exists.
    Result<std::string> storedConfigJson() const;

    // Validates and stores a new configuration. The active connection keeps the
    // snapshot it started with, plan_client.md decision 9.
    Status setConfigJson(std::string_view json);

    // Verified `info` plaintext of the current connection.
    Result<std::string> lastInfoPlaintext() const;

    // One shot `health` followed by `info`, on a transport of its own so that a
    // running connection is not disturbed. Never starts the tunnel, the worker
    // roles or a `data` poll, and never changes the connection state.
    Result<std::string> fetchInfoOnce();
    Result<ServerInfoSnapshot> fetchServerInfoOnce();

    ConnectionIdentity connectionIdentity() const;
    // Dedicated ordering guard for accepted Connect and file commit. This is
    // never the state mutex used by get-status or Disconnect.
    std::mutex& transitionMutex() noexcept { return transitionMutex_; }
    void setDiagnosticHooks(std::function<Status()> beforeConnect, std::function<void()> cancelWizard);
    void cancelDiagnostics();

private:
    friend struct VpnEngineTestAccess;
    enum class Command { none, connect, disconnect, stop };

    void runNetworkThread();
    Status connectWithGeneration(std::uint64_t generation, std::optional<std::uint64_t> startupEpoch = {});
    void pushCommand(Command command);
    Command takeCommand(std::int64_t waitMs);
    // Waits until the next `data` request is due, a command arrives first, or
    // a newly queued outbound packet shortened the wait to the short interval.
    Command waitForNextPoll(std::int64_t deadlineMs, std::int64_t shortIntervalMs);
    void onOutboundPacketQueued();

    Status runConnectSequence(bool initialBootstrap, std::optional<HttpDiagnostic>* failedDiagnostic = nullptr,
        bool* rejectedResponse = nullptr);
    std::int64_t runConnectedIteration();
    void teardown(bool keepError);

    void setState(ConnectionState state, ConnectionPhase phase);
    void setPhase(ConnectionPhase phase);
    void recordError(const Error& error);
    void clearError();
    // True after tearing a connection down for server_version_incompatible.
    bool stopForIncompatibleServer(const Result<HealthResponse>& health);

    std::int64_t backoffDelayMs();
    std::int64_t softBackoffDelayMs();

    CryptoProvider& crypto_;
    Transport& transport_;
    Clock& clock_;
    TunnelDriver& tunnelDriver_;
    NetConfig& netConfig_;
    EnginePaths paths_;
    ConfigStore& configStore_;
    std::optional<Error> startupError_;
    std::mutex transitionMutex_;
    std::uint64_t connectionEpoch_ = 0;
    std::optional<std::string> connectAttemptId_;
    std::optional<HttpDiagnostic> connectHttpDiagnostic_;
    std::function<Status()> beforeConnect_;
    std::function<void()> cancelWizard_;

    NetworkWorker network_;
    TrafficCounters counters_;
    TunnelSession tunnelSession_;

    // Hash secret survives reconnects, queues belong to a connection epoch.
    std::unique_ptr<HmacFlowHasher> flowHasher_;
    std::unique_ptr<DirectionQueues> outbound_;
    std::unique_ptr<DirectionQueues> inbound_;
    DropCounters outboundDrops_;
    DropCounters inboundDrops_;
    TunnelWorkerCounters tunnelCounters_;

    std::unique_ptr<TunnelInputWorker> inputWorker_;
    std::unique_ptr<TunnelOutputWorker> outputWorker_;

    mutable std::mutex stateMutex_;
    std::condition_variable stateSignal_;
    ConnectionState state_ = ConnectionState::disconnected;
    ConnectionPhase phase_ = ConnectionPhase::idle;
    std::string stateChangedAtUtc_;
    std::optional<std::string> connectionStartedAtUtc_;
    CheckpointResult serverUrlCheck_ = CheckpointResult::pending;
    CheckpointResult serverSecretCheck_ = CheckpointResult::pending;
    CheckpointResult accessKeyCheck_ = CheckpointResult::pending;
    CheckpointResult communicationContractsCheck_ = CheckpointResult::pending;
    bool communicationContractsVerifiedForEpoch_ = false;
    bool healthSyntaxUsable_ = false;
    bool connectCommandPending_ = false;
    Tristate serverReachable_ = Tristate::unknown;
    Tristate serverVerified_ = Tristate::unknown;
    Tristate tunnelReady_ = Tristate::unknown;
    Tristate lastHealthVerified_ = Tristate::unknown;
    Tristate lastInfoVerified_ = Tristate::unknown;
    Tristate lastDataVerified_ = Tristate::unknown;
    std::optional<SafeError> lastError_;
    std::string lastRecoveryAtUtc_;
    std::uint64_t recoveryCount_ = 0;
    std::string lastSoftRecoveryAtUtc_;
    std::uint64_t softRecoveryCount_ = 0;
    std::uint64_t packetsReceived_ = 0;
    std::uint64_t packetsSent_ = 0;
    std::uint64_t responsesRejected_ = 0;
    std::uint64_t batchesRejected_ = 0;
    std::uint64_t peakResponseCiphertextBytes_ = 0;
    std::string lastInfoPlaintext_;

    mutable std::mutex configMutex_;
    ClientConfigPtr pendingConfig_;
    bool configurationValid_ = false;
    ConfigWarnings storedWarnings_;
    std::atomic<std::uint64_t> controlGeneration_{0};
    ClientConfigPtr activeConfig_;

    // Serialises the one shot `get-info` exchanges of several local clients.
    std::mutex infoMutex_;
    std::mutex diagnosticMutex_;
    std::shared_ptr<Transport> diagnosticTransport_;
    std::atomic<std::uint64_t> diagnosticGeneration_{0};

    mutable std::mutex commandMutex_;
    std::condition_variable commandSignal_;
    std::deque<Command> commands_;
    // Guarded by commandMutex_. The deadline exists only while the network
    // thread waits for the next poll; a packet queued while a request is in
    // flight is remembered and applied once that wait begins.
    std::optional<std::int64_t> pollDeadlineMs_;
    std::int64_t pollShortIntervalMs_ = 0;
    std::optional<std::int64_t> outboundQueuedAtMs_;

    std::atomic<bool> running_{false};
    std::atomic<bool> connectRequested_{false};
    std::thread thread_;

    int backoffStep_ = 0;
    std::int64_t startedAtMs_ = 0;
    std::uint32_t jitterState_ = 0x9e3779b9u;

    // Soft recovery state, owned by the network thread.
    std::optional<Error> lastDataError_;
    bool softRecoveryActive_ = false;
    std::int64_t softRecoveryStartedAtMs_ = 0;
    int softBackoffStep_ = 0;
    bool softAuthRefreshed_ = false;
    std::int64_t softRecoveryGraceMs_ = kSoftRecoveryGraceMs;
};

}  // namespace phpvpn
