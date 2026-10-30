#include "platform/posix/paths_posix.h"

#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>

#include <string>

namespace phpvpn {
namespace {

Error insecure() {
    return Error(ErrorCode::config_directory_security_invalid, "a phpvpn directory is not safely owned");
}
Error unreadable() {
    return Error(ErrorCode::config_unreadable, "a phpvpn directory cannot be opened");
}

bool validDirectory(const std::filesystem::path& directory) {
    return directory.is_absolute() && directory == directory.lexically_normal() &&
        directory != directory.root_path() && !directory.native().ends_with('/') &&
        directory.native().find('\0') == std::string::npos;
}

// Root or the expected owner, and nobody else can add, rename or remove
// entries. A sticky directory only lets owners rename their own entries.
bool trustedComponent(const struct stat& info, uid_t expectedOwner) {
    if (!S_ISDIR(info.st_mode) || (info.st_uid != 0 && info.st_uid != expectedOwner)) return false;
#if defined(__APPLE__)
    // macOS ships /private/var/run as root:daemon 0775. The groups wheel (0)
    // and daemon (1) have root as their only member, so a root owned component
    // writable by one of them is as safe as a 0755 one. Others never may write.
    if (info.st_uid == 0 && (info.st_mode & S_IWOTH) == 0 && (info.st_gid == 0 || info.st_gid == 1)) return true;
#endif
    return (info.st_mode & (S_IWGRP | S_IWOTH)) == 0 || (info.st_mode & S_ISVTX) != 0;
}

Result<UniqueFd> openComponent(int parent, const char* name) {
    UniqueFd fd(::openat(parent, name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
    if (!fd) {
        if (errno == ELOOP || errno == ENOTDIR) return insecure();
        return unreadable();
    }
    return fd;
}

}  // namespace

Result<PosixDirectories> resolvePosixDirectories(const PosixDirectoryOverrides& overrides) {
    PosixDirectories result;
    if (!overrides.configDirectory.empty()) result.config = overrides.configDirectory;
    if (!overrides.stateDirectory.empty()) result.state = overrides.stateDirectory;
    if (!overrides.runtimeDirectory.empty()) result.runtime = overrides.runtimeDirectory;
    if (!overrides.logDirectory.empty()) result.logs = overrides.logDirectory;
    if (!validDirectory(result.config) || !validDirectory(result.state) || !validDirectory(result.runtime) ||
        !validDirectory(result.logs))
        return Error(ErrorCode::invalid_argument, "a phpvpn directory override is not an absolute normal path");
    return result;
}

std::filesystem::path configFilePath(const PosixDirectories& directories) {
    return directories.config / "config.json";
}

std::filesystem::path journalFilePath(const PosixDirectories& directories) {
    return directories.state / "ownership.json";
}

std::filesystem::path controlSocketPath(const PosixDirectories& directories) {
    return directories.runtime / "control-v1.sock";
}

std::filesystem::path serviceLockPath(const PosixDirectories& directories) {
    return directories.runtime / "service.lock";
}

std::filesystem::path logDirectoryPath(const PosixDirectories& directories) {
    return directories.logs;
}

Result<UniqueFd> openTrustedDirectory(const std::filesystem::path& directory, uid_t expectedOwner) {
    if (!directory.is_absolute() || directory != directory.lexically_normal() ||
        directory.native().find('\0') != std::string::npos)
        return Error(ErrorCode::invalid_argument, "the directory is not an absolute normal path");
    UniqueFd current(::open("/", O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
    if (!current) return unreadable();
    struct stat info {};
    if (::fstat(current.get(), &info) != 0) return unreadable();
    if (!trustedComponent(info, expectedOwner)) return insecure();
    for (const auto& part : directory.relative_path()) {
        if (part.empty()) continue;  // a trailing separator
        auto next = openComponent(current.get(), part.c_str());
        if (!next) return next.error();
        if (::fstat(next->get(), &info) != 0) return unreadable();
        if (!trustedComponent(info, expectedOwner)) return insecure();
        current = std::move(next).value();
    }
    return current;
}

Status secureServiceDirectory(const std::filesystem::path& directory, mode_t mode, uid_t expectedOwner) {
    if (!validDirectory(directory) || (mode & ~static_cast<mode_t>(07777)) != 0)
        return Error(ErrorCode::invalid_argument, "the directory is not an absolute normal path");
    auto parent = openTrustedDirectory(directory.parent_path(), expectedOwner);
    if (!parent) return parent.error();
    const std::string name = directory.filename().string();
    if (::mkdirat(parent->get(), name.c_str(), mode) != 0 && errno != EEXIST)
        return Error(ErrorCode::config_write_failed, "a phpvpn directory could not be created");
    auto opened = openComponent(parent->get(), name.c_str());
    if (!opened) return opened.error();
    struct stat info {};
    if (::fstat(opened->get(), &info) != 0) return unreadable();
    // Never take over a directory somebody else created in our place.
    if (!S_ISDIR(info.st_mode) || info.st_uid != expectedOwner) return insecure();
    if ((info.st_mode & 07777) != mode) {
        if (::fchmod(opened->get(), mode) != 0 || ::fstat(opened->get(), &info) != 0 ||
            (info.st_mode & 07777) != mode)
            return Error(ErrorCode::config_write_failed, "a phpvpn directory mode could not be repaired");
    }
    return ok();
}

Status secureServiceDirectories(const PosixDirectories& directories, uid_t expectedOwner) {
    PHPVPN_TRY(secureServiceDirectory(directories.config, kPosixConfigDirectoryMode, expectedOwner));
    PHPVPN_TRY(secureServiceDirectory(directories.state, kPosixStateDirectoryMode, expectedOwner));
    return secureServiceDirectory(directories.runtime, kPosixRuntimeDirectoryMode, expectedOwner);
}

}  // namespace phpvpn
