// POSIX durable whole file replacement for Linux and macOS, plan-v001 section
// 4.2 point 10 and plan-macos-v001 step 1.
//
// The temporary file is created exclusively next to the target, synced, renamed
// over the target and the directory is synced, so after a power loss the target
// holds either the previous or the complete new content. On macOS fsync leaves
// the data in the drive cache, so both syncs use F_FULLFSYNC there.
#include "platform/iface/durable_file.h"

#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <string>

#include "platform/posix/fd.h"

namespace phpvpn {
namespace {

Error writeFailed() { return Error(ErrorCode::config_write_failed, "the ownership journal could not be written"); }
Error replaceFailed() { return Error(ErrorCode::config_write_failed, "the ownership journal could not be replaced"); }

// Linux fsync reaches stable storage. macOS fsync only hands the data to the
// drive; F_FULLFSYNC also flushes the drive cache. Filesystems without
// F_FULLFSYNC fall back to fsync.
int fullSync(int fd) {
#if defined(__APPLE__)
    if (::fcntl(fd, F_FULLFSYNC) == 0) return 0;
#endif
    return ::fsync(fd);
}

// Like std::filesystem::create_directories on Windows, but new directories are
// private. Errors surface when the directory is opened.
void createParents(const std::filesystem::path& directory) {
    std::filesystem::path current;
    for (const auto& part : directory) {
        current /= part;
        if (part.empty() || part == current.root_path()) continue;
        ::mkdir(current.c_str(), 0700);
    }
}

bool writeAll(int fd, std::string_view bytes) {
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        const std::size_t want = std::min<std::size_t>(bytes.size() - offset, 1 << 20);
        const ssize_t written = ::write(fd, bytes.data() + offset, want);
        if (written < 0 && errno == EINTR) continue;
        if (written <= 0) return false;
        offset += static_cast<std::size_t>(written);
    }
    return true;
}

}  // namespace

Status durableReplaceFile(const std::filesystem::path& path, std::string_view bytes) {
    const std::string name = path.filename().string();
    if (name.empty() || name == "." || name == "..") return writeFailed();
    const std::filesystem::path parent = path.has_parent_path() ? path.parent_path() : ".";
    createParents(parent);
    UniqueFd directory(::open(parent.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC));
    if (!directory) return writeFailed();

    // A temporary left behind by a crash is removed first. unlinkat removes a
    // planted symbolic link itself, never its referent.
    const std::string temporary = name + ".tmp";
    if (::unlinkat(directory.get(), temporary.c_str(), 0) != 0 && errno != ENOENT) return writeFailed();
    UniqueFd file(::openat(directory.get(), temporary.c_str(),
        O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600));
    if (!file) return writeFailed();

    const auto discard = [&](Error error) {
        file.reset();
        ::unlinkat(directory.get(), temporary.c_str(), 0);
        return error;
    };
    if (!writeAll(file.get(), bytes) || fullSync(file.get()) != 0) return discard(writeFailed());
    if (::close(file.release()) != 0) return discard(writeFailed());
    if (::renameat(directory.get(), temporary.c_str(), directory.get(), name.c_str()) != 0)
        return discard(replaceFailed());
    // The new content is in place. Report a failed directory sync anyway, the
    // replacement may not survive a power loss.
    if (fullSync(directory.get()) != 0) return replaceFailed();
    return ok();
}

}  // namespace phpvpn
