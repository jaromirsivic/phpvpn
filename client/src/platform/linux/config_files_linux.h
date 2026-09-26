// POSIX implementation of the fixed-path ConfigFiles boundary, plan-v001
// section 4.8. Same interface semantics, journal format and file names as
// platform/windows/config_files_win.cpp.
#pragma once

#include <sys/types.h>

#include <cstdint>
#include <functional>
#include <memory>

#include "platform/iface/config_files.h"

namespace phpvpn {

// Deterministic barriers and failure injection, in the order the publication
// reaches them. Production leaves the hook empty.
enum class LinuxConfigFileStage {
    readChunk,
    beforeDirectorySecurityRead,
    stageCreated,        // empty protected stage file exists, no journal yet
    beforeJournalFlush,  // journal record written, not yet synced
    journalFlushed,      // journal and directory synced, no secret byte written
    stageWritten,
    beforeStageFlush,
    stageVerified,       // stage reopened read-only, identity and hash verified
    backupLinked,        // config.previous links the original inode
    beforePublish,
    afterPublish,        // rename done, directory not yet synced
    afterDirectorySync,
    beforeConfirm,       // published identity verified, CONFIRMED not written
    beforeRecoveryCleanup,
};

// fail takes the same error path as a failed system call at that point. crash
// returns at once with ErrorCode::internal and leaves every file exactly as a
// killed process would, so a new instance can exercise recover().
enum class LinuxConfigFileAction { proceed, fail, crash };

struct LinuxConfigFilePolicy {
    // Owner required for the directory and every file. Production is root, the
    // unprivileged tests pass geteuid().
    uid_t expectedOwner = 0;
    std::function<LinuxConfigFileAction(LinuxConfigFileStage)> hook;
};

// statfs magic policy: network and FUSE filesystems are refused, the analogue
// of the local NTFS requirement on Windows.
bool linuxConfigFilesystemSupported(std::uint32_t magic) noexcept;

// Absolute, lexically normal path whose file name is config.json. Ancestors are
// opened without following links and must be owned by root or the expected
// owner and not writable by others; the directory itself must be owned by the
// expected owner and not writable by group or others. The directory is kept
// open, every operation is relative to it. This factory never creates or
// relaxes the directory or target permissions.
Result<std::unique_ptr<ConfigFiles>> createLinuxConfigFiles(
    const std::filesystem::path& fixedPath, LinuxConfigFilePolicy policy = {});

}  // namespace phpvpn
