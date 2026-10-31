// The anchored trusted walk of paths_posix (plan-android-v001 point 13, WP02).
// Android trusts the private directory of the application and checks only the
// components below it, because /data and /data/user/0 belong to system. The
// walk itself is platform independent, so the Linux suite simulates the
// Android tree: an ancestor of the anchor that the unanchored walk refuses
// (world writable, not sticky) stands in for the system owned /data. Linux and
// macOS production never pass an anchor; their semantics stay covered by
// test_config_files_linux.cpp.
#include "harness.h"

#include <fcntl.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#include <filesystem>
#include <stdexcept>
#include <string>

#include "platform/posix/config_files_posix.h"
#include "platform/posix/paths_posix.h"

namespace phpvpn {
namespace {

struct TrustTree {
    std::filesystem::path path;   // private, canonical mkdtemp directory
    std::filesystem::path open;   // world writable without sticky bit, like a foreign /data
    std::filesystem::path anchor; // the private application directory
    TrustTree() {
        std::string pattern = (std::filesystem::temp_directory_path() / "phpvpn-anchor-XXXXXX").string();
        if (::mkdtemp(pattern.data()) == nullptr) throw std::runtime_error("mkdtemp failed");
        path = std::filesystem::canonical(pattern);
        open = path / "data";
        anchor = open / "app";
        if (::mkdir(open.c_str(), 0700) != 0 || ::chmod(open.c_str(), 0777) != 0) throw std::runtime_error("mkdir");
        // Android creates the application directories 0771.
        if (::mkdir(anchor.c_str(), 0700) != 0 || ::chmod(anchor.c_str(), 0771) != 0)
            throw std::runtime_error("mkdir");
    }
    ~TrustTree() {
        std::error_code ignored;
        ::chmod(open.c_str(), 0700);
        ::chmod(anchor.c_str(), 0700);
        std::filesystem::remove_all(path, ignored);
    }
    TrustTree(const TrustTree&) = delete;
    TrustTree& operator=(const TrustTree&) = delete;
};

mode_t modeOf(const std::filesystem::path& path) {
    struct stat info {};
    return ::lstat(path.c_str(), &info) == 0 ? (info.st_mode & 07777) : 0;
}

}  // namespace

PHPVPN_TEST(paths_trust_root, only_components_below_the_anchor_are_checked) {
    TrustTree tree;
    const uid_t self = ::geteuid();
    // Below the open ancestor the unanchored walk refuses, the anchored one not.
#if !defined(__ANDROID__)
    PHPVPN_REQUIRE_ERROR(openTrustedDirectory(tree.anchor, self), ErrorCode::config_directory_security_invalid);
#endif
    PHPVPN_REQUIRE_OK(openTrustedDirectory(tree.anchor, self, tree.anchor));

    // Startup normalization below the anchor: created with the exact modes.
    PosixDirectoryOverrides overrides{tree.anchor / "phpvpn", tree.anchor / "state", tree.anchor / "run",
                                      tree.anchor / "logs"};
    auto directories = resolvePosixDirectories(overrides);
    PHPVPN_REQUIRE_OK(directories);
    const mode_t previous = ::umask(077);
    auto secured = secureServiceDirectories(directories.value(), self, tree.anchor);
    ::umask(previous);
    PHPVPN_REQUIRE_OK(secured);
    PHPVPN_CHECK_EQ(modeOf(tree.anchor / "phpvpn"), mode_t(0700));
    PHPVPN_CHECK_EQ(modeOf(tree.anchor / "run"), mode_t(0755));
    PHPVPN_REQUIRE_OK(secureServiceDirectory(directories->logs, kPosixLogDirectoryMode, self, tree.anchor));
    PHPVPN_REQUIRE_OK(openTrustedDirectory(tree.anchor / "phpvpn", self, tree.anchor));
    // The anchor itself is the parent at most, never created or repaired.
    PHPVPN_REQUIRE_ERROR(secureServiceDirectory(tree.anchor, 0700, self, tree.anchor), ErrorCode::invalid_argument);
    PHPVPN_CHECK_EQ(modeOf(tree.anchor), mode_t(0771));

    // The configuration backend over the same anchor publishes normally.
    auto files = createPosixConfigFiles(configFilePath(directories.value()),
                                        PosixConfigFilePolicy{self, {}, tree.anchor});
    PHPVPN_REQUIRE_OK(files);
    auto published = files.value()->publish("{}", ConfigPublishMode::replace, "00000000-0000-4000-8000-000000000001");
    PHPVPN_REQUIRE_OK(published);
    PHPVPN_CHECK(published->state == ConfigPublishState::confirmed);
#if !defined(__ANDROID__)
    PHPVPN_REQUIRE_ERROR(createPosixConfigFiles(configFilePath(directories.value()), PosixConfigFilePolicy{self, {}, {}}),
                         ErrorCode::config_directory_security_invalid);
#endif
}

PHPVPN_TEST(paths_trust_root, components_below_the_anchor_keep_every_rule) {
    TrustTree tree;
    const uid_t self = ::geteuid();
    const auto shared = tree.anchor / "shared";
    PHPVPN_CHECK(::mkdir(shared.c_str(), 0700) == 0 && ::chmod(shared.c_str(), 0777) == 0);
    PHPVPN_CHECK(::mkdir((shared / "phpvpn").c_str(), 0700) == 0);
    PHPVPN_REQUIRE_ERROR(openTrustedDirectory(shared / "phpvpn", self, tree.anchor),
                         ErrorCode::config_directory_security_invalid);
    PHPVPN_REQUIRE_ERROR(secureServiceDirectory(shared / "other", 0700, self, tree.anchor),
                         ErrorCode::config_directory_security_invalid);
    PHPVPN_CHECK(!std::filesystem::exists(shared / "other"));
    PHPVPN_CHECK(::chmod(shared.c_str(), 01777) == 0);
    PHPVPN_REQUIRE_OK(openTrustedDirectory(shared / "phpvpn", self, tree.anchor));

    // A symbolic link below the anchor is refused, a regular file is not a directory.
    const auto link = tree.anchor / "link";
    PHPVPN_CHECK(::symlink((tree.anchor / "shared").c_str(), link.c_str()) == 0);
    PHPVPN_REQUIRE_ERROR(openTrustedDirectory(link / "phpvpn", self, tree.anchor),
                         ErrorCode::config_directory_security_invalid);
    const auto file = tree.anchor / "file";
    const int fd = ::open(file.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
    PHPVPN_CHECK(fd >= 0);
    if (fd >= 0) ::close(fd);
    PHPVPN_REQUIRE_ERROR(openTrustedDirectory(file, self, tree.anchor), ErrorCode::config_directory_security_invalid);
    PHPVPN_REQUIRE_ERROR(openTrustedDirectory(tree.anchor / "missing", self, tree.anchor),
                         ErrorCode::config_unreadable);
}

PHPVPN_TEST(paths_trust_root, anchor_must_be_an_own_directory_others_cannot_write) {
    TrustTree tree;
    const uid_t self = ::geteuid();
    PHPVPN_REQUIRE_ERROR(openTrustedDirectory(tree.anchor, self + 1, tree.anchor),
                         ErrorCode::config_directory_security_invalid);
    PHPVPN_CHECK(::chmod(tree.anchor.c_str(), 0773) == 0);
    PHPVPN_REQUIRE_ERROR(openTrustedDirectory(tree.anchor, self, tree.anchor),
                         ErrorCode::config_directory_security_invalid);
    // Group write is the sandbox's business (Android 0771 / 0770 app directories).
    PHPVPN_CHECK(::chmod(tree.anchor.c_str(), 0770) == 0);
    PHPVPN_REQUIRE_OK(openTrustedDirectory(tree.anchor, self, tree.anchor));
    PHPVPN_CHECK(::chmod(tree.anchor.c_str(), 0771) == 0);

    // The last component of the anchor must not be a link; links above it are
    // outside the trust model and followed (Android: /data/user/0 -> /data/data).
    const auto anchorLink = tree.open / "app-link";
    PHPVPN_CHECK(::symlink(tree.anchor.c_str(), anchorLink.c_str()) == 0);
    PHPVPN_REQUIRE_ERROR(openTrustedDirectory(anchorLink, self, anchorLink),
                         ErrorCode::config_directory_security_invalid);
    const auto user = tree.path / "user";
    PHPVPN_CHECK(::symlink(tree.open.c_str(), user.c_str()) == 0);
    PHPVPN_CHECK(::mkdir((tree.anchor / "phpvpn").c_str(), 0700) == 0);
    PHPVPN_REQUIRE_OK(openTrustedDirectory(user / "app" / "phpvpn", self, user / "app"));

    const auto missing = tree.open / "gone";
    PHPVPN_REQUIRE_ERROR(openTrustedDirectory(missing / "phpvpn", self, missing), ErrorCode::config_unreadable);
}

PHPVPN_TEST(paths_trust_root, directory_outside_the_anchor_or_bad_anchor_is_invalid) {
    TrustTree tree;
    const uid_t self = ::geteuid();
    const auto sibling = tree.open / "app2";
    PHPVPN_CHECK(::mkdir(sibling.c_str(), 0700) == 0);
    // Element-wise: app2 is not below app.
    PHPVPN_REQUIRE_ERROR(openTrustedDirectory(sibling, self, tree.anchor), ErrorCode::invalid_argument);
    PHPVPN_REQUIRE_ERROR(openTrustedDirectory(tree.open, self, tree.anchor), ErrorCode::invalid_argument);
    for (const auto& bad : {std::filesystem::path("relative/app"), std::filesystem::path("/"),
                                            tree.open / "x" / ".." / "app", std::filesystem::path(tree.anchor.native() + "/")}) {
        PHPVPN_REQUIRE_ERROR(openTrustedDirectory(tree.anchor / "phpvpn", self, bad), ErrorCode::invalid_argument);
    }
    PHPVPN_REQUIRE_ERROR(secureServiceDirectory(sibling / "phpvpn", 0700, self, tree.anchor),
                         ErrorCode::invalid_argument);
    PHPVPN_CHECK(!std::filesystem::exists(sibling / "phpvpn"));
    PHPVPN_REQUIRE_ERROR(createPosixConfigFiles(sibling / "config.json", PosixConfigFilePolicy{self, {}, tree.anchor}),
                         ErrorCode::invalid_argument);
}

#if defined(__ANDROID__)
PHPVPN_TEST(paths_trust_root, android_never_walks_from_the_file_system_root) {
    TrustTree tree;
    PHPVPN_REQUIRE_ERROR(openTrustedDirectory(tree.anchor, ::geteuid()), ErrorCode::invalid_argument);
    PHPVPN_REQUIRE_ERROR(secureServiceDirectory(tree.anchor / "phpvpn", 0700, ::geteuid()),
                         ErrorCode::invalid_argument);
    PHPVPN_REQUIRE_ERROR(createPosixConfigFiles(tree.anchor / "config.json", PosixConfigFilePolicy{::geteuid(), {}, {}}),
                         ErrorCode::invalid_argument);
    PHPVPN_CHECK(!std::filesystem::exists(tree.anchor / "phpvpn"));
}
#endif

}  // namespace phpvpn
