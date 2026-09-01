#include "core/runtime/vpn_engine.h"

#include <algorithm>
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

Result<std::string> readWholeFile(const std::filesystem::path& path) {
    std::error_code code;
    if (!std::filesystem::exists(path, code)) {
        return Error(ErrorCode::config_not_found, "the configuration file does not exist");
    }
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        return Error(ErrorCode::config_read_failed, "the configuration file could not be opened");
    }
    std::string content((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    if (!stream.eof() && stream.fail()) {
        return Error(ErrorCode::config_read_failed, "the configuration file could not be read");
    }
    return content;
}

Status writeWholeFile(const std::filesystem::path& path, std::string_view content) {
    std::error_code code;
    std::filesystem::create_directories(path.parent_path(), code);

    std::filesystem::path temporary = path;
    temporary += ".tmp";
    {
        std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
        if (!stream) {
            return Error(ErrorCode::config_write_failed, "the configuration could not be written");
        }
        stream.write(content.data(), static_cast<std::streamsize>(content.size()));
        stream.flush();
        if (!stream) {
            return Error(ErrorCode::config_write_failed, "the configuration could not be written");
        }
    }
    std::filesystem::rename(temporary, path, code);
    if (code) {
        std::filesystem::remove(temporary, code);
        return Error(ErrorCode::config_write_failed, "the configuration could not be replaced");
    }
    return Status::success();
}

}  // namespace

VpnEngine::VpnEngine(CryptoProvider& crypto,
                     Transport& transport,
                     Clock& clock,
                     TunnelDriver& tunnelDriver,
                     NetConfig& netConfig,
                     EnginePaths paths)
    : crypto_(crypto),
      transport_(transport),
      clock_(clock),
      tunnelDriver_(tunnelDriver),
      netConfig_(netConfig),
      paths_(std::move(paths)),
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
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        stateChangedAtUtc_ = Iso8601::format(clock_.systemUtcMs());
    }

    // A configuration that is missing at start is not an error; the user can
    // still store one over IPC before the first connect.
    auto loaded = loadStoredConfig();
    if (!loaded.ok() && loaded.error().code != ErrorCode::config_not_found) {
        PHPVPN_LOG_WARN(kComponent, loaded.error().message());
    }

    // Leftovers of an abnormally terminated run are removed before anything
    // else touches the network configuration.
    TunnelSessionSettings settings;
    settings.journalPath = paths_.journalFile;
    settings.installDirectory = paths_.installDirectory;
    auto recovered = tunnelSession_.recoverPreviousRun(settings);
    if (!recovered.ok()) {
        PHPVPN_LOG_ERROR(kComponent, recovered.error().message());
    }

    thread_ = std::thread([this] { runNetworkThread(); });
    return Status::success();
}

void VpnEngine::shutdown() {
    if (!running_.exchange(false)) {
        if (thread_.joinable()) {
            thread_.join();
        }
        return;
    }
    pushCommand(Command::stop);
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

Status VpnEngine::loadStoredConfig() {
    std::string content;
    PHPVPN_TRY_ASSIGN(content, readWholeFile(paths_.configFile));

    ConfigWarnings warnings;
    ClientConfigPtr config;
    PHPVPN_TRY_ASSIGN(config, parseAndValidateConfig(content, warnings));

    std::lock_guard<std::mutex> lock(configMutex_);
    storedConfig_ = std::move(config);
    storedWarnings_ = warnings;
    return Status::success();
}

Result<std::string> VpnEngine::storedConfigJson() const {
    std::lock_guard<std::mutex> lock(configMutex_);
    if (!storedConfig_) {
        return Error(ErrorCode::config_not_found, "no configuration is stored");
    }
    return storedConfig_->rawJson;
}

Status VpnEngine::setConfigJson(std::string_view json) {
    ConfigWarnings warnings;
    ClientConfigPtr config;
    PHPVPN_TRY_ASSIGN(config, parseAndValidateConfig(json, warnings));

    PHPVPN_TRY(writeWholeFile(paths_.configFile, json));

    std::lock_guard<std::mutex> lock(configMutex_);
    const std::uint64_t generation = storedConfig_ ? storedConfig_->generation + 1 : 1;
    auto stored = std::make_shared<ClientConfig>(*config);
    stored->generation = generation;
    storedConfig_ = std::move(stored);
    storedWarnings_ = warnings;

    PHPVPN_LOG_INFO(kComponent, "a new configuration was stored");
    return Status::success();
}

Result<std::string> VpnEngine::lastInfoPlaintext() const {
    std::lock_guard<std::mutex> lock(stateMutex_);
    if (lastInfoPlaintext_.empty()) {
        return Error(ErrorCode::state_conflict, "no verified server information is available");
    }
    return lastInfoPlaintext_;
}

Result<std::string> VpnEngine::fetchInfoOnce() {
    std::lock_guard<std::mutex> lock(infoMutex_);

    ClientConfigPtr config;
    {
        std::lock_guard<std::mutex> configLock(configMutex_);
        config = storedConfig_;
    }
    if (!config) {
        return Error(ErrorCode::config_missing, "no configuration is stored");
    }

    std::unique_ptr<Transport> transport;
    PHPVPN_TRY_ASSIGN(transport, createPlatformTransport());

    NetworkWorker worker(crypto_, *transport, clock_);
    PHPVPN_TRY(worker.configure(config));

    HealthResponse health;
    PHPVPN_TRY_ASSIGN(health, worker.performHealth());
    (void)health;

    InfoResponse info;
    PHPVPN_TRY_ASSIGN(info, worker.performInfo());
    return info.plaintext;
}

Status VpnEngine::connect() {
    {
        std::lock_guard<std::mutex> lock(configMutex_);
        if (!storedConfig_) {
            // A configuration may have appeared on disk since the service start.
            auto loaded = Status::success();
            {
                // The mutex is already held, so the reload is done inline.
                std::string content;
                auto read = readWholeFile(paths_.configFile);
                if (!read.ok()) {
                    return read.error();
                }
                content = std::move(read).value();

                ConfigWarnings warnings;
                auto parsed = parseAndValidateConfig(content, warnings);
                if (!parsed.ok()) {
                    return parsed.error();
                }
                storedConfig_ = std::move(parsed).value();
                storedWarnings_ = warnings;
            }
            (void)loaded;
        }
    }

    connectRequested_.store(true);
    pushCommand(Command::connect);
    return Status::success();
}

Status VpnEngine::disconnect() {
    connectRequested_.store(false);
    pushCommand(Command::disconnect);
    return Status::success();
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
    lastError_ = SafeError::from(error);
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

Status VpnEngine::runConnectSequence() {
    ClientConfigPtr config;
    {
        std::lock_guard<std::mutex> lock(configMutex_);
        config = storedConfig_;
    }
    if (!config) {
        return Error(ErrorCode::config_missing, "no configuration is stored");
    }

    {
        std::lock_guard<std::mutex> lock(configMutex_);
        activeConfig_ = config;
    }

    outbound_ = std::make_unique<DirectionQueues>(config->runtime.maxOutboundQueueBytes,
                                                  config->runtime.minPerPeerQueueBytes,
                                                  config->maxRemotePeers());
    inbound_ = std::make_unique<DirectionQueues>(config->runtime.maxInboundQueueBytes,
                                                 config->runtime.minPerPeerQueueBytes,
                                                 config->maxRemotePeers());

    PHPVPN_TRY(network_.configure(config));

    setState(ConnectionState::connecting, ConnectionPhase::health);
    auto health = network_.performHealth();
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        serverReachable_ = health.ok() ? Tristate::yes : Tristate::no;
        lastHealthVerified_ = health.ok() ? Tristate::yes : Tristate::no;
        serverVerified_ = health.ok() ? Tristate::yes : Tristate::no;
    }
    if (!health.ok()) {
        return health.error();
    }

    setPhase(ConnectionPhase::info);
    auto info = network_.performInfo();
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        lastInfoVerified_ = info.ok() ? Tristate::yes : Tristate::no;
        if (info.ok()) {
            lastInfoPlaintext_ = info.value().plaintext;
        }
    }
    if (!info.ok()) {
        return info.error();
    }

    setPhase(ConnectionPhase::access_key_check);
    auto accessKey = network_.verifyAccessKey(*inbound_, inboundDrops_);
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        lastDataVerified_ = accessKey.ok() ? Tristate::yes : Tristate::no;
    }
    if (!accessKey.ok()) {
        return accessKey.error();
    }

    setPhase(ConnectionPhase::starting_tunnel);
    TunnelSessionSettings settings;
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

    inputWorker_ = std::make_unique<TunnelInputWorker>(config, tunnelSession_.tunnel(), *outbound_,
                                                       outboundDrops_, tunnelCounters_);
    outputWorker_ = std::make_unique<TunnelOutputWorker>(tunnelSession_.tunnel(), *inbound_,
                                                         inboundDrops_, tunnelCounters_);
    inputWorker_->start();
    outputWorker_->start();

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
        lastError_ = SafeError::from(outcome.error());
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
            setState(ConnectionState::disconnected, ConnectionPhase::stopping);
            teardown(/*keepError=*/false);
            setState(ConnectionState::disconnected, ConnectionPhase::idle);
            backoffStep_ = 0;
            continue;
        }
        if (command == Command::connect && currentState != ConnectionState::connected) {
            backoffStep_ = 0;
            auto sequence = runConnectSequence();
            if (!sequence.ok()) {
                recordError(sequence.error());
                teardown(/*keepError=*/true);
                if (sequence.error().recoverable()) {
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
                Error error(ErrorCode::response_invalid);
                {
                    std::lock_guard<std::mutex> lock(stateMutex_);
                    if (lastError_.has_value()) {
                        error = Error(ErrorCode::response_invalid, lastError_->description);
                    }
                }
                PHPVPN_LOG_WARN(kComponent, "the data exchange failed, entering recovery");
                teardown(/*keepError=*/true);
                ++recoveryCount_;
                {
                    std::lock_guard<std::mutex> lock(stateMutex_);
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
                auto sequence = runConnectSequence();
                if (!sequence.ok()) {
                    recordError(sequence.error());
                    teardown(/*keepError=*/true);
                    if (sequence.error().recoverable()) {
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
    setState(ConnectionState::disconnected, ConnectionPhase::idle);
    running_.store(false);
}

StatusSnapshot VpnEngine::status() const {
    std::lock_guard<std::mutex> lock(stateMutex_);
    StatusSnapshot snapshot;
    snapshot.state = state_;
    snapshot.phase = phase_;
    snapshot.stateChangedAtUtc = stateChangedAtUtc_;
    snapshot.serverReachable = serverReachable_;
    snapshot.serverVerified = serverVerified_;
    snapshot.tunnelReady = tunnelReady_;
    snapshot.lastError = lastError_;
    snapshot.packetsReceived = packetsReceived_;
    snapshot.packetsSent = packetsSent_;
    snapshot.recoveryCount = recoveryCount_;
    snapshot.clientVersion = PHPVPN_CLIENT_VERSION;
    snapshot.ipcVersion = PHPVPN_IPC_VERSION;
    return snapshot;
}

DiagnosticsSnapshot VpnEngine::diagnostics() const {
    DiagnosticsSnapshot snapshot;

    {
        std::lock_guard<std::mutex> lock(configMutex_);
        snapshot.configurationValid = storedConfig_ != nullptr;
        snapshot.configurationWarnings = storedWarnings_.messages();
    }

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

    std::lock_guard<std::mutex> lock(stateMutex_);
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
    snapshot.packetsReceived = packetsReceived_;
    snapshot.packetsSent = packetsSent_;
    snapshot.batchesRejected = batchesRejected_;
    snapshot.responsesRejected = responsesRejected_;
    snapshot.peakResponseCiphertextBytes = peakResponseCiphertextBytes_;
    snapshot.lastError = lastError_;
    return snapshot;
}

}  // namespace phpvpn
