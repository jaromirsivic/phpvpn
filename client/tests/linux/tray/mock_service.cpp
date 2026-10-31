// DEVELOPMENT HARNESS ONLY, never installed or packaged.
//
// Scripted phpvpn service for exercising the GTK tray without root, a relay or a
// TUN device. It serves valid IPC v1 envelopes (core ipc_protocol/wizard_ipc
// builders) on a private Unix socket owned by the calling user:
//
//   phpvpn-tray-mock-service --socket /run/user/1000/phpvpn-mock/control.sock
//       [--result success|endpoint|http|secret|access|contracts|tunnel|timeout]
//       [--step-ms 700] [--initial disconnected|connected]
//
// and the tray is pointed at it with the test-only environment variables
//   PHPVPN_TRAY_TEST_SOCKET=<socket> PHPVPN_TRAY_TEST_SERVER_UID=$(id -u)
//
// Scenario: connect walks disconnected -> connecting (health, info, access-key-check,
// communication-contracts-check, starting-tunnel) one checkpoint per step and ends
// connected/running or in the selected error. disconnect passes through stopping.
// Handled commands: get-status, connect, disconnect, get-config (synthetic default
// document, no envelope), dismiss-connect-diagnostic, get-config-state and
// get-config-document. Everything else is answered ipc_unknown_command.
//
// Lines on stdin change the scenario while running: "connect", "disconnect",
// "result <name>", "recover", "quit". EOF on stdin keeps serving until SIGINT/SIGTERM.
// Every handled request is logged to stdout as "request <command>".
//
// Area mocks in their own files register through mock_extension.h (extra
// commands, status adjustments, work classes, stdin lines and options), for
// example mock_server_info.cpp (get-server-info, large counters).
#include <signal.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>

#include "core/config/config_defaults.h"
#include "core/crypto/crypto_provider.h"
#include "core/ipc/ipc_protocol.h"
#include "core/ipc/wizard_ipc.h"
#include "core/protocol/json.h"
#include "platform/iface/transport.h"
#include "platform/linux/ipc_unix.h"
#include "mock_extension.h"

namespace {
using namespace phpvpn;
using Clock = std::chrono::steady_clock;

struct Failure { const char* name; const char* code; const char* description; int failedCheck; };
constexpr Failure kFailures[]{
    {"endpoint", "endpoint_unavailable_or_invalid", "The server endpoint is not reachable or not valid.", 0},
    {"secret", "server_verification_failed", "The server could not be verified with the configured SERVER_SECRET.", 1},
    {"access", "access_key_rejected", "The server rejected the configured ACCESS_KEY.", 2},
    {"contracts", "communication_contracts_verification_failed", "Communication contracts could not be verified. Check server compatibility.", 3},
    {"tunnel", "tunnel_start_failed", "The local tunnel could not be started.", 4},
    {"timeout", "communication_timeout", "The server did not respond in time. Verification could not be completed.", 1},
    {"version", "server_version_incompatible", "The server uses a different major version than this client.", 0},
    // endpoint failure that also carries the bounded initial-Connect HTTP diagnostic
    {"http", "endpoint_unavailable_or_invalid", "The server endpoint is not reachable or not valid.", 0},
};

HttpDiagnostic syntheticDiagnostic() {
    auto message = [](std::string text) {
        BoundedHttpMessage bounded;
        bounded.originalSizeBytes = text.size();
        bounded.prefix = std::move(text);
        return bounded;
    };
    HttpDiagnostic diagnostic;
    diagnostic.action = HttpAction::health;
    diagnostic.responseComplete = true;
    diagnostic.requestHeadersSource = RequestHeadersSource::winhttp;
    diagnostic.responseHeadersSource = ResponseHeadersSource::winhttp;
    diagnostic.request = message("POST /phpvpn.php HTTP/1.1\r\nHost: 127.0.0.1\r\nContent-Length: 0\r\n\r\n");
    diagnostic.response = message("HTTP/1.1 503 Service Unavailable\r\nContent-Length: 0\r\n\r\n");
    return diagnostic;
}

std::string isoNow() {
    const auto now = std::chrono::system_clock::now();
    const auto seconds = std::chrono::system_clock::to_time_t(now);
    const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000;
    std::tm utc{};
    gmtime_r(&seconds, &utc);
    char text[96];
    std::snprintf(text, sizeof(text), "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ", utc.tm_year + 1900, utc.tm_mon + 1,
                  utc.tm_mday, utc.tm_hour, utc.tm_min, utc.tm_sec, static_cast<int>(millis));
    return text;
}
std::string uuid() {
    auto crypto = createPlatformCryptoProvider();
    if (!crypto) return "3f2a7c1e-9b44-4b0e-8d2f-5c6a1e7b9d03";
    auto id = crypto.value()->randomUuidV4();
    return id ? id.value() : "3f2a7c1e-9b44-4b0e-8d2f-5c6a1e7b9d03";
}

class MockService {
public:
    std::chrono::milliseconds step{700};

    void setResult(std::string value) {
        std::lock_guard lock(mutex_);
        result = std::move(value);
    }
    void connectNow() {
        std::lock_guard lock(mutex_);
        if (active_ && !stopping_ && !failed()) return;  // idempotent global Connect
        ++epoch_;
        attemptId_ = uuid();
        dismissed_ = false;
        active_ = true;
        stopping_ = false;
        started_ = Clock::now();
        startedAtUtc_.clear();
        changedAt_ = isoNow();
    }
    void disconnectNow() {
        std::lock_guard lock(mutex_);
        if (!active_ && !failed()) { return; }
        stopping_ = true;
        stoppingSince_ = Clock::now();
        changedAt_ = isoNow();
    }
    void connectedImmediately() {
        connectNow();
        std::lock_guard lock(mutex_);
        started_ = Clock::now() - step * 6;
    }
    void recover() {
        std::lock_guard lock(mutex_);
        recovering_ = !recovering_;
        changedAt_ = isoNow();
    }

    std::string handle(std::string_view request) {
        if (auto ipc = IpcProtocol::parseRequest(request)) return handleIpc(*ipc);
        if (auto wizard = WizardIpc::parseRequest(request)) return handleWizard(*wizard);
        log("malformed");
        return IpcProtocol::buildError("00000000-0000-4000-8000-000000000000", Error(ErrorCode::ipc_malformed));
    }

private:
    std::string result = "success";
    bool failed() const { return result != "success" && failure() && progress() > failure()->failedCheck; }
    const Failure* failure() const {
        for (const auto& item : kFailures) if (result == item.name) return &item;
        return nullptr;
    }
    // Number of completed steps since Connect: 0..4 are the checks, 5 the tunnel.
    int progress() const {
        return static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - started_) / step);
    }
    void log(const std::string& command) {
        std::lock_guard lock(logMutex_);
        std::cout << "request " << command << std::endl;
    }

    StatusSnapshot snapshotLocked() {
        StatusSnapshot s;
        s.clientVersion = "1.0.0-mock";
        s.stateChangedAtUtc = changedAt_;
        s.connectionEpoch = epoch_;
        if (epoch_) s.connectAttemptId = attemptId_;
        if (stopping_ && Clock::now() - stoppingSince_ >= step) {
            active_ = stopping_ = recovering_ = false;
            startedAtUtc_.clear();
            changedAt_ = isoNow();
            s.stateChangedAtUtc = changedAt_;
        }
        if (!active_) {
            s.state = ConnectionState::disconnected;
            s.tunnelReady = Tristate::no;
            return s;
        }
        const int done = progress();
        const auto* fail = result == "success" ? nullptr : failure();
        const int limit = fail ? fail->failedCheck : 5;
        auto check = [&](int index) {
            if (index < std::min(done, limit)) return CheckpointResult::success;
            if (fail && index == limit && done > limit && index < 4) return CheckpointResult::failed;
            return CheckpointResult::pending;
        };
        s.serverUrlCheck = check(0);
        s.serverSecretCheck = check(1);
        s.accessKeyCheck = check(2);
        s.communicationContractsCheck = check(3);
        s.serverReachable = done > 0 ? Tristate::yes : Tristate::unknown;
        s.serverVerified = done > 1 ? Tristate::yes : Tristate::unknown;
        if (stopping_) {
            s.state = startedAtUtc_.empty() ? ConnectionState::connecting : ConnectionState::connected;
            s.phase = ConnectionPhase::stopping;
            s.tunnelReady = Tristate::no;
            if (!startedAtUtc_.empty()) s.connectionStartedAtUtc = startedAtUtc_;
            return s;
        }
        if (fail && done > limit) {
            s.state = ConnectionState::error;
            s.phase = ConnectionPhase::idle;
            s.tunnelReady = Tristate::no;
            s.lastError = SafeError{fail->code, fail->description, false};
            if (std::strcmp(fail->name, "http") == 0 && !dismissed_) s.httpDiagnostic = syntheticDiagnostic();
            return s;
        }
        if (done >= 5) {
            if (startedAtUtc_.empty()) startedAtUtc_ = isoNow();
            s.connectionStartedAtUtc = startedAtUtc_;
            s.state = recovering_ ? ConnectionState::recovering : ConnectionState::connected;
            s.phase = recovering_ ? ConnectionPhase::health : ConnectionPhase::running;
            s.tunnelReady = recovering_ ? Tristate::no : Tristate::yes;
            s.counters.dataPollsTotal = 12;
            s.counters.dataPollsEmpty = 9;
            s.counters.dataPollsWithPayload = 3;
            return s;
        }
        constexpr ConnectionPhase phases[]{ConnectionPhase::health, ConnectionPhase::info, ConnectionPhase::access_key_check,
                                           ConnectionPhase::communication_contracts_check, ConnectionPhase::starting_tunnel};
        s.state = ConnectionState::connecting;
        s.phase = phases[std::min(done, 4)];
        s.tunnelReady = Tristate::unknown;
        return s;
    }

    std::string handleIpc(const IpcRequest& request) {
        log(ipcCommandName(request.command));
        for (const auto& extension : mock::mockExtensions())
            if (extension.ipc)
                if (auto response = extension.ipc(request)) return std::move(*response);
        switch (request.command) {
            case IpcCommand::connect: connectNow(); break;
            case IpcCommand::disconnect: disconnectNow(); break;
            case IpcCommand::get_status: break;
            case IpcCommand::get_config: return makeDefaultEditorDocument();
            case IpcCommand::dismiss_connect_diagnostic: {
                std::lock_guard lock(mutex_);
                if (request.connectionEpoch == epoch_ && request.connectAttemptId == attemptId_) dismissed_ = true;
                return IpcProtocol::buildAcknowledgement(request.requestId);
            }
            default: return IpcProtocol::buildError(request.requestId, Error(ErrorCode::ipc_unknown_command));
        }
        std::lock_guard lock(mutex_);
        auto snapshot = snapshotLocked();
        for (const auto& extension : mock::mockExtensions())
            if (extension.status) extension.status(snapshot);
        return IpcProtocol::buildStatusResponse(request.requestId, snapshot);
    }

    std::string handleWizard(const WizardRequest& request) {
        log(WizardIpc::commandName(request.command));
        using J = JsonValue;
        std::lock_guard lock(mutex_);
        const auto snapshot = snapshotLocked();
        for (const auto& handler : mock::mockWizardHandlers())
            if (auto response = handler(request, snapshot)) return std::move(*response);
        if (request.command == WizardCommand::get_config_state) {
            return WizardIpc::buildResult(request.requestId, J::makeObject({
                {"availability", J::makeString("valid")}, {"reasonCode", J::makeNull()},
                {"configPath", J::makeString("/etc/phpvpn/config.json")}, {"revisionToken", J::makeString("mock-revision-1")},
                {"serviceInstanceId", J::makeString(instanceId_)},
                {"connectionEpoch", J::makeUnsignedInteger(snapshot.connectionEpoch)},
                {"vpnState", J::makeString(connectionStateName(snapshot.state))},
                {"vpnPhase", J::makeString(connectionPhaseName(snapshot.phase))}}));
        }
        if (request.command == WizardCommand::get_config_document) {
            return WizardIpc::buildResult(request.requestId, J::makeObject({
                {"configPath", J::makeString("/etc/phpvpn/config.json")}, {"availability", J::makeString("valid")},
                {"reasonCode", J::makeNull()}, {"document", J::makeString(makeDefaultEditorDocument())},
                {"revisionToken", J::makeString("mock-revision-1")}}));
        }
        return WizardIpc::buildError(request.requestId, WizardIpc::safeError(Error(ErrorCode::ipc_unknown_command)));
    }

    std::mutex mutex_, logMutex_;
    std::uint64_t epoch_ = 0;
    std::string attemptId_, changedAt_ = isoNow(), startedAtUtc_, instanceId_ = uuid();
    bool active_ = false, stopping_ = false, recovering_ = false, dismissed_ = false;
    Clock::time_point started_{}, stoppingSince_{};
};

int usage() {
    std::fputs("usage: phpvpn-tray-mock-service --socket <absolute path> [--result success|endpoint|http|secret|access|"
               "contracts|tunnel|timeout] [--step-ms <n>] [--initial disconnected|connected] [area options of mock_*.cpp]\n",
               stderr);
    return 2;
}
}  // namespace

int main(int argc, char** argv) {
    std::string socketPath, initial = "disconnected";
    MockService service;
    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        if (i + 1 >= argc) return usage();
        const std::string value = argv[++i];
        if (argument == "--socket") socketPath = value;
        else if (argument == "--result") service.setResult(value);
        else if (argument == "--step-ms") service.step = std::chrono::milliseconds(std::max(50, std::atoi(value.c_str())));
        else if (argument == "--initial") initial = value;
        else if (!std::any_of(mock::mockExtensions().begin(), mock::mockExtensions().end(),
                              [&](const auto& e) { return e.option && e.option(argument, value); }))
            return usage();
    }
    if (socketPath.empty() || socketPath.front() != '/') return usage();
    const auto directory = socketPath.substr(0, socketPath.rfind('/'));
    if (!directory.empty() && mkdir(directory.c_str(), 0700) != 0 && errno != EEXIST) {
        std::perror("mkdir");
        return 1;
    }
    if (initial == "connected") service.connectedImmediately();

    sigset_t signals;
    sigemptyset(&signals);
    sigaddset(&signals, SIGINT);
    sigaddset(&signals, SIGTERM);
    pthread_sigmask(SIG_BLOCK, &signals, nullptr);

    IpcSocketServerOptions options;
    options.socketPath = socketPath;
    options.expectedOwnerUid = geteuid();
    options.classify = [](std::string_view request) {
        for (const auto& extension : mock::mockExtensions())
            if (extension.classify)
                if (auto work = extension.classify(request)) return *work;
        return IpcWorkClass::shortControl;
    };
    // The correlated busy responses of the service (service_runtime.cpp).
    options.busyResponse = [](std::string_view request) {
        if (auto wizard = WizardIpc::parseRequest(request))
            return WizardIpc::buildError(wizard->requestId, WizardIpc::safeError(Error(ErrorCode::busy)));
        auto parsed = IpcProtocol::parseRequest(request);
        return IpcProtocol::buildError(parsed ? parsed->requestId : std::string{}, Error(ErrorCode::busy));
    };
    IpcSocketServer server([&](std::string_view request) { return service.handle(request); }, options);
    if (auto started = server.start(); !started) {
        std::fprintf(stderr, "mock service could not listen on %s: %s\n", socketPath.c_str(), started.error().name());
        return 1;
    }
    std::cout << "listening " << socketPath << std::endl;

    std::thread input([&] {
        std::string line;
        while (std::getline(std::cin, line)) {
            if (line == "connect") service.connectNow();
            else if (line == "disconnect") service.disconnectNow();
            else if (line.rfind("result ", 0) == 0) service.setResult(line.substr(7));
            else if (line == "recover") service.recover();
            else if (line == "quit") { kill(getpid(), SIGTERM); return; }
            else for (const auto& extension : mock::mockExtensions())
                if (extension.control && extension.control(line)) break;
        }
    });
    input.detach();
    int received = 0;
    sigwait(&signals, &received);
    server.stop();
    std::cout << "stopped" << std::endl;
    return 0;
}
