// Every system call of config_files_posix.cpp and durable_file_posix.cpp, one
// by one in the temporary directory (plan-android-v001 T-N03). Inside the
// application sandbox SELinux (untrusted_app on app_data_file) decides what
// passes, not the file mode, and the configuration code only reports an error
// code. This probe prints the exact call and errno of a refusal, so a failed
// configuration test can be traced to the policy. Also tries the fixtures the
// configuration tests need: a symbolic link, a FIFO and a second hard link.
// linkat and link are reported only: Android publishes with the exchange
// backup strategy, and SELinux refuses applications every hard link.
#include "harness.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <unistd.h>

#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

#include "platform/posix/config_files_posix.h"

namespace phpvpn {
namespace {

struct ProbeDirectory {
    std::filesystem::path path;
    ProbeDirectory() {
        std::string pattern = (std::filesystem::temp_directory_path() / "phpvpn-probe-XXXXXX").string();
        if (::mkdtemp(pattern.data()) == nullptr) throw std::runtime_error("mkdtemp failed");
        path = std::filesystem::canonical(pattern);
    }
    ~ProbeDirectory() {
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
    }
    ProbeDirectory(const ProbeDirectory&) = delete;
    ProbeDirectory& operator=(const ProbeDirectory&) = delete;
};

// Prints the outcome and returns whether the call succeeded.
bool probe(const char* call, int result) {
    if (result >= 0) {
        std::cout << "    [ PROBE ] " << call << ": ok\n";
        return true;
    }
    const int error = errno;
    std::cout << "    [ PROBE ] " << call << ": errno " << error << " (" << ::strerror(error) << ")\n";
    return false;
}

}  // namespace

PHPVPN_TEST(android_posix, config_file_system_calls_are_permitted) {
    ProbeDirectory probeDirectory;
    const int directory = ::open(probeDirectory.path.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    PHPVPN_CHECK(probe("open(directory, O_DIRECTORY|O_NOFOLLOW)", directory));
    if (directory < 0) return;

    struct statfs filesystem {};
    PHPVPN_CHECK(probe("fstatfs(directory)", ::fstatfs(directory, &filesystem)));
    char magic[32];
    ::snprintf(magic, sizeof(magic), "0x%llx", static_cast<unsigned long long>(filesystem.f_type));
    std::cout << "    [ PROBE ] filesystem magic " << magic << "\n";
    PHPVPN_CHECK(linuxConfigFilesystemSupported(static_cast<std::uint32_t>(filesystem.f_type)));

    const int file = ::openat(directory, "stage", O_RDWR | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
    PHPVPN_CHECK(probe("openat(O_CREAT|O_EXCL|O_NOFOLLOW)", file));
    if (file < 0) {
        ::close(directory);
        return;
    }
    PHPVPN_CHECK(probe("fchmod(file, 0600)", ::fchmod(file, 0600)));
    PHPVPN_CHECK(probe("write(file)", static_cast<int>(::write(file, "abc", 3))));
    PHPVPN_CHECK(probe("fsync(file)", ::fsync(file)));
    PHPVPN_CHECK(probe("lseek(file)", static_cast<int>(::lseek(file, 0, SEEK_SET))));
    PHPVPN_CHECK(probe("fcntl(file, F_GETFL)", ::fcntl(file, F_GETFL)));
    struct stat info {};
    PHPVPN_CHECK(probe("fstat(file)", ::fstat(file, &info)));
    ::close(file);
    PHPVPN_CHECK(probe("fsync(directory)", ::fsync(directory)));

    // Only the hardLink backup strategy links; Android uses exchange because
    // SELinux refuses applications the link permission (EACCES). Reported, not
    // required.
    const bool linked = probe("linkat(stage, backup) [hardLink strategy only]",
                              ::linkat(directory, "stage", directory, "backup", 0));
    if (linked) {
        PHPVPN_CHECK(probe("fstatat(AT_SYMLINK_NOFOLLOW)", ::fstatat(directory, "backup", &info, AT_SYMLINK_NOFOLLOW)));
        PHPVPN_CHECK_EQ(info.st_nlink, static_cast<nlink_t>(2));
        PHPVPN_CHECK(probe("unlinkat(backup)", ::unlinkat(directory, "backup", 0)));
    }
    PHPVPN_CHECK(probe("renameat2(RENAME_NOREPLACE)",
                       ::renameat2(directory, "stage", directory, "config.json", RENAME_NOREPLACE)));
    const int other = ::openat(directory, "other", O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (other >= 0) ::close(other);
    errno = 0;
    const int refused = ::renameat2(directory, "other", directory, "config.json", RENAME_NOREPLACE);
    std::cout << "    [ PROBE ] renameat2(RENAME_NOREPLACE) onto an existing name: errno " << errno << "\n";
    PHPVPN_CHECK(refused != 0 && errno == EEXIST);

    // The exchange backup strategy: an atomic swap of two names keeps both inodes.
    struct stat published {};
    PHPVPN_CHECK(probe("fstatat(config.json)", ::fstatat(directory, "config.json", &published, AT_SYMLINK_NOFOLLOW)));
    PHPVPN_CHECK(probe("renameat2(RENAME_EXCHANGE)",
                       ::renameat2(directory, "other", directory, "config.json", RENAME_EXCHANGE)));
    struct stat swapped {};
    PHPVPN_CHECK(probe("fstatat(other)", ::fstatat(directory, "other", &swapped, AT_SYMLINK_NOFOLLOW)));
    PHPVPN_CHECK(swapped.st_ino == published.st_ino);
    PHPVPN_CHECK(probe("renameat(replace)", ::renameat(directory, "other", directory, "config.json")));
    PHPVPN_CHECK(probe("unlinkat(file)", ::unlinkat(directory, "config.json", 0)));

    // durableReplaceFile and the fixtures of the configuration tests.
    PHPVPN_CHECK(probe("mkdirat(0700)", ::mkdirat(directory, "nested", 0700)));
    PHPVPN_CHECK(probe("symlinkat", ::symlinkat("config.json", directory, "link")));
    PHPVPN_CHECK(probe("mkfifoat", ::mkfifoat(directory, "fifo", 0600)));
    const int fixture = ::openat(directory, "fixture", O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (fixture >= 0) ::close(fixture);
    probe("link (planted hard link fixture, reported only)",
          ::link((probeDirectory.path / "fixture").c_str(), (probeDirectory.path / "hard").c_str()));
    ::close(directory);
}

}  // namespace phpvpn
