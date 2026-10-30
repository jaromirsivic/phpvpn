// Entry point of the phpvpn macOS service, plan-macos-v001 step 6.
//
// `--launchd` (the default when the parent is launchd or XPC_SERVICE_NAME is
// set) runs as the system daemon com.pilandrome.phpvpn. launchd has no
// Type=notify, STATUS= or RuntimeDirectory=, so the daemon creates its own
// runtime and log directories, writes the rotating file log
// /private/var/log/phpvpn/phpvpn-service.log (8 MiB x 5, as on Windows) and logs
// every change of the connection state at info level. `--foreground` (alias
// `--console`) runs the same runtime in a terminal with the log lines also on
// stdout until Ctrl+C. `--verify-tunnel` checks the utun kernel control without
// creating an interface.
//
// Registration is the job of the package (launchctl bootstrap in the
// postinstall script), as "installation is not an IPC operation" on Windows,
// so there is no --install or --uninstall here.
//
// Hidden test options --config-dir, --state-dir, --runtime-dir, --log-dir and
// --adapter-name exist only for isolated end-to-end instances. The production
// property list never passes them.

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <sys/event.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <time.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "core/base/logger.h"
#include "core/base/utf8.h"
#include "core/runtime/tunnel_session.h"
#include "platform/iface/tunnel.h"
#include "platform/posix/config_files_posix.h"
#include "platform/posix/fd.h"
#include "platform/posix/ipc_unix.h"
#include "platform/posix/paths_posix.h"
#include "service_runtime.h"

namespace {

using namespace phpvpn;

constexpr const char* kComponent = "service";
// The connection state is checked for changes at this interval.
constexpr int kStatusPollMs = 1000;

enum class Mode { launchd, foreground, verifyTunnel, help };

struct Options {
    Mode mode = Mode::foreground;
    PosixDirectoryOverrides overrides;
    std::wstring adapterName;
};

void printUsage(std::FILE* stream) {
    std::fputs(
        "usage: phpvpn-service [--launchd | --foreground | --console | --verify-tunnel]\n"
        "\n"
        "  --launchd        run as the launchd daemon com.pilandrome.phpvpn, logging to\n"
        "                   /private/var/log/phpvpn; the default when started by launchd\n"
        "  --foreground     run in the foreground with log lines on stdout, stop with\n"
        "                   Ctrl+C; the default otherwise\n"
        "  --console        alias of --foreground\n"
        "  --verify-tunnel  check the utun kernel control without creating an interface\n",
        stream);
}

// launchd starts daemons as its own children and exports XPC_SERVICE_NAME with
// the job label. Terminal sessions often carry XPC_SERVICE_NAME=0.
bool startedByLaunchd() {
    if (::getppid() == 1) return true;
    const char* serviceName = std::getenv("XPC_SERVICE_NAME");
    return serviceName != nullptr && *serviceName != '\0' && std::string_view(serviceName) != "0";
}

// Returns the options, or nullopt after printing why the command line is invalid.
std::optional<Options> parseArguments(int argc, char** argv) {
    Options options;
    options.mode = startedByLaunchd() ? Mode::launchd : Mode::foreground;
    bool modeGiven = false;
    auto setMode = [&](Mode mode) {
        if (modeGiven) return false;
        modeGiven = true;
        options.mode = mode;
        return true;
    };
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument = argv[index];
        bool accepted = true;
        if (argument == "--launchd") {
            accepted = setMode(Mode::launchd);
        } else if (argument == "--foreground" || argument == "--console") {
            accepted = setMode(Mode::foreground);
        } else if (argument == "--verify-tunnel") {
            accepted = setMode(Mode::verifyTunnel);
        } else if (argument == "--help" || argument == "-h") {
            accepted = setMode(Mode::help);
        } else if (argument == "--config-dir" || argument == "--state-dir" || argument == "--runtime-dir" ||
                   argument == "--log-dir" || argument == "--adapter-name") {
            if (index + 1 >= argc || argv[index + 1][0] == '\0') {
                std::fprintf(stderr, "%s needs a value\n", argv[index]);
                return std::nullopt;
            }
            const std::string value = argv[++index];
            if (argument == "--config-dir") options.overrides.configDirectory = value;
            else if (argument == "--state-dir") options.overrides.stateDirectory = value;
            else if (argument == "--runtime-dir") options.overrides.runtimeDirectory = value;
            else if (argument == "--log-dir") options.overrides.logDirectory = value;
            else {
                auto wide = fromUtf8(value);
                if (!wide) {
                    std::fputs("--adapter-name is not valid UTF-8\n", stderr);
                    return std::nullopt;
                }
                options.adapterName = std::move(wide).value();
            }
        } else {
            accepted = false;
        }
        if (!accepted) {
            printUsage(stderr);
            return std::nullopt;
        }
    }
    return options;
}

// The macOS counterpart of the Windows allowlist check: the utun kernel control
// com.apple.net.utun_control is resolved through a PF_SYSTEM socket, nothing is
// connected, so no interface is created and no network setting is touched. The
// control socket needs root; the driver error says so.
int verifyTunnel() {
    auto driver = createPlatformTunnelDriver();
    if (driver == nullptr) {
        std::fputs("the tunnel driver backend is unavailable\n", stderr);
        return 1;
    }
    auto loaded = driver->loadAndVerify(std::wstring());
    if (!loaded.ok()) {
        std::fputs(loaded.error().message().c_str(), stderr);
        std::fputc('\n', stderr);
        return 1;
    }
    struct utsname system {};
    const char* release = ::uname(&system) == 0 ? system.release : "unknown";
    std::puts("the utun kernel control is usable");
    std::printf("kernel release %s\n", release);
    return 0;
}

LogSink stdoutSink() {
    return [](LogLevel, const std::string& line) {
        std::string record = line;
        record.push_back('\n');
        std::fwrite(record.data(), 1, record.size(), stdout);
        std::fflush(stdout);
    };
}

// The launchd counterpart of the systemd STATUS= line, logged when it changes.
std::string statusLine(const ServiceRuntime& runtime) {
    const auto status = runtime.status();
    if (!status) return "starting";
    std::string line = "VPN ";
    line += connectionStateName(status->state);
    if (status->phase != ConnectionPhase::idle) {
        line += " (";
        line += connectionPhaseName(status->phase);
        line += ")";
    }
    return line;
}

// Singleton guard: an exclusive flock on <runtime>/service.lock, held until the
// process exits. The runtime directory was already verified root owned.
Result<UniqueFd> acquireServiceLock(const PosixDirectories& directories) {
    const std::string path = serviceLockPath(directories).string();
    UniqueFd lock(::open(path.c_str(), O_RDWR | O_CREAT | O_NOFOLLOW | O_CLOEXEC, 0600));
    if (!lock) return Error(ErrorCode::internal, "the service lock could not be opened");
    struct stat info {};
    if (::fstat(lock.get(), &info) != 0 || !S_ISREG(info.st_mode) || info.st_uid != 0 || info.st_nlink != 1)
        return Error(ErrorCode::config_directory_security_invalid, "the service lock is not a private file");
    if ((info.st_mode & 07777) != 0600 && ::fchmod(lock.get(), 0600) != 0)
        return Error(ErrorCode::internal, "the service lock mode could not be repaired");
    if (::flock(lock.get(), LOCK_EX | LOCK_NB) != 0) {
        if (errno == EWOULDBLOCK) return Error(ErrorCode::service_already_running, "another phpvpn service instance is running");
        return Error(ErrorCode::internal, "the service lock could not be taken");
    }
    return lock;
}

ServicePlatformHost macosPlatformHost(const PosixDirectories& directories, const std::wstring& adapterName) {
    const TunnelSessionSettings defaults;
    ServicePlatformHost host;
    host.prepareStorage = [directories]() -> Result<ServiceStorage> {
        ServiceStorage storage;
        storage.configFile = configFilePath(directories);
        storage.journalFile = journalFilePath(directories);
        // Counterpart of the Windows ACL repair: root owned 0700 directories.
        auto secured = secureServiceDirectory(directories.config, kPosixConfigDirectoryMode);
        if (secured) secured = secureServiceDirectory(directories.state, kPosixStateDirectoryMode);
        if (!secured) storage.storageError = secured.error();
        if (!storage.storageError) {
            auto backend = createPosixConfigFiles(storage.configFile);
            if (backend) storage.files = std::move(backend).value();
            else storage.storageError = backend.error();
        }
        return storage;
    };
    host.installDirectory = []() -> Result<std::wstring> { return std::wstring(); };
    host.adapterName = adapterName.empty() ? defaults.adapterName : adapterName;
    host.adapterGuid = defaults.requestedGuid;
    const std::string socketPath = controlSocketPath(directories).string();
    host.createIpcServer = [socketPath](IpcHandler handler, IpcServerOptions shared) -> std::unique_ptr<IpcServer> {
        IpcSocketServerOptions options;
        static_cast<IpcServerOptions&>(options) = std::move(shared);
        options.socketPath = socketPath;
        options.expectedOwnerUid = 0;
        return std::make_unique<IpcSocketServer>(std::move(handler), std::move(options));
    };
    return host;
}

// Reports a failure before the runtime runs: the log (file once configured,
// stdout in the foreground) and stderr, which launchd appends to
// /private/var/log/phpvpn/launchd.log, so a failure before the file log exists is
// still visible.
int failStartup(const std::string& message) {
    PHPVPN_LOG_CRITICAL(kComponent, message);
    std::fputs(message.c_str(), stderr);
    std::fputc('\n', stderr);
    return 1;
}

// Registers SIGTERM and SIGINT with a new kqueue.
Result<UniqueFd> createSignalQueue() {
    UniqueFd queue(::kqueue());
    if (!queue) return Error(ErrorCode::internal, "the signal queue could not be created");
    // A kqueue is never inherited by fork; close-on-exec covers exec as well.
    if (::fcntl(queue.get(), F_SETFD, FD_CLOEXEC) != 0)
        return Error(ErrorCode::internal, "the signal queue could not be configured");
    struct kevent changes[2] {};
    const int stopSignals[2] = {SIGTERM, SIGINT};
    for (int index = 0; index < 2; ++index) {
        changes[index].ident = static_cast<uintptr_t>(stopSignals[index]);
        changes[index].filter = EVFILT_SIGNAL;
        changes[index].flags = EV_ADD | EV_ENABLE;
        changes[index].fflags = 0;
        changes[index].data = 0;
        changes[index].udata = nullptr;
    }
    if (::kevent(queue.get(), changes, 2, nullptr, 0, nullptr) != 0)
        return Error(ErrorCode::internal, "the stop signals could not be registered");
    return queue;
}

int runService(const Options& options) {
    const bool launchd = options.mode == Mode::launchd;
    // Under launchd stdout and stderr go to launchd.log; the log lines belong in
    // the rotating file only. The foreground run also shows them on stdout.
    if (!launchd) Logger::instance().setSecondarySink(stdoutSink());

    // SIGHUP has no reload meaning and is ignored like SIGPIPE; sockets use
    // SO_NOSIGPIPE. SIGTERM (launchctl bootout/kill) and SIGINT (Ctrl+C) are
    // observed through a kqueue EVFILT_SIGNAL filter. That filter only records
    // delivery attempts and has a lower precedence than the signal disposition:
    // with the default disposition the kernel would still terminate the process
    // before the loop below sees the event, and a blocked signal would stay
    // pending forever. SIG_IGN discards the signal itself while kqueue still
    // reports every attempt, also when a worker thread was the delivery target.
    // The filter is registered first, so no stop request is lost in between,
    // and the dispositions are set before the first thread exists.
    auto signalQueue = createSignalQueue();
    if (!signalQueue) return failStartup(signalQueue.error().message());
    ::signal(SIGHUP, SIG_IGN);
    ::signal(SIGPIPE, SIG_IGN);
    ::signal(SIGTERM, SIG_IGN);
    ::signal(SIGINT, SIG_IGN);

    if (::geteuid() != 0) return failStartup("the phpvpn service must run as root");

    auto directories = resolvePosixDirectories(options.overrides);
    if (!directories) return failStartup(directories.error().message());
    // launchd has no RuntimeDirectory=, the daemon creates the runtime and the
    // log directory itself. Both must be root owned 0755.
    auto runtimeReady = secureServiceDirectory(directories->runtime, kPosixRuntimeDirectoryMode);
    if (!runtimeReady) return failStartup(runtimeReady.error().message());
    auto logsReady = secureServiceDirectory(directories->logs, kPosixLogDirectoryMode);
    if (!logsReady) return failStartup(logsReady.error().message());
    // The rotating file log of client.md "Logovani a diagnostika", as on Windows
    // in both service and console mode.
    Logger::instance().configureFile(directories->logs, std::string(kServiceLogBaseName));
    auto lock = acquireServiceLock(*directories);
    if (!lock) return failStartup(lock.error().message());

    ServiceRuntime runtime;
    auto started = runtime.start(macosPlatformHost(*directories, options.adapterName));
    if (!started.ok()) {
        const int code = failStartup(started.error().message());
        runtime.stop();
        return code;
    }

    std::string status = statusLine(runtime);
    if (launchd) {
        PHPVPN_LOG_INFO(kComponent, "phpvpn service is running under launchd");
    } else {
        std::fputs("phpvpn service is running in the foreground, press Ctrl+C to stop\n", stdout);
        std::fflush(stdout);
    }
    PHPVPN_LOG_INFO(kComponent, "status: " + status);
    runtime.requestStartupConnect();

    struct timespec timeout {};
    timeout.tv_sec = kStatusPollMs / 1000;
    timeout.tv_nsec = static_cast<long>(kStatusPollMs % 1000) * 1000000L;
    for (;;) {
        struct kevent event {};
        const int ready = ::kevent(signalQueue->get(), nullptr, 0, &event, 1, &timeout);
        if (ready < 0 && errno != EINTR) {
            PHPVPN_LOG_ERROR(kComponent, "the signal wait failed");
            break;
        }
        if (ready > 0) {
            if (event.filter == EVFILT_SIGNAL &&
                (event.ident == static_cast<uintptr_t>(SIGTERM) || event.ident == static_cast<uintptr_t>(SIGINT))) {
                PHPVPN_LOG_INFO(kComponent, event.ident == static_cast<uintptr_t>(SIGTERM) ? "stop requested (SIGTERM)"
                                                                                          : "stop requested (SIGINT)");
                break;
            }
            continue;
        }
        if (ready < 0) continue;  // EINTR
        std::string current = statusLine(runtime);
        if (current != status) {
            status = std::move(current);
            PHPVPN_LOG_INFO(kComponent, "status: " + status);
        }
    }

    PHPVPN_LOG_INFO(kComponent, "status: stopping");
    runtime.stop();
    PHPVPN_LOG_INFO(kComponent, "phpvpn service stopped");
    Logger::instance().flush();
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    auto options = parseArguments(argc, argv);
    if (!options) return 2;
    switch (options->mode) {
        case Mode::help:
            printUsage(stdout);
            return 0;
        case Mode::verifyTunnel:
            return verifyTunnel();
        case Mode::launchd:
        case Mode::foreground:
            break;
    }
    return runService(*options);
}
