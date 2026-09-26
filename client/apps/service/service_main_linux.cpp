// Entry point of the phpvpn Linux service, plan-v001 section 4.10.
//
// `--systemd` (the default when NOTIFY_SOCKET is set) runs under the unit
// phpvpn.service with Type=notify: READY=1 once the control socket listens,
// STATUS= with the connection state and STOPPING=1 on shutdown, logging to
// journald through stderr. `--foreground` (alias `--console`) runs the same
// runtime in a terminal with plain log lines on stdout until Ctrl+C.
// `--verify-tunnel` checks the TUN device without creating an interface.
//
// Registration is the job of the package, as "installation is not an IPC
// operation" on Windows, so there is no --install or --uninstall here.
//
// Hidden test options --config-dir, --state-dir, --runtime-dir and
// --adapter-name exist only for isolated end-to-end instances in private
// network namespaces. The production unit never passes them.

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/signalfd.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <unistd.h>

#include <linux/if_tun.h>
#include <systemd/sd-daemon.h>

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
#include "platform/linux/config_files_linux.h"
#include "platform/linux/fd.h"
#include "platform/linux/ipc_unix.h"
#include "platform/linux/paths_linux.h"
#include "service_runtime.h"

namespace {

using namespace phpvpn;

constexpr const char* kComponent = "service";
// The status line is refreshed at this interval while the service runs.
constexpr int kStatusPollMs = 1000;

enum class Mode { systemd, foreground, verifyTunnel, help };

struct Options {
    Mode mode = Mode::foreground;
    LinuxDirectoryOverrides overrides;
    std::wstring adapterName;
};

void printUsage(std::FILE* stream) {
    std::fputs(
        "usage: phpvpn-service [--systemd | --foreground | --console | --verify-tunnel]\n"
        "\n"
        "  --systemd        run under systemd (phpvpn.service, Type=notify), logging to\n"
        "                   the journal; the default when NOTIFY_SOCKET is set\n"
        "  --foreground     run in the foreground with log lines on stdout, stop with\n"
        "                   Ctrl+C; the default otherwise\n"
        "  --console        alias of --foreground\n"
        "  --verify-tunnel  check the TUN device without creating an interface\n",
        stream);
}

// Returns the options, or nullopt after printing why the command line is invalid.
std::optional<Options> parseArguments(int argc, char** argv) {
    Options options;
    const char* notifySocket = std::getenv("NOTIFY_SOCKET");
    options.mode = notifySocket != nullptr && *notifySocket != '\0' ? Mode::systemd : Mode::foreground;
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
        if (argument == "--systemd") {
            accepted = setMode(Mode::systemd);
        } else if (argument == "--foreground" || argument == "--console") {
            accepted = setMode(Mode::foreground);
        } else if (argument == "--verify-tunnel") {
            accepted = setMode(Mode::verifyTunnel);
        } else if (argument == "--help" || argument == "-h") {
            accepted = setMode(Mode::help);
        } else if (argument == "--config-dir" || argument == "--state-dir" ||
                   argument == "--runtime-dir" || argument == "--adapter-name") {
            if (index + 1 >= argc || argv[index + 1][0] == '\0') {
                std::fprintf(stderr, "%s needs a value\n", argv[index]);
                return std::nullopt;
            }
            const std::string value = argv[++index];
            if (argument == "--config-dir") options.overrides.configDirectory = value;
            else if (argument == "--state-dir") options.overrides.stateDirectory = value;
            else if (argument == "--runtime-dir") options.overrides.runtimeDirectory = value;
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

std::string featureNames(unsigned int features) {
    struct Flag { unsigned int bit; const char* name; };
    static constexpr Flag kFlags[] = {
        {IFF_TUN, "IFF_TUN"},           {IFF_TAP, "IFF_TAP"},
        {IFF_NO_PI, "IFF_NO_PI"},       {IFF_ONE_QUEUE, "IFF_ONE_QUEUE"},
        {IFF_VNET_HDR, "IFF_VNET_HDR"}, {IFF_TUN_EXCL, "IFF_TUN_EXCL"},
        {IFF_MULTI_QUEUE, "IFF_MULTI_QUEUE"}, {IFF_NAPI, "IFF_NAPI"},
        {IFF_NAPI_FRAGS, "IFF_NAPI_FRAGS"},
    };
    std::string names;
    for (const auto& flag : kFlags) {
        if ((features & flag.bit) == 0) continue;
        if (!names.empty()) names += ' ';
        names += flag.name;
    }
    return names;
}

// The Linux counterpart of the Windows allowlist check: the TUN device node is
// the kernel device and offers IFF_TUN|IFF_NO_PI. No interface is created and
// no network setting is touched, so it also runs without root.
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
    unsigned int features = 0;
    UniqueFd device(::open("/dev/net/tun", O_RDWR | O_CLOEXEC));
    if (!device || ::ioctl(device.get(), TUNGETFEATURES, &features) != 0) {
        std::fputs("the TUN features could not be queried\n", stderr);
        return 1;
    }
    utsname system{};
    const char* release = ::uname(&system) == 0 ? system.release : "unknown";
    std::printf("the TUN device is usable, features 0x%04x (%s)\n", features, featureNames(features).c_str());
    std::printf("kernel release %s\n", release);
    return 0;
}

// journald stamps every record itself, so the ISO timestamp of the formatted
// line is dropped. Level tag, component and message stay as in the log file.
LogSink journaldSinkWithoutTimestamp() {
    return [](LogLevel level, const std::string& line) {
        std::string_view text = line;
        const auto space = text.find(' ');
        if (space != std::string_view::npos) text.remove_prefix(space + 1);
        const std::string record = journaldLine(level, text);
        const ssize_t written = ::write(STDERR_FILENO, record.data(), record.size());
        (void)written;
    };
}

LogSink stdoutSink() {
    return [](LogLevel, const std::string& line) {
        std::string record = line;
        record.push_back('\n');
        std::fwrite(record.data(), 1, record.size(), stdout);
        std::fflush(stdout);
    };
}

void notify(bool systemd, const std::string& state) {
    if (systemd) (void)sd_notify(0, state.c_str());
}

std::string statusLine(const ServiceRuntime& runtime) {
    const auto status = runtime.status();
    if (!status) return "STATUS=starting";
    std::string line = "STATUS=VPN ";
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
Result<UniqueFd> acquireServiceLock(const LinuxDirectories& directories) {
    const std::string path = serviceLockPath(directories).string();
    UniqueFd lock(::open(path.c_str(), O_RDWR | O_CREAT | O_NOFOLLOW | O_CLOEXEC, 0600));
    if (!lock) return Error(ErrorCode::internal, "the service lock could not be opened");
    struct stat info{};
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

ServicePlatformHost linuxPlatformHost(const LinuxDirectories& directories, const std::wstring& adapterName) {
    const TunnelSessionSettings defaults;
    ServicePlatformHost host;
    host.prepareStorage = [directories]() -> Result<ServiceStorage> {
        ServiceStorage storage;
        storage.configFile = configFilePath(directories);
        storage.journalFile = journalFilePath(directories);
        // Counterpart of the Windows ACL repair: root owned 0700 directories.
        auto secured = secureServiceDirectory(directories.config, kLinuxConfigDirectoryMode);
        if (secured) secured = secureServiceDirectory(directories.state, kLinuxStateDirectoryMode);
        if (!secured) storage.storageError = secured.error();
        if (!storage.storageError) {
            auto backend = createLinuxConfigFiles(storage.configFile);
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

// Reports a failure before the runtime runs: journal or stderr, and the unit
// status line under systemd.
int failStartup(bool systemd, const std::string& message) {
    PHPVPN_LOG_CRITICAL(kComponent, message);
    if (!systemd) {
        std::fputs(message.c_str(), stderr);
        std::fputc('\n', stderr);
    }
    notify(systemd, "STATUS=start failed: " + message);
    return 1;
}

int runService(const Options& options) {
    const bool systemd = options.mode == Mode::systemd;
    Logger::instance().setSecondarySink(systemd ? journaldSinkWithoutTimestamp() : stdoutSink());

    // SIGTERM and SIGINT arrive through a signalfd. They are blocked before the
    // first thread exists, so every worker inherits the mask. SIGHUP has no
    // reload meaning and is ignored like SIGPIPE; sockets use MSG_NOSIGNAL.
    ::signal(SIGHUP, SIG_IGN);
    ::signal(SIGPIPE, SIG_IGN);
    sigset_t signals;
    sigemptyset(&signals);
    sigaddset(&signals, SIGTERM);
    sigaddset(&signals, SIGINT);
    if (::pthread_sigmask(SIG_BLOCK, &signals, nullptr) != 0) return failStartup(systemd, "the signal mask could not be set");
    UniqueFd signalFd(::signalfd(-1, &signals, SFD_CLOEXEC | SFD_NONBLOCK));
    if (!signalFd) return failStartup(systemd, "the signal descriptor could not be created");

    if (::geteuid() != 0) return failStartup(systemd, "the phpvpn service must run as root");

    auto directories = resolveLinuxDirectories(options.overrides);
    if (!directories) return failStartup(systemd, directories.error().message());
    // systemd provides the runtime directory (RuntimeDirectory=); a foreground
    // run creates it. Either way it must be root owned 0755.
    auto runtimeReady = secureServiceDirectory(directories->runtime, kLinuxRuntimeDirectoryMode);
    if (!runtimeReady) return failStartup(systemd, runtimeReady.error().message());
    auto lock = acquireServiceLock(*directories);
    if (!lock) return failStartup(systemd, lock.error().message());

    ServiceRuntime runtime;
    auto started = runtime.start(linuxPlatformHost(*directories, options.adapterName));
    if (!started.ok()) {
        const int code = failStartup(systemd, started.error().message());
        runtime.stop();
        return code;
    }

    std::string status = statusLine(runtime);
    notify(systemd, "READY=1\n" + status);
    if (!systemd) std::fputs("phpvpn service is running in the foreground, press Ctrl+C to stop\n", stdout);
    runtime.requestStartupConnect();

    for (;;) {
        pollfd wait{signalFd.get(), POLLIN, 0};
        const int ready = ::poll(&wait, 1, kStatusPollMs);
        if (ready < 0 && errno != EINTR) {
            PHPVPN_LOG_ERROR(kComponent, "the signal wait failed");
            break;
        }
        if (ready > 0) {
            signalfd_siginfo info{};
            const ssize_t count = ::read(signalFd.get(), &info, sizeof(info));
            if (count == static_cast<ssize_t>(sizeof(info)) &&
                (info.ssi_signo == SIGTERM || info.ssi_signo == SIGINT)) {
                PHPVPN_LOG_INFO(kComponent, info.ssi_signo == SIGTERM ? "stop requested (SIGTERM)" : "stop requested (SIGINT)");
                break;
            }
            continue;
        }
        std::string current = statusLine(runtime);
        if (current != status) {
            status = std::move(current);
            notify(systemd, status);
        }
    }

    notify(systemd, "STOPPING=1\nSTATUS=stopping");
    runtime.stop();
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
        case Mode::systemd:
        case Mode::foreground:
            break;
    }
    return runService(*options);
}
