#include "android_host.h"

#include <unistd.h>

#include <mutex>
#include <utility>

#include "core/base/logger.h"
#include "platform/android/log_sink_android.h"
#include "platform/posix/config_files_posix.h"
#include "platform/posix/paths_posix.h"

namespace phpvpn {
namespace {

constexpr const char* kComponent = "service";

// Logger::configureFile cannot reopen its stream while one is open, so the
// file is configured once per process; nativeStop and nativeStart in the same
// process keep writing to the same file (the directory never changes there).
void configureLogFileOnce(const std::filesystem::path& logs) {
    static std::mutex mutex;
    static std::filesystem::path configured;
    std::lock_guard lock(mutex);
    if (!configured.empty()) {
        if (configured != logs) PHPVPN_LOG_WARN(kComponent, "the log file stays in the directory of the first start");
        return;
    }
    // Defaults of Logger::configureFile: 8 MiB per file and five history files,
    // the policy of client.md "Logování a diagnostika" on every platform.
    Logger::instance().configureFile(logs, kServiceLogBaseName);
    configured = logs;
}

Status secureLayout(const AndroidStorageLayout& layout, const std::filesystem::path& anchor) {
    const uid_t self = ::getuid();
    // secureServiceDirectory creates only the last component, so the parent
    // first. secureServiceDirectories is not used: it makes the runtime
    // directory 0755, Android keeps all four private (0700).
    PHPVPN_TRY(secureServiceDirectory(layout.root, kAndroidDirectoryMode, self, anchor));
    for (const auto* directory : {&layout.config, &layout.state, &layout.runtime, &layout.logs})
        PHPVPN_TRY(secureServiceDirectory(*directory, kAndroidDirectoryMode, self, anchor));
    return Status::success();
}

}  // namespace

AndroidStorageLayout androidStorageLayout(const std::filesystem::path& noBackupFilesDir) {
    AndroidStorageLayout layout;
    layout.root = noBackupFilesDir / kAndroidDataDirectoryName;
    layout.config = layout.root / "config";
    layout.state = layout.root / "state";
    layout.runtime = layout.root / "run";
    layout.logs = layout.root / "logs";
    return layout;
}

ServicePlatformHost androidPlatformHost(std::filesystem::path noBackupFilesDir,
                                        std::shared_ptr<std::shared_ptr<InProcessIpcEndpoint>> endpoint) {
    ServicePlatformHost host;
    host.prepareStorage = [anchor = std::move(noBackupFilesDir)]() -> Result<ServiceStorage> {
        const auto layout = androidStorageLayout(anchor);
        PosixDirectoryOverrides overrides;
        overrides.configDirectory = layout.config;
        overrides.stateDirectory = layout.state;
        overrides.runtimeDirectory = layout.runtime;
        overrides.logDirectory = layout.logs;
        auto directories = resolvePosixDirectories(overrides);

        ServiceStorage storage;
        if (!directories) {
            // A path the runtime cannot even name: still no abort, the config
            // store reports the error through IPC.
            storage.configFile = layout.config / "config.json";
            storage.journalFile = layout.state / "ownership.json";
            storage.storageError = directories.error();
            PHPVPN_LOG_ERROR(kComponent, std::string("storage: ") + storage.storageError->name());
            return storage;
        }
        storage.configFile = configFilePath(*directories);
        storage.journalFile = journalFilePath(*directories);
        auto secured = secureLayout(layout, anchor);
        if (!secured) {
            storage.storageError = secured.error();
        } else {
            // Point 13 and the WP02 follow-up: hard links are refused by
            // SELinux in the application data, so the backup is an exchange.
            auto backend = createPosixConfigFiles(
                storage.configFile,
                PosixConfigFilePolicy{::getuid(), {}, anchor, PosixConfigBackupStrategy::exchange});
            if (backend) storage.files = std::move(backend).value();
            else storage.storageError = backend.error();
        }
        // The log directory is secured as part of the layout; without it only
        // logcat carries the records.
        if (!storage.storageError) configureLogFileOnce(layout.logs);
        if (storage.storageError) PHPVPN_LOG_ERROR(kComponent, std::string("storage: ") + storage.storageError->name());
        return storage;
    };
    host.installDirectory = []() -> Result<std::wstring> { return std::wstring(); };
    host.adapterName = kAndroidAdapterName;
    host.adapterGuid = kAndroidAdapterGuid;
    host.createIpcServer = [endpoint = std::move(endpoint)](IpcHandler handler,
                                                            IpcServerOptions options) -> std::unique_ptr<IpcServer> {
        auto created = std::make_shared<InProcessIpcEndpoint>(std::move(handler), std::move(options));
        if (endpoint) *endpoint = created;
        return std::make_unique<InProcessIpcServer>(std::move(created));
    };
    return host;
}

}  // namespace phpvpn
