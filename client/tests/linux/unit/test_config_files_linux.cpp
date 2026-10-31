// Linux platform tests owned by WP02 (plan-v001 section 9.4): the POSIX
// ConfigFiles backend, durableReplaceFile and the service directory policy.
// Everything runs without privileges; the expected owner is geteuid(). Shared
// with macOS (plan-macos-v001 step 1): the statfs policy and the production
// paths are platform specific, macOS paths are in tests/macos/unit. Android
// runs the same file (plan-android-v001 T-N03) inside the application sandbox,
// with the trusted walk anchored at the temporary directory the runner points
// TMPDIR to.
#include "harness.h"

#include <fcntl.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "core/config/config_defaults.h"
#include "core/config/config_document.h"
#include "core/config/config_store.h"
#include "core/crypto/crypto_provider.h"
#include "platform/iface/durable_file.h"
#include "platform/linux/config_files_linux.h"
#include "platform/linux/paths_linux.h"

namespace phpvpn {
namespace {

constexpr char kCommitA[] = "00000000-0000-4000-8000-000000000001";
constexpr char kCommitB[] = "00000000-0000-4000-8000-000000000002";
constexpr char kCommitC[] = "00000000-0000-4000-8000-000000000003";

using Stage = LinuxConfigFileStage;
using Action = LinuxConfigFileAction;

void makeRemovable(const std::filesystem::path& path) {
    struct stat info {};
    if (::lstat(path.c_str(), &info) != 0 || !S_ISDIR(info.st_mode)) return;
    ::chmod(path.c_str(), 0700);
    std::error_code error;
    for (std::filesystem::directory_iterator it(path, error), end; !error && it != end; it.increment(error))
        makeRemovable(it->path());
}

// Private directory created by mkdtemp under the system temporary directory.
// The path is canonical: the trusted walk refuses symbolic link ancestors, and
// the macOS TMPDIR lies below the /var link.
struct TempDirectory {
    std::filesystem::path path;
    TempDirectory() {
        std::string pattern = (std::filesystem::temp_directory_path() / "phpvpn-wp02-XXXXXX").string();
        if (::mkdtemp(pattern.data()) == nullptr) throw std::runtime_error("mkdtemp failed");
        path = std::filesystem::canonical(pattern);
    }
    ~TempDirectory() {
        makeRemovable(path);
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
    }
    TempDirectory(const TempDirectory&) = delete;
    TempDirectory& operator=(const TempDirectory&) = delete;
    std::filesystem::path config() const { return path / "config.json"; }
};

void writeFile(const std::filesystem::path& path, std::string_view text, mode_t mode = 0600) {
    const int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW | O_CLOEXEC, mode);
    PHPVPN_CHECK(fd >= 0);
    if (fd < 0) return;
    PHPVPN_CHECK(::fchmod(fd, mode) == 0);
    PHPVPN_CHECK(::write(fd, text.data(), text.size()) == static_cast<ssize_t>(text.size()));
    ::close(fd);
}

std::string readFile(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) return "READ ERROR";
    return std::string((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
}

std::vector<std::string> names(const std::filesystem::path& directory) {
    std::vector<std::string> result;
    for (const auto& entry : std::filesystem::directory_iterator(directory))
        result.push_back(entry.path().filename().string());
    std::sort(result.begin(), result.end());
    return result;
}

mode_t modeOf(const std::filesystem::path& path) {
    struct stat info {};
    return ::lstat(path.c_str(), &info) == 0 ? (info.st_mode & 07777) : 0;
}

// Empty on Linux and macOS, where the walk starts at "/". Android always
// anchors it (paths_posix.h); the system owned /data could never pass.
std::filesystem::path trustRoot() {
#if defined(__ANDROID__)
    return std::filesystem::canonical(std::filesystem::temp_directory_path());
#else
    return {};
#endif
}

LinuxConfigFilePolicy policy(std::function<Action(Stage)> hook = {}) {
    return LinuxConfigFilePolicy{::geteuid(), std::move(hook), trustRoot()};
}

// Fires `action` once at `stage` after arm() was called.
struct Injector {
    Stage stage;
    Action action;
    bool armed = false;
    bool fired = false;
    std::function<Action(Stage)> hook() {
        return [this](Stage reached) {
            if (!armed || fired || reached != stage) return Action::proceed;
            fired = true;
            return action;
        };
    }
};

using Files = std::unique_ptr<ConfigFiles>;

}  // namespace

PHPVPN_TEST(config_files_linux, factory_rejects_invalid_paths_and_unsafe_directories) {
    TempDirectory fixture;
    PHPVPN_REQUIRE_ERROR(createLinuxConfigFiles("config.json", policy()), ErrorCode::invalid_argument);
    PHPVPN_REQUIRE_ERROR(createLinuxConfigFiles(fixture.path / "other.json", policy()), ErrorCode::invalid_argument);
    PHPVPN_REQUIRE_ERROR(createLinuxConfigFiles(fixture.path / "x" / ".." / "config.json", policy()),
                         ErrorCode::invalid_argument);
    PHPVPN_REQUIRE_ERROR(createLinuxConfigFiles(fixture.path / "missing" / "config.json", policy()),
                         ErrorCode::config_unreadable);
    // A different expected owner makes the directory foreign.
    PHPVPN_REQUIRE_ERROR(
        createLinuxConfigFiles(fixture.config(), LinuxConfigFilePolicy{::geteuid() + 1, {}, trustRoot()}),
        ErrorCode::config_directory_security_invalid);
    for (const mode_t unsafe : {mode_t(0720), mode_t(0702), mode_t(0777)}) {
        PHPVPN_CHECK(::chmod(fixture.path.c_str(), unsafe) == 0);
        PHPVPN_REQUIRE_ERROR(createLinuxConfigFiles(fixture.config(), policy()),
                             ErrorCode::config_directory_security_invalid);
    }
    PHPVPN_CHECK(::chmod(fixture.path.c_str(), 0700) == 0);
    const auto real = fixture.path / "real";
    const auto link = fixture.path / "link";
    PHPVPN_CHECK(::mkdir(real.c_str(), 0700) == 0);
    PHPVPN_CHECK(::symlink(real.c_str(), link.c_str()) == 0);
    PHPVPN_REQUIRE_ERROR(createLinuxConfigFiles(link / "config.json", policy()),
                         ErrorCode::config_directory_security_invalid);
    // An ancestor writable by others without the sticky bit.
    const auto shared = fixture.path / "open";
    PHPVPN_CHECK(::mkdir(shared.c_str(), 0700) == 0);
    PHPVPN_CHECK(::mkdir((shared / "etc").c_str(), 0700) == 0);
    PHPVPN_CHECK(::chmod(shared.c_str(), 0777) == 0);
    PHPVPN_REQUIRE_ERROR(createLinuxConfigFiles(shared / "etc" / "config.json", policy()),
                         ErrorCode::config_directory_security_invalid);
    PHPVPN_CHECK(::chmod(shared.c_str(), 01777) == 0);
    PHPVPN_REQUIRE_OK(createLinuxConfigFiles(shared / "etc" / "config.json", policy()));
    PHPVPN_REQUIRE_OK(createLinuxConfigFiles(fixture.config(), policy()));
    PHPVPN_CHECK((names(fixture.path) == std::vector<std::string>{"link", "open", "real"}));
    PHPVPN_CHECK(names(real).empty());
}

#if defined(__linux__)
PHPVPN_TEST(config_files_linux, filesystem_policy_refuses_network_and_fuse) {
    for (const std::uint32_t refused : {0x6969u, 0x517Bu, 0xFF534D42u, 0xFE534D42u, 0x65735546u, 0x01021997u})
        PHPVPN_CHECK(!linuxConfigFilesystemSupported(refused));
    // tmpfs, ext4, xfs, btrfs and overlayfs are local.
    for (const std::uint32_t local : {0x01021994u, 0xEF53u, 0x58465342u, 0x9123683Eu, 0x794C7630u})
        PHPVPN_CHECK(linuxConfigFilesystemSupported(local));
}
#endif

#if defined(__APPLE__)
PHPVPN_TEST(config_files_macos, filesystem_policy_refuses_network_and_fuse_by_type_name) {
    for (const char* refused : {"nfs", "smbfs", "afpfs", "webdav", "cifs", "osxfuse", "macfuse", "fuse", "ftp",
                                "NFS", "SMBfs", "MacFUSE"})
        PHPVPN_CHECK(!macosConfigFilesystemSupported(refused));
    // apfs, hfs, msdos, exfat and the empty name are allowed; only the listed
    // names are refused, not their prefixes.
    for (const char* local : {"apfs", "hfs", "msdos", "exfat", "", "nfsx", "fus"})
        PHPVPN_CHECK(macosConfigFilesystemSupported(local));
}
#endif

PHPVPN_TEST(config_files_linux, bounded_read_exact_hash_and_replace_create_only) {
    TempDirectory fixture;
    auto created = createLinuxConfigFiles(fixture.config(), policy());
    PHPVPN_REQUIRE_OK(created);
    auto files = std::move(created).value();
    PHPVPN_CHECK(files->path() == fixture.config());
    auto absent = files->readBounded(kMaxConfigFileBytes);
    PHPVPN_REQUIRE_OK(absent);
    PHPVPN_CHECK(!absent->found);
    auto first = files->publish("abc", ConfigPublishMode::replace, kCommitA);
    PHPVPN_REQUIRE_OK(first);
    PHPVPN_CHECK(first->state == ConfigPublishState::confirmed);
    PHPVPN_CHECK_EQ(modeOf(fixture.config()), mode_t(0600));
    auto read = files->readBounded(3);
    PHPVPN_REQUIRE_OK(read);
    PHPVPN_CHECK(read->found);
    PHPVPN_CHECK_EQ(read->bytes, std::string("abc"));
    PHPVPN_CHECK_EQ(read->sha256, std::string("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
    PHPVPN_CHECK(read->identity == first->identity);
    struct stat info {};
    PHPVPN_CHECK(::stat(fixture.config().c_str(), &info) == 0);
    PHPVPN_CHECK((read->identity == ConfigFileIdentity{static_cast<std::uint64_t>(info.st_dev),
                                                        static_cast<std::uint64_t>(info.st_ino)}));
    PHPVPN_REQUIRE_ERROR(files->readBounded(2), ErrorCode::config_too_large);
    // The confirmed journal stays until the next recovery or publication.
    const auto journal = readFile(fixture.path / "config.publish-journal");
    PHPVPN_CHECK(journal.starts_with("PHPVPN-CONFIG-1\n" + std::string(kCommitA) + "\n"));
    PHPVPN_CHECK(journal.ends_with("\nCONFIRMED\n"));
    PHPVPN_CHECK_EQ(modeOf(fixture.path / "config.publish-journal"), mode_t(0600));
    auto existing = files->publish("def", ConfigPublishMode::createIfMissing, kCommitB);
    PHPVPN_REQUIRE_OK(existing);
    PHPVPN_CHECK(existing->state == ConfigPublishState::alreadyExists);
    auto second = files->publish("new", ConfigPublishMode::replace, kCommitB);
    PHPVPN_REQUIRE_OK(second);
    PHPVPN_CHECK(second->state == ConfigPublishState::confirmed);
    PHPVPN_CHECK_EQ(readFile(fixture.config()), std::string("new"));
    PHPVPN_CHECK_EQ(readFile(fixture.path / "config.previous"), std::string("abc"));
    PHPVPN_CHECK((names(fixture.path) ==
                  std::vector<std::string>{"config.json", "config.previous", "config.publish-journal"}));
    auto recovered = files->recover();
    PHPVPN_REQUIRE_OK(recovered);
    PHPVPN_CHECK(recovered->state == ConfigRecoveryState::confirmed);
    PHPVPN_CHECK_EQ(recovered->commitId, std::string(kCommitB));
    PHPVPN_CHECK((names(fixture.path) == std::vector<std::string>{"config.json"}));
    auto clean = files->recover();
    PHPVPN_REQUIRE_OK(clean);
    PHPVPN_CHECK(clean->state == ConfigRecoveryState::clean);
}

PHPVPN_TEST(config_files_linux, repeated_commit_is_idempotent_and_never_replayed) {
    TempDirectory fixture;
    auto made = createLinuxConfigFiles(fixture.config(), policy());
    PHPVPN_REQUIRE_OK(made);
    auto first = made.value()->publish("abc", ConfigPublishMode::replace, kCommitA);
    PHPVPN_REQUIRE_OK(first);
    auto again = made.value()->publish("abc", ConfigPublishMode::replace, kCommitA);
    PHPVPN_REQUIRE_OK(again);
    PHPVPN_CHECK(again->state == ConfigPublishState::confirmed);
    PHPVPN_CHECK(again->identity == first->identity);
    PHPVPN_CHECK_EQ(again->sha256, first->sha256);
    PHPVPN_REQUIRE_OK(made.value()->publish("abc", ConfigPublishMode::replace, kCommitB));
    PHPVPN_REQUIRE_ERROR(made.value()->publish("other", ConfigPublishMode::replace, kCommitB),
                         ErrorCode::invalid_argument);
    PHPVPN_REQUIRE_ERROR(made.value()->publish("abc", ConfigPublishMode::replace, "not-a-uuid"),
                         ErrorCode::invalid_argument);
    PHPVPN_REQUIRE_ERROR(made.value()->publish(std::string(kMaxConfigFileBytes + 1, 'x'),
                                               ConfigPublishMode::replace, kCommitC),
                         ErrorCode::config_too_large);
}

PHPVPN_TEST(config_files_linux, exact_file_limit_and_no_full_read_for_oversize) {
    TempDirectory fixture;
    std::size_t chunks = 0;
    auto made = createLinuxConfigFiles(fixture.config(), policy([&](Stage stage) {
        if (stage == Stage::readChunk) ++chunks;
        return Action::proceed;
    }));
    PHPVPN_REQUIRE_OK(made);
    const std::string full(kMaxConfigFileBytes, 'x');
    PHPVPN_REQUIRE_OK(made.value()->publish(full, ConfigPublishMode::replace, kCommitA));
    auto read = made.value()->readBounded(kMaxConfigFileBytes);
    PHPVPN_REQUIRE_OK(read);
    PHPVPN_CHECK_EQ(read->bytes.size(), kMaxConfigFileBytes);
    PHPVPN_CHECK_EQ(chunks, std::size_t(32));
    chunks = 0;
    PHPVPN_REQUIRE_ERROR(made.value()->readBounded(kMaxConfigFileBytes - 1), ErrorCode::config_too_large);
    PHPVPN_CHECK_EQ(chunks, std::size_t(0));
    PHPVPN_REQUIRE_ERROR(made.value()->readBounded(kMaxConfigFileBytes + 1), ErrorCode::invalid_argument);
    PHPVPN_REQUIRE_ERROR(made.value()->publish(full + "x", ConfigPublishMode::replace, kCommitB),
                         ErrorCode::config_too_large);
}

PHPVPN_TEST(config_files_linux, growth_during_read_is_too_large_not_truncated) {
    TempDirectory fixture;
    bool grow = false;
    auto made = createLinuxConfigFiles(fixture.config(), policy([&](Stage stage) {
        if (grow && stage == Stage::readChunk) {
            grow = false;
            const int fd = ::open(fixture.config().c_str(), O_WRONLY | O_APPEND | O_CLOEXEC);
            PHPVPN_CHECK(fd >= 0);
            PHPVPN_CHECK(::write(fd, "g", 1) == 1);
            ::close(fd);
        }
        return Action::proceed;
    }));
    PHPVPN_REQUIRE_OK(made);
    PHPVPN_REQUIRE_OK(made.value()->publish(std::string(32768, 'a'), ConfigPublishMode::replace, kCommitA));
    grow = true;
    PHPVPN_REQUIRE_ERROR(made.value()->readBounded(32768), ErrorCode::config_too_large);
    auto grown = made.value()->readBounded(32769);
    PHPVPN_REQUIRE_OK(grown);
    PHPVPN_CHECK_EQ(grown->bytes, std::string(32768, 'a') + "g");
}

PHPVPN_TEST(config_files_linux, unsafe_targets_are_unreadable_and_never_written_through) {
    TempDirectory fixture;
    auto made = createLinuxConfigFiles(fixture.config(), policy());
    PHPVPN_REQUIRE_OK(made);
    auto& files = made.value();
    const auto outside = fixture.path / "outside";
    writeFile(outside, "unrelated");

    // Symbolic link at the fixed name.
    PHPVPN_CHECK(::symlink(outside.c_str(), fixture.config().c_str()) == 0);
    PHPVPN_REQUIRE_ERROR(files->readBounded(kMaxConfigFileBytes), ErrorCode::config_unreadable);
    PHPVPN_REQUIRE_ERROR(files->publish("new", ConfigPublishMode::replace, kCommitA), ErrorCode::config_write_failed);
    PHPVPN_CHECK_EQ(readFile(outside), std::string("unrelated"));
    PHPVPN_CHECK(std::filesystem::is_symlink(fixture.config()));
    PHPVPN_CHECK(::unlink(fixture.config().c_str()) == 0);

    // A second hard link outside the publication protocol.
    PHPVPN_CHECK(::link(outside.c_str(), fixture.config().c_str()) == 0);
    PHPVPN_REQUIRE_ERROR(files->readBounded(kMaxConfigFileBytes), ErrorCode::config_unreadable);
    PHPVPN_REQUIRE_ERROR(files->publish("new", ConfigPublishMode::replace, kCommitA), ErrorCode::config_write_failed);
    PHPVPN_CHECK_EQ(readFile(outside), std::string("unrelated"));
    PHPVPN_CHECK(::unlink(fixture.config().c_str()) == 0);

    // FIFO and directory at the fixed name, neither may block or be read.
    PHPVPN_CHECK(::mkfifo(fixture.config().c_str(), 0600) == 0);
    PHPVPN_REQUIRE_ERROR(files->readBounded(kMaxConfigFileBytes), ErrorCode::config_unreadable);
    PHPVPN_REQUIRE_ERROR(files->publish("new", ConfigPublishMode::replace, kCommitA), ErrorCode::config_write_failed);
    PHPVPN_CHECK(::unlink(fixture.config().c_str()) == 0);
    PHPVPN_CHECK(::mkdir(fixture.config().c_str(), 0700) == 0);
    PHPVPN_REQUIRE_ERROR(files->readBounded(kMaxConfigFileBytes), ErrorCode::config_unreadable);
    PHPVPN_REQUIRE_ERROR(files->publish("new", ConfigPublishMode::replace, kCommitA), ErrorCode::config_write_failed);
    PHPVPN_CHECK(::rmdir(fixture.config().c_str()) == 0);

    // Group or other write permission.
    for (const mode_t unsafe : {mode_t(0620), mode_t(0602), mode_t(0666)}) {
        writeFile(fixture.config(), "writable", unsafe);
        PHPVPN_REQUIRE_ERROR(files->readBounded(kMaxConfigFileBytes), ErrorCode::config_unreadable);
        PHPVPN_REQUIRE_ERROR(files->publish("new", ConfigPublishMode::replace, kCommitA),
                             ErrorCode::config_write_failed);
        PHPVPN_CHECK_EQ(readFile(fixture.config()), std::string("writable"));
    }
    // Read-only for group and others is acceptable.
    PHPVPN_CHECK(::chmod(fixture.config().c_str(), 0644) == 0);
    PHPVPN_REQUIRE_OK(files->readBounded(kMaxConfigFileBytes));

    if (::geteuid() == 0) {
        PHPVPN_CHECK(::chown(fixture.config().c_str(), 65534, 65534) == 0);
        PHPVPN_REQUIRE_ERROR(files->readBounded(kMaxConfigFileBytes), ErrorCode::config_unreadable);
        PHPVPN_REQUIRE_ERROR(files->publish("new", ConfigPublishMode::replace, kCommitA),
                             ErrorCode::config_write_failed);
    } else {
        // A file the caller cannot open is unreadable, not missing.
        PHPVPN_CHECK(::chmod(fixture.config().c_str(), 0000) == 0);
        PHPVPN_REQUIRE_ERROR(files->readBounded(kMaxConfigFileBytes), ErrorCode::config_unreadable);
        std::cout << "[ NOT RUN ] foreign file owner needs root to chown, covered by the expected owner policy\n";
    }
    PHPVPN_CHECK_EQ(readFile(outside), std::string("unrelated"));
    PHPVPN_CHECK(!std::filesystem::exists(fixture.path / "config.publish-journal"));
}

PHPVPN_TEST(config_files_linux, wrong_directory_mode_or_moved_directory_blocks_publication) {
    TempDirectory fixture;
    const auto directory = fixture.path / "etc";
    PHPVPN_CHECK(::mkdir(directory.c_str(), 0700) == 0);
    auto made = createLinuxConfigFiles(directory / "config.json", policy());
    PHPVPN_REQUIRE_OK(made);
    auto& files = made.value();
    PHPVPN_REQUIRE_OK(files->publish("first", ConfigPublishMode::replace, kCommitA));
    const auto journal = readFile(directory / "config.publish-journal");
    PHPVPN_CHECK(::chmod(directory.c_str(), 0770) == 0);
    PHPVPN_REQUIRE_ERROR(files->publish("blocked", ConfigPublishMode::replace, kCommitB),
                         ErrorCode::config_directory_security_invalid);
    PHPVPN_REQUIRE_ERROR(files->recover(), ErrorCode::config_directory_security_invalid);
    PHPVPN_CHECK_EQ(readFile(directory / "config.json"), std::string("first"));
    PHPVPN_CHECK_EQ(readFile(directory / "config.publish-journal"), journal);
    PHPVPN_CHECK(::chmod(directory.c_str(), 0700) == 0);
    PHPVPN_REQUIRE_OK(files->publish("second", ConfigPublishMode::replace, kCommitB));

    // The pinned directory is renamed away and a new one takes its name.
    const auto moved = fixture.path / "moved";
    PHPVPN_CHECK(::rename(directory.c_str(), moved.c_str()) == 0);
    PHPVPN_CHECK(::mkdir(directory.c_str(), 0700) == 0);
    PHPVPN_REQUIRE_ERROR(files->publish("third", ConfigPublishMode::replace, kCommitC),
                         ErrorCode::config_directory_security_invalid);
    PHPVPN_CHECK(names(directory).empty());
    PHPVPN_CHECK_EQ(readFile(moved / "config.json"), std::string("second"));
}

PHPVPN_TEST(config_files_linux, directory_stat_failure_is_not_a_policy_mismatch) {
    TempDirectory fixture;
    for (unsigned failureAt : {1u, 2u, 3u}) {
        unsigned reads = 0;
        auto backend = createLinuxConfigFiles(fixture.config(), policy([&](Stage stage) {
            if (stage == Stage::beforeDirectorySecurityRead && ++reads == failureAt) return Action::fail;
            return Action::proceed;
        }));
        if (failureAt == 1) {
            PHPVPN_REQUIRE_ERROR(backend, ErrorCode::config_unreadable);
        } else {
            PHPVPN_REQUIRE_OK(backend);
            PHPVPN_REQUIRE_ERROR(backend.value()->publish("template", ConfigPublishMode::createIfMissing, kCommitA),
                                 ErrorCode::config_unreadable);
        }
        PHPVPN_CHECK_EQ(reads, failureAt);
        PHPVPN_CHECK(names(fixture.path).empty());
    }
}

PHPVPN_TEST(config_files_linux, injected_prepublication_failures_preserve_original) {
    for (const auto failure : {Stage::stageCreated, Stage::beforeJournalFlush, Stage::journalFlushed,
                               Stage::stageWritten, Stage::beforeStageFlush, Stage::backupLinked,
                               Stage::beforePublish}) {
        TempDirectory fixture;
        Injector injector{failure, Action::fail};
        auto made = createLinuxConfigFiles(fixture.config(), policy(injector.hook()));
        PHPVPN_REQUIRE_OK(made);
        PHPVPN_REQUIRE_OK(made.value()->publish("old", ConfigPublishMode::replace, kCommitA));
        PHPVPN_REQUIRE_OK(made.value()->recover());
        injector.armed = true;
        PHPVPN_REQUIRE_ERROR(made.value()->publish("secret", ConfigPublishMode::replace, kCommitB),
                             ErrorCode::config_write_failed);
        PHPVPN_CHECK(injector.fired);
        PHPVPN_CHECK_EQ(readFile(fixture.config()), std::string("old"));
        PHPVPN_CHECK((names(fixture.path) == std::vector<std::string>{"config.json"}));
        struct stat info {};
        PHPVPN_CHECK(::stat(fixture.config().c_str(), &info) == 0 && info.st_nlink == 1);
    }
}

PHPVPN_TEST(config_files_linux, failures_around_publication_resolve_without_replaying) {
    // Failure after the verified stage: nothing published, recovery says so.
    // Failures after the rename: published, recovery confirms by identity and hash.
    for (const auto failure : {Stage::stageVerified, Stage::afterPublish, Stage::afterDirectorySync,
                               Stage::beforeConfirm}) {
        TempDirectory fixture;
        {
            Injector injector{failure, Action::fail, true};
            auto made = createLinuxConfigFiles(fixture.config(), policy(injector.hook()));
            PHPVPN_REQUIRE_OK(made);
            auto result = made.value()->publish("new", ConfigPublishMode::replace, kCommitA);
            PHPVPN_REQUIRE_OK(result);
            PHPVPN_CHECK(result->state == ConfigPublishState::unknown);
            PHPVPN_CHECK_EQ(result->commitId, std::string(kCommitA));
        }
        auto reopened = createLinuxConfigFiles(fixture.config(), policy());
        PHPVPN_REQUIRE_OK(reopened);
        const bool published = failure != Stage::stageVerified;
        auto recovered = reopened.value()->recover();
        PHPVPN_REQUIRE_OK(recovered);
        PHPVPN_CHECK(recovered->state ==
                     (published ? ConfigRecoveryState::confirmed : ConfigRecoveryState::notPublished));
        PHPVPN_CHECK_EQ(recovered->commitId, std::string(kCommitA));
        auto read = reopened.value()->readBounded(kMaxConfigFileBytes);
        PHPVPN_REQUIRE_OK(read);
        PHPVPN_CHECK_EQ(read->found, published);
        PHPVPN_CHECK(names(fixture.path) ==
                     (published ? std::vector<std::string>{"config.json"} : std::vector<std::string>{}));
    }
}

PHPVPN_TEST(config_files_linux, crash_between_every_step_recovers_consistently) {
    const Stage stages[] = {Stage::stageCreated, Stage::beforeJournalFlush, Stage::journalFlushed,
                            Stage::stageWritten, Stage::beforeStageFlush, Stage::stageVerified,
                            Stage::backupLinked, Stage::beforePublish, Stage::afterPublish,
                            Stage::afterDirectorySync, Stage::beforeConfirm};
    for (const bool withOriginal : {false, true}) {
        for (const auto crash : stages) {
            if (crash == Stage::backupLinked && !withOriginal) continue;
            TempDirectory fixture;
            if (withOriginal) {
                auto made = createLinuxConfigFiles(fixture.config(), policy());
                PHPVPN_REQUIRE_OK(made);
                PHPVPN_REQUIRE_OK(made.value()->publish("old", ConfigPublishMode::replace, kCommitA));
                PHPVPN_REQUIRE_OK(made.value()->recover());
            }
            {
                Injector injector{crash, Action::crash, true};
                auto made = createLinuxConfigFiles(fixture.config(), policy(injector.hook()));
                PHPVPN_REQUIRE_OK(made);
                PHPVPN_REQUIRE_ERROR(made.value()->publish("new", ConfigPublishMode::replace, kCommitB),
                                     ErrorCode::internal);
                PHPVPN_CHECK(injector.fired);
            }
            const bool published = crash == Stage::afterPublish || crash == Stage::afterDirectorySync ||
                crash == Stage::beforeConfirm;
            const std::string expectedTarget = published ? "new" : (withOriginal ? "old" : "READ ERROR");
            // Readers never see a partial file, even before recovery.
            PHPVPN_CHECK_EQ(readFile(fixture.config()), expectedTarget);

            // A crash inside the recovery cleanup must be repeatable as well.
            {
                Injector injector{Stage::beforeRecoveryCleanup, Action::crash, true};
                auto made = createLinuxConfigFiles(fixture.config(), policy(injector.hook()));
                PHPVPN_REQUIRE_OK(made);
                auto interrupted = made.value()->recover();
                PHPVPN_CHECK(crash == Stage::stageCreated ? interrupted.ok() : !interrupted.ok());
            }
            auto made = createLinuxConfigFiles(fixture.config(), policy());
            PHPVPN_REQUIRE_OK(made);
            auto& files = made.value();
            if (crash == Stage::stageCreated) {
                // No journal yet: only an empty protected stage may remain.
                auto recovered = files->recover();
                PHPVPN_REQUIRE_OK(recovered);
                PHPVPN_CHECK(recovered->state == ConfigRecoveryState::clean);
                std::size_t stray = 0;
                for (const auto& name : names(fixture.path)) {
                    if (!name.starts_with("config.stage.")) continue;
                    ++stray;
                    PHPVPN_CHECK(name.ends_with(".tmp"));
                    PHPVPN_CHECK_EQ(std::filesystem::file_size(fixture.path / name), std::uintmax_t(0));
                    PHPVPN_CHECK_EQ(modeOf(fixture.path / name), mode_t(0600));
                }
                PHPVPN_CHECK_EQ(stray, std::size_t(1));
            } else if (published) {
                // The same commit is reported, never republished.
                auto repeated = files->publish("new", ConfigPublishMode::replace, kCommitB);
                PHPVPN_REQUIRE_OK(repeated);
                PHPVPN_CHECK(repeated->state == ConfigPublishState::confirmed);
                auto read = files->readBounded(kMaxConfigFileBytes);
                PHPVPN_REQUIRE_OK(read);
                PHPVPN_CHECK(read->identity == repeated->identity);
                PHPVPN_CHECK((names(fixture.path) == std::vector<std::string>{"config.json"}));
            } else {
                // Recovery never completes an unpublished commit, and the same
                // commit id is refused instead of silently retried.
                PHPVPN_REQUIRE_ERROR(files->publish("new", ConfigPublishMode::replace, kCommitB),
                                     ErrorCode::config_write_failed);
                PHPVPN_CHECK_EQ(readFile(fixture.config()), expectedTarget);
                PHPVPN_CHECK(names(fixture.path) == (withOriginal ? std::vector<std::string>{"config.json"}
                                                                  : std::vector<std::string>{}));
            }
            auto next = files->publish("final", ConfigPublishMode::replace, kCommitC);
            PHPVPN_REQUIRE_OK(next);
            PHPVPN_CHECK(next->state == ConfigPublishState::confirmed);
            PHPVPN_CHECK_EQ(readFile(fixture.config()), std::string("final"));
        }
    }
}

PHPVPN_TEST(config_files_linux, notpublished_recovery_reports_before_cleanup) {
    TempDirectory fixture;
    {
        Injector injector{Stage::beforePublish, Action::crash, true};
        auto made = createLinuxConfigFiles(fixture.config(), policy(injector.hook()));
        PHPVPN_REQUIRE_OK(made);
        PHPVPN_REQUIRE_ERROR(made.value()->publish("new", ConfigPublishMode::createIfMissing, kCommitA),
                             ErrorCode::internal);
    }
    auto made = createLinuxConfigFiles(fixture.config(), policy());
    PHPVPN_REQUIRE_OK(made);
    auto recovered = made.value()->recover();
    PHPVPN_REQUIRE_OK(recovered);
    PHPVPN_CHECK(recovered->state == ConfigRecoveryState::notPublished);
    PHPVPN_CHECK_EQ(recovered->commitId, std::string(kCommitA));
    PHPVPN_CHECK(names(fixture.path).empty());
}

PHPVPN_TEST(config_files_linux, foreign_backup_and_planted_links_are_preserved) {
    TempDirectory fixture;
    auto made = createLinuxConfigFiles(fixture.config(), policy());
    PHPVPN_REQUIRE_OK(made);
    PHPVPN_REQUIRE_OK(made.value()->publish("old", ConfigPublishMode::replace, kCommitA));
    PHPVPN_REQUIRE_OK(made.value()->recover());
    writeFile(fixture.path / "config.previous", "foreign");
    auto result = made.value()->publish("new", ConfigPublishMode::replace, kCommitB);
    PHPVPN_CHECK(!result || result->state != ConfigPublishState::confirmed);
    PHPVPN_CHECK_EQ(readFile(fixture.path / "config.previous"), std::string("foreign"));
    PHPVPN_CHECK_EQ(readFile(fixture.config()), std::string("old"));

    TempDirectory second;
    auto other = createLinuxConfigFiles(second.config(), policy());
    PHPVPN_REQUIRE_OK(other);
    PHPVPN_REQUIRE_OK(other.value()->publish("old", ConfigPublishMode::replace, kCommitA));
    PHPVPN_REQUIRE_OK(other.value()->recover());
    const auto outside = second.path / "outside";
    writeFile(outside, "unrelated");
    PHPVPN_CHECK(::symlink(outside.c_str(), (second.path / "config.previous").c_str()) == 0);
    auto linked = other.value()->publish("new", ConfigPublishMode::replace, kCommitB);
    PHPVPN_CHECK(!linked || linked->state != ConfigPublishState::confirmed);
    PHPVPN_CHECK_EQ(readFile(outside), std::string("unrelated"));
    PHPVPN_CHECK_EQ(readFile(second.config()), std::string("old"));
    PHPVPN_CHECK(std::filesystem::is_symlink(second.path / "config.previous"));
}

PHPVPN_TEST(config_files_linux, corrupt_or_linked_journal_fails_closed) {
    TempDirectory fixture;
    auto made = createLinuxConfigFiles(fixture.config(), policy());
    PHPVPN_REQUIRE_OK(made);
    PHPVPN_REQUIRE_OK(made.value()->publish("original", ConfigPublishMode::replace, kCommitA));
    PHPVPN_CHECK(::unlink(fixture.config().c_str()) == 0);
    const auto journal = fixture.path / "config.publish-journal";
    writeFile(journal, "UNKNOWN JOURNAL VERSION\n");
    PHPVPN_REQUIRE_ERROR(made.value()->recover(), ErrorCode::journal_corrupt);
    PHPVPN_REQUIRE_ERROR(made.value()->publish("default", ConfigPublishMode::createIfMissing, kCommitB),
                         ErrorCode::journal_corrupt);
    PHPVPN_CHECK(!std::filesystem::exists(fixture.config()));
    PHPVPN_CHECK_EQ(readFile(journal), std::string("UNKNOWN JOURNAL VERSION\n"));
    writeFile(journal, std::string(2048, 'j'));
    PHPVPN_REQUIRE_ERROR(made.value()->recover(), ErrorCode::config_too_large);

    const auto outside = fixture.path / "outside";
    writeFile(outside, "PHPVPN-CONFIG-1\n");
    PHPVPN_CHECK(::unlink(journal.c_str()) == 0);
    PHPVPN_CHECK(::symlink(outside.c_str(), journal.c_str()) == 0);
    PHPVPN_REQUIRE_ERROR(made.value()->recover(), ErrorCode::journal_ambiguous);
    PHPVPN_CHECK(::unlink(journal.c_str()) == 0);
    writeFile(journal, "PHPVPN-CONFIG-1\n", 0666);
    PHPVPN_REQUIRE_ERROR(made.value()->recover(), ErrorCode::journal_ambiguous);
    PHPVPN_CHECK_EQ(readFile(outside), std::string("PHPVPN-CONFIG-1\n"));
}

PHPVPN_TEST(config_files_linux, confirmed_receipt_survives_external_change_and_allows_later_save) {
    TempDirectory fixture;
    auto made = createLinuxConfigFiles(fixture.config(), policy());
    PHPVPN_REQUIRE_OK(made);
    PHPVPN_REQUIRE_OK(made.value()->publish("first", ConfigPublishMode::replace, kCommitA));
    PHPVPN_CHECK(::unlink(fixture.config().c_str()) == 0);
    writeFile(fixture.config(), "external edit");
    auto historical = made.value()->recover();
    PHPVPN_REQUIRE_OK(historical);
    PHPVPN_CHECK(historical->state == ConfigRecoveryState::confirmed);
    PHPVPN_CHECK_EQ(historical->commitId, std::string(kCommitA));
    PHPVPN_CHECK_EQ(readFile(fixture.config()), std::string("external edit"));
    auto next = made.value()->publish("last writer", ConfigPublishMode::replace, kCommitB);
    PHPVPN_REQUIRE_OK(next);
    PHPVPN_CHECK(next->state == ConfigPublishState::confirmed);
    PHPVPN_CHECK_EQ(readFile(fixture.config()), std::string("last writer"));
}

PHPVPN_TEST(config_files_linux, unrelated_target_after_unconfirmed_commit_stays_unknown) {
    TempDirectory fixture;
    auto made = createLinuxConfigFiles(fixture.config(),
        policy([](Stage stage) { return stage == Stage::afterPublish ? Action::fail : Action::proceed; }));
    PHPVPN_REQUIRE_OK(made);
    auto published = made.value()->publish("uncertain", ConfigPublishMode::replace, kCommitA);
    PHPVPN_REQUIRE_OK(published);
    PHPVPN_CHECK(published->state == ConfigPublishState::unknown);
    const auto moved = fixture.path / "external-move";
    PHPVPN_CHECK(::rename(fixture.config().c_str(), moved.c_str()) == 0);
    writeFile(fixture.config(), "unrelated");
    auto recovery = made.value()->recover();
    PHPVPN_REQUIRE_OK(recovery);
    PHPVPN_CHECK(recovery->state == ConfigRecoveryState::unknown);
    PHPVPN_REQUIRE_ERROR(made.value()->publish("next", ConfigPublishMode::replace, kCommitB),
                         ErrorCode::journal_ambiguous);
    PHPVPN_CHECK_EQ(readFile(fixture.config()), std::string("unrelated"));
    PHPVPN_CHECK_EQ(readFile(moved), std::string("uncertain"));
}

PHPVPN_TEST(config_files_linux, create_if_missing_loses_to_external_writer) {
    TempDirectory fixture;
    bool injected = false;
    auto made = createLinuxConfigFiles(fixture.config(), policy([&](Stage stage) {
        if (!injected && stage == Stage::beforePublish) {
            injected = true;
            writeFile(fixture.config(), "external");
        }
        return Action::proceed;
    }));
    PHPVPN_REQUIRE_OK(made);
    auto result = made.value()->publish("template", ConfigPublishMode::createIfMissing, kCommitA);
    PHPVPN_REQUIRE_OK(result);
    PHPVPN_CHECK(result->state == ConfigPublishState::alreadyExists);
    PHPVPN_CHECK_EQ(readFile(fixture.config()), std::string("external"));
    PHPVPN_CHECK((names(fixture.path) == std::vector<std::string>{"config.json"}));
}

PHPVPN_TEST(config_files_linux, create_if_missing_under_concurrency_publishes_once) {
    const char* commits[] = {"00000000-0000-4000-8000-000000000011", "00000000-0000-4000-8000-000000000012",
                             "00000000-0000-4000-8000-000000000013", "00000000-0000-4000-8000-000000000014",
                             "00000000-0000-4000-8000-000000000015", "00000000-0000-4000-8000-000000000016",
                             "00000000-0000-4000-8000-000000000017", "00000000-0000-4000-8000-000000000018"};
    // One backend shared by many threads: exactly one creation wins.
    {
        TempDirectory fixture;
        auto made = createLinuxConfigFiles(fixture.config(), policy());
        PHPVPN_REQUIRE_OK(made);
        std::atomic<int> confirmed{0}, existing{0}, other{0};
        std::mutex winnerLock;
        std::string winner;
        std::vector<std::thread> threads;
        for (int i = 0; i < 8; ++i) {
            threads.emplace_back([&, i] {
                const std::string content = "candidate " + std::to_string(i);
                auto result = made.value()->publish(content, ConfigPublishMode::createIfMissing, commits[i]);
                if (result && result->state == ConfigPublishState::confirmed) {
                    ++confirmed;
                    std::lock_guard lock(winnerLock);
                    winner = content;
                } else if (result && result->state == ConfigPublishState::alreadyExists) {
                    ++existing;
                } else {
                    ++other;
                }
            });
        }
        for (auto& thread : threads) thread.join();
        PHPVPN_CHECK_EQ(confirmed.load(), 1);
        PHPVPN_CHECK_EQ(existing.load(), 7);
        PHPVPN_CHECK_EQ(other.load(), 0);
        PHPVPN_CHECK_EQ(readFile(fixture.config()), winner);
    }
    // Independent backends (separate processes in practice): never two winners
    // and never a target that differs from the confirmed publication.
    for (int round = 0; round < 5; ++round) {
        TempDirectory fixture;
        std::vector<Files> backends;
        for (int i = 0; i < 4; ++i) {
            auto made = createLinuxConfigFiles(fixture.config(), policy());
            PHPVPN_REQUIRE_OK(made);
            backends.push_back(std::move(made).value());
        }
        std::atomic<int> confirmed{0};
        std::mutex winnerLock;
        std::string winner;
        std::vector<std::thread> threads;
        for (int i = 0; i < 8; ++i) {
            threads.emplace_back([&, i] {
                const std::string content = "candidate " + std::to_string(i);
                auto result = backends[i % 4]->publish(content, ConfigPublishMode::createIfMissing, commits[i]);
                if (result && result->state == ConfigPublishState::confirmed) {
                    ++confirmed;
                    std::lock_guard lock(winnerLock);
                    winner = content;
                }
            });
        }
        for (auto& thread : threads) thread.join();
        PHPVPN_CHECK(confirmed.load() <= 1);
        const auto target = readFile(fixture.config());
        if (confirmed.load() == 1) PHPVPN_CHECK_EQ(target, winner);
        PHPVPN_CHECK(target == "READ ERROR" || target.starts_with("candidate "));
    }
}

PHPVPN_TEST(config_files_linux, config_store_over_the_linux_backend) {
    TempDirectory fixture;
    auto crypto = createPlatformCryptoProvider();
    PHPVPN_REQUIRE_OK(crypto);
    auto backend = createLinuxConfigFiles(fixture.config(), policy());
    PHPVPN_REQUIRE_OK(backend);
    ConfigStore store(*crypto.value(), std::move(backend).value());
    PHPVPN_REQUIRE_OK(store.recover());
    auto initial = store.ensureDefaultAtStartup();
    PHPVPN_REQUIRE_OK(initial);
    PHPVPN_CHECK_EQ(initial.value(), StartupConfigState::created);
    auto snapshot = store.read();
    PHPVPN_REQUIRE_OK(snapshot);
    PHPVPN_CHECK_EQ(*snapshot->document, makeDefaultEditorDocument());
    const auto customized = makeDefaultEditorDocument() + "\n\n";
    PHPVPN_REQUIRE_OK(store.save(customized, kCommitA));
    PHPVPN_CHECK(std::filesystem::exists(fixture.path / "config.previous"));
    PHPVPN_CHECK(std::filesystem::exists(fixture.path / "config.publish-journal"));
    PHPVPN_REQUIRE_OK(store.recover());
    auto repeated = store.ensureDefaultAtStartup();
    PHPVPN_REQUIRE_OK(repeated);
    PHPVPN_CHECK_EQ(repeated.value(), StartupConfigState::existing);
    snapshot = store.read();
    PHPVPN_REQUIRE_OK(snapshot);
    PHPVPN_CHECK_EQ(*snapshot->document, customized);
    PHPVPN_CHECK(::unlink(fixture.config().c_str()) == 0);
    auto missing = store.read();
    PHPVPN_REQUIRE_OK(missing);
    PHPVPN_CHECK_EQ(missing->availability, ConfigAvailability::missing);
    auto recreated = store.createDefault();
    PHPVPN_REQUIRE_OK(recreated);
    PHPVPN_CHECK(recreated->created && recreated->snapshot.config != nullptr);
    PHPVPN_CHECK_EQ(*recreated->snapshot.document, makeDefaultEditorDocument());
    PHPVPN_CHECK_EQ(modeOf(fixture.config()), mode_t(0600));
}

PHPVPN_TEST(durable_file_linux, replaces_atomically_with_private_mode_and_no_leftovers) {
    TempDirectory fixture;
    const auto target = fixture.path / "state" / "nested" / "ownership.json";
    PHPVPN_REQUIRE_OK(durableReplaceFile(target, "{\"first\":true}"));
    PHPVPN_CHECK_EQ(readFile(target), std::string("{\"first\":true}"));
    PHPVPN_CHECK_EQ(modeOf(target), mode_t(0600));
    PHPVPN_CHECK_EQ(modeOf(fixture.path / "state"), mode_t(0700));
    PHPVPN_REQUIRE_OK(durableReplaceFile(target, "second"));
    PHPVPN_CHECK_EQ(readFile(target), std::string("second"));
    PHPVPN_REQUIRE_OK(durableReplaceFile(target, ""));
    PHPVPN_CHECK_EQ(readFile(target), std::string(""));
    PHPVPN_CHECK((names(target.parent_path()) == std::vector<std::string>{"ownership.json"}));

    // A stale temporary from a crash, even a planted link, is removed without
    // touching its referent.
    const auto outside = fixture.path / "outside";
    writeFile(outside, "unrelated");
    writeFile(target.parent_path() / "ownership.json.tmp", "stale");
    PHPVPN_REQUIRE_OK(durableReplaceFile(target, "third"));
    PHPVPN_CHECK(::symlink(outside.c_str(), (target.parent_path() / "ownership.json.tmp").c_str()) == 0);
    PHPVPN_REQUIRE_OK(durableReplaceFile(target, "fourth"));
    PHPVPN_CHECK_EQ(readFile(target), std::string("fourth"));
    PHPVPN_CHECK_EQ(readFile(outside), std::string("unrelated"));
    PHPVPN_CHECK((names(target.parent_path()) == std::vector<std::string>{"ownership.json"}));
}

PHPVPN_TEST(durable_file_linux, failures_are_config_write_failed_without_temporary) {
    TempDirectory fixture;
    // The parent is a regular file.
    writeFile(fixture.path / "file", "x");
    PHPVPN_REQUIRE_ERROR(durableReplaceFile(fixture.path / "file" / "ownership.json", "data"),
                         ErrorCode::config_write_failed);
    // The target is a non empty directory, the rename fails after the write.
    const auto directory = fixture.path / "ownership.json";
    PHPVPN_CHECK(::mkdir(directory.c_str(), 0700) == 0);
    writeFile(directory / "inside", "keep");
    PHPVPN_REQUIRE_ERROR(durableReplaceFile(directory, "data"), ErrorCode::config_write_failed);
    PHPVPN_CHECK_EQ(readFile(directory / "inside"), std::string("keep"));
    PHPVPN_CHECK((names(fixture.path) == std::vector<std::string>{"file", "ownership.json"}));
    // A directory in place of the temporary name cannot be cleared.
    const auto target = fixture.path / "journal.json";
    PHPVPN_CHECK(::mkdir((fixture.path / "journal.json.tmp").c_str(), 0700) == 0);
    PHPVPN_REQUIRE_ERROR(durableReplaceFile(target, "data"), ErrorCode::config_write_failed);
    PHPVPN_CHECK(!std::filesystem::exists(target));
}

#if defined(__linux__)
PHPVPN_TEST(paths_linux, fixed_production_paths_and_overrides) {
    auto production = resolveLinuxDirectories();
    PHPVPN_REQUIRE_OK(production);
    PHPVPN_CHECK(configFilePath(production.value()) == "/etc/phpvpn/config.json");
    PHPVPN_CHECK(journalFilePath(production.value()) == "/var/lib/phpvpn/ownership.json");
    PHPVPN_CHECK(controlSocketPath(production.value()) == "/run/phpvpn/control-v1.sock");
    PHPVPN_CHECK(serviceLockPath(production.value()) == "/run/phpvpn/service.lock");

    LinuxDirectoryOverrides overrides;
    overrides.stateDirectory = "/tmp/phpvpn-e2e/state";
    auto partial = resolveLinuxDirectories(overrides);
    PHPVPN_REQUIRE_OK(partial);
    PHPVPN_CHECK(partial->config == "/etc/phpvpn");
    PHPVPN_CHECK(journalFilePath(partial.value()) == "/tmp/phpvpn-e2e/state/ownership.json");
    PHPVPN_CHECK(partial->runtime == "/run/phpvpn");

    for (const char* invalid : {"relative/dir", "/", "/tmp/../etc", "/tmp/x/"}) {
        LinuxDirectoryOverrides bad;
        bad.runtimeDirectory = invalid;
        PHPVPN_REQUIRE_ERROR(resolveLinuxDirectories(bad), ErrorCode::invalid_argument);
    }
}
#endif

PHPVPN_TEST(paths_linux, service_directories_are_created_and_repaired_to_exact_modes) {
    TempDirectory fixture;
    LinuxDirectoryOverrides overrides{fixture.path / "etc", fixture.path / "state", fixture.path / "run",
                                      fixture.path / "log"};
    auto directories = resolveLinuxDirectories(overrides);
    PHPVPN_REQUIRE_OK(directories);
    const mode_t previous = ::umask(077);
    auto created = secureServiceDirectories(directories.value(), ::geteuid(), trustRoot());
    ::umask(previous);
    PHPVPN_REQUIRE_OK(created);
    PHPVPN_CHECK_EQ(modeOf(fixture.path / "etc"), mode_t(0700));
    PHPVPN_CHECK_EQ(modeOf(fixture.path / "state"), mode_t(0700));
    PHPVPN_CHECK_EQ(modeOf(fixture.path / "run"), mode_t(0755));
    PHPVPN_CHECK(::chmod((fixture.path / "etc").c_str(), 0777) == 0);
    PHPVPN_CHECK(::chmod((fixture.path / "run").c_str(), 02775) == 0);
    PHPVPN_REQUIRE_OK(secureServiceDirectories(directories.value(), ::geteuid(), trustRoot()));
    PHPVPN_CHECK_EQ(modeOf(fixture.path / "etc"), mode_t(0700));
    PHPVPN_CHECK_EQ(modeOf(fixture.path / "run"), mode_t(0755));
    // The repaired directory is accepted by the configuration backend.
    PHPVPN_REQUIRE_OK(createLinuxConfigFiles(configFilePath(directories.value()), policy()));
}

PHPVPN_TEST(paths_linux, symlink_foreign_owner_or_unsafe_ancestor_fail_closed) {
    TempDirectory fixture;
    const auto real = fixture.path / "real";
    PHPVPN_CHECK(::mkdir(real.c_str(), 0777) == 0);
    PHPVPN_CHECK(::chmod(real.c_str(), 0777) == 0);
    const auto link = fixture.path / "link";
    PHPVPN_CHECK(::symlink(real.c_str(), link.c_str()) == 0);
    PHPVPN_REQUIRE_ERROR(secureServiceDirectory(link, 0700, ::geteuid(), trustRoot()),
                         ErrorCode::config_directory_security_invalid);
    PHPVPN_CHECK_EQ(modeOf(real), mode_t(0777));
    PHPVPN_CHECK(std::filesystem::is_symlink(link));

    // Not a directory.
    writeFile(fixture.path / "file", "x");
    PHPVPN_REQUIRE_ERROR(secureServiceDirectory(fixture.path / "file", 0700, ::geteuid(), trustRoot()),
                         ErrorCode::config_directory_security_invalid);

    // Somebody else's directory is never taken over or repaired.
    PHPVPN_REQUIRE_ERROR(secureServiceDirectory(real, 0700, ::geteuid() + 1, trustRoot()),
                         ErrorCode::config_directory_security_invalid);
    PHPVPN_CHECK_EQ(modeOf(real), mode_t(0777));

    // A symbolic link or a world writable non sticky directory among the ancestors.
    PHPVPN_REQUIRE_ERROR(secureServiceDirectory(link / "phpvpn", 0700, ::geteuid(), trustRoot()),
                         ErrorCode::config_directory_security_invalid);
    PHPVPN_REQUIRE_ERROR(secureServiceDirectory(real / "phpvpn", 0700, ::geteuid(), trustRoot()),
                         ErrorCode::config_directory_security_invalid);
    PHPVPN_CHECK(!std::filesystem::exists(real / "phpvpn"));
    PHPVPN_CHECK(::chmod(real.c_str(), 01777) == 0);
    PHPVPN_REQUIRE_OK(secureServiceDirectory(real / "phpvpn", 0700, ::geteuid(), trustRoot()));
    PHPVPN_REQUIRE_ERROR(openTrustedDirectory(link / "phpvpn", ::geteuid(), trustRoot()),
                         ErrorCode::config_directory_security_invalid);
    PHPVPN_REQUIRE_OK(openTrustedDirectory(real / "phpvpn", ::geteuid(), trustRoot()));

    // The parent has to exist, only the last component is created.
    PHPVPN_REQUIRE_ERROR(
        secureServiceDirectory(fixture.path / "missing" / "phpvpn", 0700, ::geteuid(), trustRoot()),
        ErrorCode::config_unreadable);
    PHPVPN_REQUIRE_ERROR(secureServiceDirectory("relative", 0700, ::geteuid(), trustRoot()),
                         ErrorCode::invalid_argument);
}

}  // namespace phpvpn
