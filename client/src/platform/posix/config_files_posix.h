// POSIX implementation of the fixed-path ConfigFiles boundary for Linux and
// macOS, plan-v001 section 4.8 and plan-macos-v001 step 1. Same interface
// semantics, journal format and file names as platform/windows/config_files_win.cpp.
//
// Platform differences: the create-only publication renames with
// renameat2(RENAME_NOREPLACE) on Linux and renameatx_np(RENAME_EXCL) on macOS;
// the local filesystem check uses the statfs f_type magic on Linux and the
// f_fstypename string on macOS; the journal and publication syncs add
// F_FULLFSYNC on macOS, where fsync does not flush the drive cache.
#pragma once

#include <sys/types.h>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string_view>

#include "platform/iface/config_files.h"

namespace phpvpn {

// Deterministic barriers and failure injection, in the order the publication
// reaches them. Production leaves the hook empty.
enum class PosixConfigFileStage {
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
enum class PosixConfigFileAction { proceed, fail, crash };

struct PosixConfigFilePolicy {
    // Owner required for the directory and every file. Production is root, the
    // unprivileged tests pass geteuid().
    uid_t expectedOwner = 0;
    std::function<PosixConfigFileAction(PosixConfigFileStage)> hook;
    // Anchor of the trusted walk to the directory, see openTrustedDirectory in
    // paths_posix.h. Empty on Linux and macOS; Android passes the private
    // directory of the application.
    std::filesystem::path trustRoot{};
};

#if defined(__linux__)
// statfs magic policy: network and FUSE filesystems are refused, the analogue
// of the local NTFS requirement on Windows.
bool linuxConfigFilesystemSupported(std::uint32_t magic) noexcept;
#endif

#if defined(__APPLE__)
// statfs f_fstypename policy, compared case-insensitively: nfs, smbfs, afpfs,
// webdav, cifs, osxfuse, macfuse, fuse and ftp are refused, every other
// (local) filesystem such as apfs or hfs is allowed.
bool macosConfigFilesystemSupported(std::string_view fstypename) noexcept;
#endif

// Absolute, lexically normal path whose file name is config.json. Ancestors are
// opened without following links and must be owned by root or the expected
// owner and not writable by others (with a trust root only the ancestors below
// it, see PosixConfigFilePolicy::trustRoot); the directory itself must be owned by the
// expected owner and not writable by group or others. The directory is kept
// open, every operation is relative to it. This factory never creates or
// relaxes the directory or target permissions.
Result<std::unique_ptr<ConfigFiles>> createPosixConfigFiles(
    const std::filesystem::path& fixedPath, PosixConfigFilePolicy policy = {});

}  // namespace phpvpn
