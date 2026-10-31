// Well known locations of the POSIX installations (Linux and macOS), plan-v001
// section 4.8 and plan-macos-v001 step 1.
//
// Linux keeps the configuration in /etc/phpvpn, the ownership journal in
// /var/lib/phpvpn and the control socket with the service lock in /run/phpvpn;
// it logs to journald, so the log directory is unused there. macOS uses the
// canonical paths of paths_macos.h. The locations are fixed for the production
// service. Isolated end to end test instances replace whole directories through
// PosixDirectoryOverrides, never single files. Android has no fixed location:
// its host overrides every directory with one below the private directory of
// the application and anchors the trusted walk there (openTrustedDirectory).
#pragma once

#include <sys/types.h>

#include <filesystem>

#include "core/base/result.h"
#include "platform/posix/fd.h"

#if defined(__APPLE__)
#include "platform/macos/paths_macos.h"
#endif

namespace phpvpn {

#if defined(__APPLE__)
inline constexpr const char* kPosixConfigDirectory = kMacosConfigDirectory;
inline constexpr const char* kPosixStateDirectory = kMacosStateDirectory;
inline constexpr const char* kPosixRuntimeDirectory = kMacosRuntimeDirectory;
inline constexpr const char* kPosixLogDirectory = kMacosLogDirectory;

inline constexpr mode_t kPosixConfigDirectoryMode = kMacosConfigDirectoryMode;
inline constexpr mode_t kPosixStateDirectoryMode = kMacosStateDirectoryMode;
inline constexpr mode_t kPosixRuntimeDirectoryMode = kMacosRuntimeDirectoryMode;
inline constexpr mode_t kPosixLogDirectoryMode = kMacosLogDirectoryMode;
#else
inline constexpr const char* kPosixConfigDirectory = "/etc/phpvpn";
inline constexpr const char* kPosixStateDirectory = "/var/lib/phpvpn";
inline constexpr const char* kPosixRuntimeDirectory = "/run/phpvpn";
// Unused on Linux, the service logs to journald.
inline constexpr const char* kPosixLogDirectory = "/var/log/phpvpn";

inline constexpr mode_t kPosixConfigDirectoryMode = 0700;
inline constexpr mode_t kPosixStateDirectoryMode = 0700;
inline constexpr mode_t kPosixRuntimeDirectoryMode = 0755;
inline constexpr mode_t kPosixLogDirectoryMode = 0755;
#endif

// Base name of the rotated service log file in the log directory (macOS).
inline constexpr const char* kServiceLogBaseName = "phpvpn-service";

// An empty member keeps the production location.
struct PosixDirectoryOverrides {
    std::filesystem::path configDirectory{};
    std::filesystem::path stateDirectory{};
    std::filesystem::path runtimeDirectory{};
    std::filesystem::path logDirectory{};
};

struct PosixDirectories {
    std::filesystem::path config = kPosixConfigDirectory;
    std::filesystem::path state = kPosixStateDirectory;
    std::filesystem::path runtime = kPosixRuntimeDirectory;
    std::filesystem::path logs = kPosixLogDirectory;
};

// Applies the overrides. Every directory must be absolute, lexically normal and
// must not be the root directory, otherwise invalid_argument.
Result<PosixDirectories> resolvePosixDirectories(const PosixDirectoryOverrides& overrides = {});

std::filesystem::path configFilePath(const PosixDirectories& directories);    // config.json
std::filesystem::path journalFilePath(const PosixDirectories& directories);   // ownership.json
std::filesystem::path controlSocketPath(const PosixDirectories& directories); // control-v1.sock
std::filesystem::path serviceLockPath(const PosixDirectories& directories);   // service.lock
std::filesystem::path logDirectoryPath(const PosixDirectories& directories);  // the log directory itself

// Opens an absolute, lexically normal directory component by component with
// O_NOFOLLOW. Every component, the directory itself included, must be owned by
// root or by `expectedOwner` and must not be writable by group or others unless
// the sticky bit is set (/tmp). On macOS a root owned component that only the
// group wheel or daemon may write is trusted as well (/private/var/run is
// root:daemon 0775); both groups contain only root. A symbolic link, a non
// directory or an unsafe component fails with config_directory_security_invalid,
// a missing or unreadable component with config_unreadable. Production passes
// uid 0.
//
// `trustRoot` anchors the walk (plan-android-v001 point 13). Empty, the default
// and the only value of the Linux and macOS services, starts at "/" as
// described above. Otherwise it is an absolute, lexically normal directory
// other than "/" that `directory` equals or lies below, else invalid_argument.
// The anchor is opened as given: links among its ancestors are followed (they
// are outside the trust model), its last component must not be a link. It must
// be a directory owned by `expectedOwner` that others cannot write; its group
// bits are not checked, because Android creates the application directories
// 0771 with the private group of the application. Only the components below
// the anchor are checked by the rules above.
//
// Android always anchors: /data and /data/user/0 belong to system, and the
// application sandbox (own uid, SELinux untrusted_app, file based encryption)
// already isolates the private directory. The Android host passes
// Context.getNoBackupFilesDir() as `trustRoot` and getuid() as `expectedOwner`,
// to this function, to secureServiceDirectory(ies) and in
// PosixConfigFilePolicy::trustRoot; an empty anchor fails with invalid_argument
// there.
Result<UniqueFd> openTrustedDirectory(const std::filesystem::path& directory, uid_t expectedOwner = 0,
                                      const std::filesystem::path& trustRoot = {});

// Startup normalization of one service directory, the counterpart of the
// Windows ACL repair. Creates the directory when missing (its parent must
// exist), requires the owner `expectedOwner` and sets exactly `mode`. A symbolic
// link, a foreign owner or an unsafe ancestor fails closed with
// config_directory_security_invalid and changes nothing. With a `trustRoot`
// (see openTrustedDirectory) the directory must lie strictly below it.
Status secureServiceDirectory(const std::filesystem::path& directory, mode_t mode, uid_t expectedOwner = 0,
                              const std::filesystem::path& trustRoot = {});

// Configuration 0700, state 0700 and runtime 0755, in this order. The log
// directory is secured separately by the macOS service with
// secureServiceDirectory(directories.logs, kPosixLogDirectoryMode).
Status secureServiceDirectories(const PosixDirectories& directories, uid_t expectedOwner = 0,
                                const std::filesystem::path& trustRoot = {});

}  // namespace phpvpn
