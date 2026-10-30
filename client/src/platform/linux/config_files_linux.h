// Forwarding header: the fixed-path ConfigFiles backend moved to
// platform/posix/config_files_posix.h, which is shared with macOS. The Linux
// names stay as aliases for the service, the tray and the tests.
#pragma once

#include <filesystem>
#include <memory>
#include <utility>

#include "platform/posix/config_files_posix.h"

namespace phpvpn {

using LinuxConfigFileStage = PosixConfigFileStage;
using LinuxConfigFileAction = PosixConfigFileAction;
using LinuxConfigFilePolicy = PosixConfigFilePolicy;

inline Result<std::unique_ptr<ConfigFiles>> createLinuxConfigFiles(
    const std::filesystem::path& fixedPath, LinuxConfigFilePolicy policy = {}) {
    return createPosixConfigFiles(fixedPath, std::move(policy));
}

}  // namespace phpvpn
