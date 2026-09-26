// Well known locations of the Linux installation, plan-v001 section 4.8.
//
// The configuration lives in /etc/phpvpn, the ownership journal in
// /var/lib/phpvpn and the control socket with the service lock in /run/phpvpn.
// The locations are fixed for the production service. Isolated end to end test
// instances replace whole directories through LinuxDirectoryOverrides, never
// single files.
#pragma once

#include <sys/types.h>

#include <filesystem>

#include "core/base/result.h"
#include "platform/linux/fd.h"

namespace phpvpn {

inline constexpr char kLinuxConfigDirectory[] = "/etc/phpvpn";
inline constexpr char kLinuxStateDirectory[] = "/var/lib/phpvpn";
inline constexpr char kLinuxRuntimeDirectory[] = "/run/phpvpn";

inline constexpr mode_t kLinuxConfigDirectoryMode = 0700;
inline constexpr mode_t kLinuxStateDirectoryMode = 0700;
inline constexpr mode_t kLinuxRuntimeDirectoryMode = 0755;

// An empty member keeps the production location.
struct LinuxDirectoryOverrides {
    std::filesystem::path configDirectory;
    std::filesystem::path stateDirectory;
    std::filesystem::path runtimeDirectory;
};

struct LinuxDirectories {
    std::filesystem::path config = kLinuxConfigDirectory;
    std::filesystem::path state = kLinuxStateDirectory;
    std::filesystem::path runtime = kLinuxRuntimeDirectory;
};

// Applies the overrides. Every directory must be absolute, lexically normal and
// must not be the root directory, otherwise invalid_argument.
Result<LinuxDirectories> resolveLinuxDirectories(const LinuxDirectoryOverrides& overrides = {});

std::filesystem::path configFilePath(const LinuxDirectories& directories);    // config.json
std::filesystem::path journalFilePath(const LinuxDirectories& directories);   // ownership.json
std::filesystem::path controlSocketPath(const LinuxDirectories& directories); // control-v1.sock
std::filesystem::path serviceLockPath(const LinuxDirectories& directories);   // service.lock

// Opens an absolute, lexically normal directory component by component with
// O_NOFOLLOW. Every component, the directory itself included, must be owned by
// root or by `expectedOwner` and must not be writable by group or others unless
// the sticky bit is set (/tmp). A symbolic link, a non directory or an unsafe
// component fails with config_directory_security_invalid, a missing or
// unreadable component with config_unreadable. Production passes uid 0.
Result<UniqueFd> openTrustedDirectory(const std::filesystem::path& directory, uid_t expectedOwner = 0);

// Startup normalization of one service directory, the counterpart of the
// Windows ACL repair. Creates the directory when missing (its parent must
// exist), requires the owner `expectedOwner` and sets exactly `mode`. A symbolic
// link, a foreign owner or an unsafe ancestor fails closed with
// config_directory_security_invalid and changes nothing.
Status secureServiceDirectory(const std::filesystem::path& directory, mode_t mode, uid_t expectedOwner = 0);

// Configuration 0700, state 0700 and runtime 0755, in this order.
Status secureServiceDirectories(const LinuxDirectories& directories, uid_t expectedOwner = 0);

}  // namespace phpvpn
