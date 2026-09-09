#pragma once
#include <windows.h>
#include <functional>
#include <memory>
#include "platform/iface/config_files.h"

namespace phpvpn {
// Deterministic barriers/failure injection. Production leaves the callback empty.
// Returning false simulates failure at the named boundary, without weakening ACLs.
enum class ConfigFileStage {
    readChunk, stageWritten, beforeStageFlush, beforeJournalFlush,
    journalFlushed, beforePublish, afterPublish, beforeRecoveryCleanup,
    beforeDirectorySecurityRead
};
struct ConfigFileHooks {
    std::function<bool(ConfigFileStage)> proceed;
};

// Pure descriptor policy, shared by handle validation and nonprivileged tests.
Result<bool> validateWindowsConfigDescriptor(PSECURITY_DESCRIPTOR descriptor,
    bool directory);

// Local NTFS path only. The existing parent directory must have a protected
// SYSTEM/Administrators full control, safe inherited read/execute and a privileged owner.
// Ancestors are opened without delete sharing and reject reparse points.
// This factory never creates or relaxes the directory or target permissions.
Result<std::unique_ptr<ConfigFiles>> createWindowsConfigFiles(
    const std::filesystem::path& fixedPath, ConfigFileHooks hooks = {});
}  // namespace phpvpn
