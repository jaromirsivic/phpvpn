// Forwarding header: the Linux locations moved to platform/posix/paths_posix.h,
// which is shared with macOS. The Linux names stay as aliases for the service,
// the tray and the tests.
#pragma once

#include "platform/posix/paths_posix.h"

namespace phpvpn {

inline constexpr const char* kLinuxConfigDirectory = kPosixConfigDirectory;
inline constexpr const char* kLinuxStateDirectory = kPosixStateDirectory;
inline constexpr const char* kLinuxRuntimeDirectory = kPosixRuntimeDirectory;

inline constexpr mode_t kLinuxConfigDirectoryMode = kPosixConfigDirectoryMode;
inline constexpr mode_t kLinuxStateDirectoryMode = kPosixStateDirectoryMode;
inline constexpr mode_t kLinuxRuntimeDirectoryMode = kPosixRuntimeDirectoryMode;

using LinuxDirectoryOverrides = PosixDirectoryOverrides;
using LinuxDirectories = PosixDirectories;

inline Result<LinuxDirectories> resolveLinuxDirectories(const LinuxDirectoryOverrides& overrides = {}) {
    return resolvePosixDirectories(overrides);
}

}  // namespace phpvpn
