#include "core/runtime/vpn_engine.h"

#include <algorithm>
#include <limits>
#include <fstream>
#include <system_error>
#include <utility>

#include "core/base/logger.h"
#include "core/protocol/iso8601.h"

namespace phpvpn {
namespace {

constexpr const char* kComponent = "engine";

// client.md "Obnova": 1, 2, 4, 8, 16, 32 and then 60 seconds.
constexpr std::int64_t kBackoffLadderMs[] = {1000, 2000, 4000, 8000, 16000, 32000, 60000};
constexpr int kBackoffLadderSize = static_cast<int>(sizeof(kBackoffLadderMs) / sizeof(kBackoffLadderMs[0]));

}  // namespace

VpnEngine::VpnEngine(CryptoProvider& crypto,
                     Transport& transport,
                     Clock& clock,
                     TunnelDriver& tunnelDriver,
                     NetConfig& netConfig,
                     EnginePaths paths,
                     ConfigStore& configStore)
    : crypto_(crypto),
      transport_(transport),
      clock_(clock),
      tunnelDriver_(tunnelDriver),
      netConfig_(netConfig),
      paths_(std::move(paths)),
      configStore_(configStore),
      network_(crypto, transport, clock),
      tunnelSession_(tunnelDriver, netConfig) {}

VpnEngine::~VpnEngine() {
    shutdown();
}

Status VpnEngine::start() {
    if (running_.exchange(true)) {
        return Error(ErrorCode::service_already_running, "the engine is already running");
    }

    startedAtMs_ = clock_.monotonicMs();
    startupError_.reset();
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        stateChangedAtUtc_ = Iso8601::format(clock_.systemUtcMs());
    }

    // Leftovers of an abnormally terminated run are removed before anything
    // else touches the network configuration.
    TunnelSessionSettings settings;
    settings.adapterName = paths_.adapterName;
    settings.requestedGuid = paths_.adapterGuid;
    settings.journalPath = paths_.journalFile;
    settings.installDirectory = paths_.installDirectory;
    auto recovered = configStore_.operationalStatus();
    if (recovered) recovered = tunnelSession_.recoverPreviousRun(settings);
    if (!recovered.ok()) {
        PHPVPN_LOG_ERROR(kComponent, recovered.error().message());
        running_.store(false);
        startupError_ = recovered.error();
        recordError(recovered.error());
        setState(ConnectionState::error, ConnectionPhase::idle);
        return recovered.error();
    }

    thread_ = std::thread([this] { runNetworkThread(); });
    return Status::success();
}

void VpnEngine::shutdown() {
    controlGeneration_.fetch_add(1);
    const bool wasRunning = running_.exchange(false);
    cancelDiagnostics();
    if (!wasRunning) {
        if (thread_.joinable()) {
            thread_.join();
        }
        return;
    }
    pushCommand(Command::stop);
    transport_.cancelCurrentRequest();
    if (thread_.joinable()) {
        thread_.join();
    }
}

void VpnEngine::pushCommand(Command command) {
    {
        std::lock_guard<std::mutex> lock(commandMutex_);
        commands_.push_back(command);
    }
    commandSignal_.notify_all();
}

VpnEngine::Command VpnEngine::takeCommand(std::int64_t waitMs) {
    std::unique_lock<std::mutex> lock(commandMutex_);
    if (commands_.empty() && waitMs > 0) {
        commandSignal_.wait_for(lock, std::chrono::milliseconds(waitMs),
                                [this] { return !commands_.empty(); });
    }
    if (commands_.empty()) {
        return Command::none;
    }
    const Command command = commands_.front();
    commands_.pop_front();
    return command;
}

Result<std::string> VpnEngine::storedConfigJson() const {
    auto snapshot = configStore_.read();
    if (!snapshot) return snapshot.error();
    if (!snapshot->document) return Error(snapshot->reasonCode.value_or(ErrorCode::config_not_found));
    return *snapshot->document;
}

Status VpnEngine::setConfigJson(std::string_view json) {
    std::string commitId;
    PHPVPN_TRY_ASSIGN(commitId, crypto_.randomUuidV4());
    auto commit = configStore_.save(json, commitId);
    if (!commit) return commit.error();
    if (commit->state != ConfigPublishState::confirmed)
        return Error(ErrorCode::config_write_failed, "the publication is not confirmed");
    PHPVPN_LOG_INFO(kComponent, "a new configuration was stored");
    return ok();
}

Result<std::string> VpnEngine::lastInfoPlaintext() const {
    std::lock_guard<std::mutex> lock(stateMutex_);
    if (lastInfoPlaintext_.empty()) {
        return Error(ErrorCode::state_conflict, "no verified server information is available");
    }
    return lastInfoPlaintext_;
}

Result<std::string> VpnEngine::fetchInfoOnce() {
    std::unique_lock lock(infoMutex_, std::try_to_lock);
    if (!lock.owns_lock()) return Error(ErrorCode::busy);
    const auto generation = diagnosticGeneration_.load();
    ClientConfigPtr config;
    PHPVPN_TRY_ASSIGN(config, configStore_.readValid());

    std::unique_ptr<Transport> created;
    PHPVPN_TRY_ASSIGN(created, createPlatformTransport());
    std::shared_ptr<Transport> transport(std::move(created));
    {
        std::lock_guard diagnosticLock(diagnosticMutex_);
        if (!running_.load() || generation != diagnosticGeneration_.load()) return Error(ErrorCode::cancelled);
        diagnosticTransport_ = transport;
    }
    struct Cleanup {
        VpnEngine& engine;
        ~Cleanup() { std::lock_guard diagnosticLock(engine.diagnosticMutex_); engine.diagnosticTransport_.reset(); }
    } cleanup{*this};
    NetworkWorker worker(crypto_, *transport, clock_);
    PHPVPN_TRY(worker.configure(config, nullptr, [this, generation] {
        return !running_.load() || generation != diagnosticGeneration_.load();
    }));

    HealthResponse health;
    PHPVPN_TRY_ASSIGN(health, worker.performHealth());
    (void)health;

    InfoResponse info;
    PHPVPN_TRY_ASSIGN(info, worker.performInfo());
    return info.plaintext;
}

Status VpnEngine::connect() {
    if (startupError_) return *startupError_;
    if (!running_.load()) return Error(ErrorCode::service_not_running);
    const auto generation = controlGeneration_.load();
    {
        std::unique_lock lock(stateMutex_);
        if (state_ == ConnectionState::connecting || state_ == ConnectionState::connected ||
            state_ == ConnectionState::recovering) return ok();
        if (connectCommandPending_) {
            stateSignal_.wait(lock, [this] { return !connectCommandPending_ || !running_.load(); });
            return ok();
        }
    }
    std::unique_lock transitionLock(transitionMutex_);
    {
        std::lock_guard lock(stateMutex_);
        if (state_ == ConnectionState::connecting || state_ == ConnectionState::connected ||
            state_ == ConnectionState::recovering) return ok();
    }
    // An action reads once, outside the state lock. Concurrent readers can
    // share acceptance, but only the first accepted snapshot becomes active.
    PHPVPN_TRY(configStore_.operationalStatus());
    auto snapshot = configStore_.read();
    if (!snapshot) return snapshot.error();
    {
        std::lock_guard lock(configMutex_);
        configurationValid_ = snapshot->config != nullptr;
        storedWarnings_ = snapshot->warnings;
    }
    if (!snapshot->config) return Error(snapshot->reasonCode.value_or(ErrorCode::config_invalid_value));
    if (beforeConnect_) PHPVPN_TRY(beforeConnect_());
    {
        std::unique_lock lock(stateMutex_);
        if (generation != controlGeneration_.load()) return Error(ErrorCode::cancelled);
        if (!running_.load()) return Error(ErrorCode::service_not_running);
        if (state_ == ConnectionState::connecting || state_ == ConnectionState::connected ||
            state_ == ConnectionState::recovering) return ok();
        if (!connectCommandPending_ && connectionEpoch_ == (std::numeric_limits<std::uint64_t>::max)()) return Error(ErrorCode::state_conflict);
        connectRequested_.store(true);
        if (!connectCommandPending_) {
            {
                std::lock_guard configLock(configMutex_);
                pendingConfig_ = snapshot->config;
            }
            ++connectionEpoch_;
            connectCommandPending_ = true;
            pushCommand(Command::connect);
        }
        stateSignal_.wait(lock, [this] { return !connectCommandPending_ || !running_.load(); });
    }
    return Status::success();
}

Status VpnEngine::disconnect() {
    controlGeneration_.fetch_add(1);
    cancelDiagnostics();
    connectRequested_.store(false);
    if (!running_.load()) return ok();
    pushCommand(Command::disconnect);
    transport_.cancelCurrentRequest();
    return Status::success();
}

ConnectionIdentity VpnEngine::connectionIdentity() const {
    std::lock_guard lock(stateMutex_);
    return {connectionEpoch_, state_, phase_, connectCommandPending_};
}
void VpnEngine::setDiagnosticHooks(std::function<Status()> beforeConnect, std::function<void()> cancelWizard) {
    beforeConnect_ = std::move(beforeConnect);
    cancelWizard_ = std::move(cancelWizard);
}
void VpnEngine::cancelDiagnostics() {
    diagnosticGeneration_.fetch_add(1);
    std::shared_ptr<Transport> transport;
    { std::lock_guard lock(diagnosticMutex_); transport = diagnosticTransport_; }
    if (transport) transport->cancelCurrentRequest();
    if (cancelWizard_) cancelWizard_();
}

void VpnEngine::setState(ConnectionState state, ConnectionPhase phase) {
    std::lock_guard<std::mutex> lock(stateMutex_);
    state_ = state;
    phase_ = phase;
    stateChangedAtUtc_ = Iso8601::format(clock_.systemUtcMs());
}

void VpnEngine::setPhase(ConnectionPhase phase) {
    std::lock_guard<std::mutex> lock(stateMutex_);
    phase_ = phase;
}

void VpnEngine::recordError(const Error& error) {
    std::lock_guard<std::mutex> lock(stateMutex_);
    lastError_ = SafeError::fromConnection(error, phase_, healthSyntaxUsable_);
}

void VpnEngine::clearError() {
    std::lock_guard<std::mutex> lock(stateMutex_);
    lastError_.reset();
}

std::int64_t VpnEngine::backoffDelayMs() {
    const int step = std::min(backoffStep_, kBackoffLadderSize - 1);
    const std::int64_t base = kBackoffLadderMs[step];

    // Deterministic jitter of up to 20 percent, so a fleet of clients does not
    // retry in lockstep after a shared outage.
    jitterState_ = jitterState_ * 1664525u + 1013904223u;
    const std::int64_t jitter = static_cast<std::int64_t>(jitterState_ >> 16) % (base / 5 + 1);
    return base + jitter;
}

Status VpnEngine::runConnectSequence(bool initialBootstrap) {
    setPhase(ConnectionPhase::health);
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        healthSyntaxUsable_ = false;
    }
    ClientConfigPtr config;
    {
        std::lock_guard<std::mutex> lock(configMutex_);
        if (initialBootstrap) activeConfig_ = std::move(pendingConfig_);
        config = activeConfig_;
    }
    if (!config) {
        return Error(ErrorCode::config_missing, "no configuration is stored");
    }

    {
        std::lock_guard<std::mutex> lock(configMutex_);
        activeConfig_ = config;
    }

    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        outbound_ = std::make_unique<DirectionQueues>(config->runtime.maxOutboundQueueBytes,
                                                     config->runtime.minPerPeerQueueBytes,
                                                     config->maxRemotePeers());
        inbound_ = std::make_unique<DirectionQueues>(config->runtime.maxInboundQueueBytes,
                                                    config->runtime.minPerPeerQueueBytes,
                                                    config->maxRemotePeers());
    }

    auto configured = network_.configure(config, &counters_,
        [this] { return !running_.load() || !connectRequested_.load(); });
    if (!configured.ok()) {
        if (initialBootstrap) {
            std::lock_guard<std::mutex> lock(stateMutex_);
            serverUrlCheck_ = CheckpointResult::failed;
        }
        return configured.error();
    }
    if (!running_.load() || !connectRequested_.load()) return Error(ErrorCode::cancelled);
    auto health = network_.performHealth([this, initialBootstrap] {
        std::lock_guard<std::mutex> lock(stateMutex_);
        healthSyntaxUsable_ = true;
        serverReachable_ = Tristate::yes;
        if (initialBootstrap) serverUrlCheck_ = CheckpointResult::success;
    });
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        serverReachable_ = healthSyntaxUsable_ ? Tristate::yes : Tristate::no;
        lastHealthVerified_ = health.ok() ? Tristate::yes : Tristate::no;
        serverVerified_ = health.ok() ? Tristate::yes : Tristate::no;
        if (initialBootstrap && !health.ok()) {
            (healthSyntaxUsable_ ? serverSecretCheck_ : serverUrlCheck_) = CheckpointResult::failed;
        }
    }
    if (!health.ok()) {
        return health.error();
    }

    setPhase(ConnectionPhase::info);
    if (!running_.load() || !connectRequested_.load()) return Error(ErrorCode::cancelled);
    auto info = network_.performInfo();
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        lastInfoVerified_ = info.ok() ? Tristate::yes : Tristate::no;
        if (initialBootstrap)
            serverSecretCheck_ = info.ok() ? CheckpointResult::success : CheckpointResult::failed;
        if (info.ok()) {
            lastInfoPlaintext_ = info.value().plaintext;
        }
    }
    if (!info.ok()) {
        return info.error();
    }

    setPhase(ConnectionPhase::access_key_check);
    if (!running_.load() || !connectRequested_.load()) return Error(ErrorCode::cancelled);
    auto accessKey = network_.verifyAccessKey(*inbound_, inboundDrops_);
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        lastDataVerified_ = accessKey.ok() ? Tristate::yes : Tristate::no;
        if (initialBootstrap)
            accessKeyCheck_ = accessKey.ok() ? CheckpointResult::success : CheckpointResult::failed;
    }
    if (!accessKey.ok()) {
        return accessKey.error();
    }

    setPhase(ConnectionPhase::starting_tunnel);
    if (!running_.load() || !connectRequested_.load()) return Error(ErrorCode::cancelled);
    TunnelSessionSettings settings;
    settings.adapterName = paths_.adapterName;
    settings.requestedGuid = paths_.adapterGuid;
    settings.journalPath = paths_.journalFile;
    settings.installDirectory = paths_.installDirectory;

    auto started = tunnelSession_.start(*config, settings);
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        tunnelReady_ = started.ok() ? Tristate::yes : Tristate::no;
    }
    if (!started.ok()) {
        return started.error();
    }

    try {
        inputWorker_ = std::make_unique<TunnelInputWorker>(config, tunnelSession_.tunnel(), *outbound_,
                                                           outboundDrops_, tunnelCounters_);
        outputWorker_ = std::make_unique<TunnelOutputWorker>(tunnelSession_.tunnel(), *inbound_,
                                                             inboundDrops_, tunnelCounters_);
        inputWorker_->start();
        outputWorker_->start();
    } catch (const std::exception&) {
        return Error(ErrorCode::tunnel_session_failed, "the worker roles could not be started");
    }

    backoffStep_ = 0;
    clearError();
    setState(ConnectionState::connected, ConnectionPhase::running);
    PHPVPN_LOG_INFO(kComponent, "the connection is established");
    return Status::success();
}

void VpnEngine::runConnectedIteration() {
    auto outcome = network_.exchangeData(*outbound_, *inbound_, outboundDrops_, inboundDrops_,
                                         /*allowSending=*/true);
    if (!outcome.ok()) {
        std::lock_guard<std::mutex> lock(stateMutex_);
        ++responsesRejected_;
        lastDataVerified_ = Tristate::no;
        lastError_ = SafeError::fromConnection(outcome.error(), ConnectionPhase::running);
        return;
    }

    const DataExchangeOutcome& result = outcome.value();
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        lastDataVerified_ = Tristate::yes;
        packetsSent_ += result.packetsSent;
        packetsReceived_ += result.packetsReceived;
        batchesRejected_ += result.batchesRejected;
        peakResponseCiphertextBytes_ =
            std::max<std::uint64_t>(peakResponseCiphertextBytes_, result.decodedCiphertextBytes);
    }

    if (result.packetsReceived > 0 && outputWorker_) {
        outputWorker_->notifyDataAvailable();
    }
}

void VpnEngine::teardown(bool keepError) {
    if (inputWorker_) {
        inputWorker_->stop();
        inputWorker_.reset();
    }
    if (outputWorker_) {
        outputWorker_->stop();
        outputWorker_.reset();
    }

    if (tunnelSession_.active()) {
        auto stopped = tunnelSession_.stop();
        if (!stopped.ok()) {
            PHPVPN_LOG_ERROR(kComponent, stopped.error().message());
        }
    }

    network_.reset();

    if (outbound_) {
        outbound_->clear();
    }
    if (inbound_) {
        inbound_->clear();
    }

    std::lock_guard<std::mutex> lock(stateMutex_);
    tunnelReady_ = Tristate::no;
    serverVerified_ = Tristate::unknown;
    lastDataVerified_ = Tristate::unknown;
    if (!keepError) {
        lastError_.reset();
    }
}

void VpnEngine::runNetworkThread() {
    std::int64_t retryAtMs = 0;

    while (running_.load()) {
        ConnectionState currentState;
        {
            std::lock_guard<std::mutex> lock(stateMutex_);
            currentState = state_;
        }

        std::int64_t waitMs = 250;
        if (currentState == ConnectionState::connected) {
            waitMs = 0;
        } else if (currentState == ConnectionState::recovering) {
            const std::int64_t now = clock_.monotonicMs();
            waitMs = retryAtMs > now ? retryAtMs - now : 0;
            waitMs = std::min<std::int64_t>(waitMs, 500);
        }

        const Command command = takeCommand(waitMs);
        if (command == Command::stop) {
            break;
        }
        if (command == Command::disconnect) {
            setPhase(ConnectionPhase::stopping);
            teardown(/*keepError=*/false);
            {
                std::lock_guard<std::mutex> lock(stateMutex_);
                counters_.reset();
                serverUrlCheck_ = serverSecretCheck_ = accessKeyCheck_ = CheckpointResult::pending;
                packetsReceived_ = packetsSent_ = 0;
                state_ = ConnectionState::disconnected;
                phase_ = ConnectionPhase::idle;
                stateChangedAtUtc_ = Iso8601::format(clock_.systemUtcMs());
            }
            backoffStep_ = 0;
            continue;
        }
        if (command == Command::connect &&
            (currentState == ConnectionState::disconnected || currentState == ConnectionState::error)) {
            {
                std::lock_guard<std::mutex> lock(stateMutex_);
                counters_.reset();
                serverUrlCheck_ = serverSecretCheck_ = accessKeyCheck_ = CheckpointResult::pending;
                packetsReceived_ = packetsSent_ = 0;
                lastError_.reset();
                state_ = ConnectionState::connecting;
                connectCommandPending_ = false;
                phase_ = ConnectionPhase::health;
                stateChangedAtUtc_ = Iso8601::format(clock_.systemUtcMs());
            }
            stateSignal_.notify_all();
            backoffStep_ = 0;
            auto sequence = runConnectSequence(true);
            if (!sequence.ok()) {
                recordError(sequence.error());
                teardown(/*keepError=*/true);
                if (status().lastError->recoverable) {
                    ++backoffStep_;
                    retryAtMs = clock_.monotonicMs() + backoffDelayMs();
                    setState(ConnectionState::recovering, ConnectionPhase::idle);
                } else {
                    setState(ConnectionState::error, ConnectionPhase::idle);
                }
            }
            continue;
        }

        {
            std::lock_guard<std::mutex> lock(stateMutex_);
            currentState = state_;
        }

        if (currentState == ConnectionState::connected) {
            const std::int64_t before = clock_.monotonicMs();
            runConnectedIteration();

            bool healthy = true;
            {
                std::lock_guard<std::mutex> lock(stateMutex_);
                healthy = lastDataVerified_ == Tristate::yes;
            }

            if (!healthy) {
                bool recoverable = false;
                {
                    std::lock_guard<std::mutex> lock(stateMutex_);
                    recoverable = lastError_.has_value() && lastError_->recoverable;
                }
                if (!recoverable) {
                    teardown(/*keepError=*/true);
                    setState(ConnectionState::error, ConnectionPhase::idle);
                    continue;
                }
                PHPVPN_LOG_WARN(kComponent, "the data exchange failed, entering recovery");
                teardown(/*keepError=*/true);
                {
                    std::lock_guard<std::mutex> lock(stateMutex_);
                    ++recoveryCount_;
                    lastRecoveryAtUtc_ = Iso8601::format(clock_.systemUtcMs());
                }
                ++backoffStep_;
                retryAtMs = clock_.monotonicMs() + backoffDelayMs();
                setState(ConnectionState::recovering, ConnectionPhase::idle);
                continue;
            }

            // Refresh the verified time well before the safety margin can drift.
            const std::int64_t now = clock_.monotonicMs();
            ClientConfigPtr active;
            {
                std::lock_guard<std::mutex> lock(configMutex_);
                active = activeConfig_;
            }
            const std::int64_t syncIntervalMs =
                active ? active->runtime.timeSyncIntervalSeconds * 1000 : 300000;
            const auto& snapshot = network_.timeSync().snapshot();
            const bool needsResync =
                !snapshot.has_value() ||
                now - snapshot->synchronisedAtMonoMs >= syncIntervalMs ||
                network_.timeSync().systemClockChanged(now, clock_.systemUtcMs());
            if (needsResync) {
                auto health = network_.performHealth();
                std::lock_guard<std::mutex> lock(stateMutex_);
                lastHealthVerified_ = health.ok() ? Tristate::yes : Tristate::no;
            }

            const std::int64_t elapsed = clock_.monotonicMs() - before;
            const std::int64_t delay = network_.nextPollDelayMs(
                packetsReceived_ > 0 || packetsSent_ > 0);
            const std::int64_t remaining = delay > elapsed ? delay - elapsed : 0;
            if (remaining > 0) {
                const Command queued = takeCommand(remaining);
                if (queued == Command::stop) {
                    break;
                }
                if (queued == Command::disconnect) {
                    pushCommand(Command::disconnect);
                }
            }
            continue;
        }

        if (currentState == ConnectionState::recovering && connectRequested_.load()) {
            if (clock_.monotonicMs() >= retryAtMs) {
                auto sequence = runConnectSequence(false);
                if (!sequence.ok()) {
                    recordError(sequence.error());
                    teardown(/*keepError=*/true);
                    if (status().lastError->recoverable) {
                        ++backoffStep_;
                        retryAtMs = clock_.monotonicMs() + backoffDelayMs();
                        setState(ConnectionState::recovering, ConnectionPhase::idle);
                    } else {
                        setState(ConnectionState::error, ConnectionPhase::idle);
                    }
                }
            }
        }
    }

    teardown(/*keepError=*/true);
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        counters_.reset();
        serverUrlCheck_ = serverSecretCheck_ = accessKeyCheck_ = CheckpointResult::pending;
        state_ = ConnectionState::disconnected;
        phase_ = ConnectionPhase::idle;
        stateChangedAtUtc_ = Iso8601::format(clock_.systemUtcMs());
        connectCommandPending_ = false;
    }
    stateSignal_.notify_all();
    running_.store(false);
}

StatusSnapshot VpnEngine::status() const {
    std::lock_guard<std::mutex> lock(stateMutex_);
    StatusSnapshot snapshot;
    snapshot.state = state_;
    snapshot.phase = phase_;
    snapshot.stateChangedAtUtc = stateChangedAtUtc_;
    snapshot.serverUrlCheck = serverUrlCheck_;
    snapshot.serverSecretCheck = serverSecretCheck_;
    snapshot.accessKeyCheck = accessKeyCheck_;
    snapshot.serverReachable = serverReachable_;
    snapshot.serverVerified = serverVerified_;
    snapshot.tunnelReady = tunnelReady_;
    snapshot.lastError = lastError_;
    snapshot.counters = counters_.snapshot();
    snapshot.recoveryCount = recoveryCount_;
    snapshot.clientVersion = PHPVPN_CLIENT_VERSION;
    snapshot.ipcVersion = PHPVPN_IPC_VERSION;
    return snapshot;
}

DiagnosticsSnapshot VpnEngine::diagnostics() const {
    DiagnosticsSnapshot snapshot;

    {
        std::lock_guard<std::mutex> lock(configMutex_);
        snapshot.configurationValid = configurationValid_;
        snapshot.configurationWarnings = storedWarnings_.messages();
    }

    std::lock_guard<std::mutex> lock(stateMutex_);
    if (outbound_) {
        const QueueStatistics stats = outbound_->statistics();
        snapshot.outbound.itemCount = stats.itemCount;
        snapshot.outbound.occupiedBytes = stats.occupiedBytes;
        snapshot.outbound.limitBytes = stats.limitBytes;
        snapshot.outbound.peerQueueCount = stats.peerQueueCount;
    }
    if (inbound_) {
        const QueueStatistics stats = inbound_->statistics();
        snapshot.inbound.itemCount = stats.itemCount;
        snapshot.inbound.occupiedBytes = stats.occupiedBytes;
        snapshot.inbound.limitBytes = stats.limitBytes;
        snapshot.inbound.peerQueueCount = stats.peerQueueCount;
    }

    snapshot.packetsDropped = outboundDrops_.total() + inboundDrops_.total();

    snapshot.state = state_;
    snapshot.phase = phase_;
    snapshot.clientVersion = PHPVPN_CLIENT_VERSION;
    snapshot.serviceUptimeSeconds = (clock_.monotonicMs() - startedAtMs_) / 1000;
    snapshot.lastHealthVerified = lastHealthVerified_;
    snapshot.lastInfoVerified = lastInfoVerified_;
    snapshot.lastDataVerified = lastDataVerified_;
    snapshot.lastRecoveryAtUtc = lastRecoveryAtUtc_;
    snapshot.recoveryCount = recoveryCount_;
    snapshot.tunnelReady = tunnelReady_;
    snapshot.counters = counters_.snapshot();
    snapshot.batchesRejected = batchesRejected_;
    snapshot.responsesRejected = responsesRejected_;
    snapshot.peakResponseCiphertextBytes = peakResponseCiphertextBytes_;
    snapshot.lastError = lastError_;
    return snapshot;
}

}  // namespace phpvpn
