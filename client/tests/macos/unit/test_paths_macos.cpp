// macOS service directory tests (plan-macos-v001 step 1): the canonical
// production paths, override validation, the trusted directory walk and the
// startup normalization of the service directories. Everything runs without
// privileges; the expected owner is geteuid().
#include "harness.h"

#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

#include "core/ipc/ipc_protocol.h"
#include "platform/posix/paths_posix.h"

namespace phpvpn {
namespace {

// Private mkdtemp directory. The path is canonical: TMPDIR lies below /var, a
// symbolic link the trusted walk refuses.
struct Fixture {
    std::filesystem::path path;
    Fixture() {
        std::string pattern = (std::filesystem::temp_directory_path() / "phpvpn-paths-XXXXXX").string();
        if (::mkdtemp(pattern.data()) == nullptr) throw std::runtime_error("mkdtemp failed");
        path = std::filesystem::canonical(pattern);
    }
    ~Fixture() {
        std::error_code ignored;
        for (auto it = std::filesystem::recursive_directory_iterator(path, ignored);
             !ignored && it != std::filesystem::recursive_directory_iterator(); it.increment(ignored)) {
            if (it->is_directory(ignored) && !it->is_symlink(ignored)) ::chmod(it->path().c_str(), 0700);
        }
        std::filesystem::remove_all(path, ignored);
    }
    Fixture(const Fixture&) = delete;
    Fixture& operator=(const Fixture&) = delete;
};

mode_t modeOf(const std::filesystem::path& path) {
    struct stat info {};
    return ::lstat(path.c_str(), &info) == 0 ? (info.st_mode & 07777) : 0;
}

}  // namespace

PHPVPN_TEST(paths_macos, production_directories_are_the_canonical_paths) {
    auto production = resolvePosixDirectories();
    PHPVPN_REQUIRE_OK(production);
    PHPVPN_CHECK(production->config == "/Library/Application Support/phpvpn");
    PHPVPN_CHECK(production->state == "/private/var/db/phpvpn");
    PHPVPN_CHECK(production->runtime == "/private/var/run/phpvpn");
    PHPVPN_CHECK(production->logs == "/private/var/log/phpvpn");
    PHPVPN_CHECK(configFilePath(production.value()) == "/Library/Application Support/phpvpn/config.json");
    PHPVPN_CHECK(journalFilePath(production.value()) == "/private/var/db/phpvpn/ownership.json");
    PHPVPN_CHECK(controlSocketPath(production.value()) == "/private/var/run/phpvpn/control-v1.sock");
    PHPVPN_CHECK(controlSocketPath(production.value()) == std::filesystem::path(kIpcSocketPath));
    PHPVPN_CHECK(serviceLockPath(production.value()) == "/private/var/run/phpvpn/service.lock");
    PHPVPN_CHECK(logDirectoryPath(production.value()) == "/private/var/log/phpvpn");
    PHPVPN_CHECK_EQ(std::string_view(kServiceLogBaseName), std::string_view("phpvpn-service"));
    PHPVPN_CHECK_EQ(kPosixConfigDirectoryMode, mode_t(0700));
    PHPVPN_CHECK_EQ(kPosixStateDirectoryMode, mode_t(0700));
    PHPVPN_CHECK_EQ(kPosixRuntimeDirectoryMode, mode_t(0755));
    PHPVPN_CHECK_EQ(kPosixLogDirectoryMode, mode_t(0755));
}

PHPVPN_TEST(paths_macos, production_parents_pass_the_trusted_walk) {
    // The service creates only the last component, so every parent of the
    // production table must be acceptable to the root owned walk. /var is a
    // symbolic link and must be refused, which is why the paths are canonical.
    for (const char* parent : {"/Library/Application Support", "/private/var/db", "/private/var/run",
                               "/private/var/log"}) {
        auto trusted = openTrustedDirectory(parent, 0);
        if (!trusted) std::cout << "[ INFO ] untrusted parent " << parent << ": " << trusted.error().name() << "\n";
        PHPVPN_CHECK(trusted.ok());
    }
    struct stat info {};
    if (::lstat("/var", &info) == 0 && S_ISLNK(info.st_mode))
        PHPVPN_REQUIRE_ERROR(openTrustedDirectory("/var/db", 0), ErrorCode::config_directory_security_invalid);
}

PHPVPN_TEST(paths_macos, overrides_replace_whole_directories_and_are_validated) {
    PosixDirectoryOverrides overrides;
    overrides.stateDirectory = "/private/tmp/phpvpn-e2e/state";
    overrides.logDirectory = "/private/tmp/phpvpn-e2e/logs";
    auto partial = resolvePosixDirectories(overrides);
    PHPVPN_REQUIRE_OK(partial);
    PHPVPN_CHECK(partial->config == "/Library/Application Support/phpvpn");
    PHPVPN_CHECK(journalFilePath(partial.value()) == "/private/tmp/phpvpn-e2e/state/ownership.json");
    PHPVPN_CHECK(partial->runtime == "/private/var/run/phpvpn");
    PHPVPN_CHECK(logDirectoryPath(partial.value()) == "/private/tmp/phpvpn-e2e/logs");

    for (const char* invalid : {"relative/dir", "/", "/private/tmp/../etc", "/private/tmp/x/", "/private//tmp"}) {
        for (int member = 0; member < 4; ++member) {
            PosixDirectoryOverrides bad;
            if (member == 0) bad.configDirectory = invalid;
            if (member == 1) bad.stateDirectory = invalid;
            if (member == 2) bad.runtimeDirectory = invalid;
            if (member == 3) bad.logDirectory = invalid;
            PHPVPN_REQUIRE_ERROR(resolvePosixDirectories(bad), ErrorCode::invalid_argument);
        }
    }
}

PHPVPN_TEST(paths_macos, trusted_walk_refuses_links_and_group_writable_components) {
    Fixture fixture;
    const auto real = fixture.path / "real";
    PHPVPN_CHECK(::mkdir(real.c_str(), 0700) == 0);
    PHPVPN_CHECK(::chmod(real.c_str(), 0755) == 0);
    PHPVPN_CHECK(::mkdir((real / "phpvpn").c_str(), 0700) == 0);
    PHPVPN_REQUIRE_OK(openTrustedDirectory(real / "phpvpn", ::geteuid()));

    // A symbolic link component, also as the last component.
    const auto link = fixture.path / "link";
    PHPVPN_CHECK(::symlink(real.c_str(), link.c_str()) == 0);
    PHPVPN_REQUIRE_ERROR(openTrustedDirectory(link / "phpvpn", ::geteuid()),
                         ErrorCode::config_directory_security_invalid);
    PHPVPN_REQUIRE_ERROR(openTrustedDirectory(link, ::geteuid()), ErrorCode::config_directory_security_invalid);

    // A group writable component without the sticky bit. Only a root owned
    // wheel or daemon group component is exempt, so a root run moves the
    // fixture to the group staff (20) first.
    const auto shared = fixture.path / "shared";
    PHPVPN_CHECK(::mkdir(shared.c_str(), 0700) == 0);
    PHPVPN_CHECK(::mkdir((shared / "phpvpn").c_str(), 0700) == 0);
    if (::geteuid() == 0) PHPVPN_CHECK(::chown(shared.c_str(), 0, 20) == 0);
    PHPVPN_CHECK(::chmod(shared.c_str(), 0775) == 0);
    PHPVPN_REQUIRE_ERROR(openTrustedDirectory(shared / "phpvpn", ::geteuid()),
                         ErrorCode::config_directory_security_invalid);
    PHPVPN_REQUIRE_ERROR(secureServiceDirectory(shared / "created", 0700, ::geteuid()),
                         ErrorCode::config_directory_security_invalid);
    PHPVPN_CHECK(!std::filesystem::exists(shared / "created"));
    // World writable is refused as well, sticky makes either acceptable (/tmp).
    PHPVPN_CHECK(::chmod(shared.c_str(), 0757) == 0);
    PHPVPN_REQUIRE_ERROR(openTrustedDirectory(shared / "phpvpn", ::geteuid()),
                         ErrorCode::config_directory_security_invalid);
    PHPVPN_CHECK(::chmod(shared.c_str(), 01775) == 0);
    PHPVPN_REQUIRE_OK(openTrustedDirectory(shared / "phpvpn", ::geteuid()));

    // A foreign expected owner makes every fixture component untrusted.
    PHPVPN_REQUIRE_ERROR(openTrustedDirectory(real / "phpvpn", ::geteuid() + 1),
                         ErrorCode::config_directory_security_invalid);
    // Missing components are unreadable, relative paths invalid.
    PHPVPN_REQUIRE_ERROR(openTrustedDirectory(fixture.path / "missing", ::geteuid()), ErrorCode::config_unreadable);
    PHPVPN_REQUIRE_ERROR(openTrustedDirectory("relative", ::geteuid()), ErrorCode::invalid_argument);
}

PHPVPN_TEST(paths_macos, service_directories_are_created_and_repaired_to_exact_modes) {
    Fixture fixture;
    PosixDirectoryOverrides overrides{fixture.path / "config", fixture.path / "state", fixture.path / "run",
                                      fixture.path / "logs"};
    auto directories = resolvePosixDirectories(overrides);
    PHPVPN_REQUIRE_OK(directories);
    const mode_t previous = ::umask(077);
    auto created = secureServiceDirectories(directories.value(), ::geteuid());
    auto logs = secureServiceDirectory(directories->logs, kPosixLogDirectoryMode, ::geteuid());
    ::umask(previous);
    PHPVPN_REQUIRE_OK(created);
    PHPVPN_REQUIRE_OK(logs);
    PHPVPN_CHECK_EQ(modeOf(fixture.path / "config"), mode_t(0700));
    PHPVPN_CHECK_EQ(modeOf(fixture.path / "state"), mode_t(0700));
    PHPVPN_CHECK_EQ(modeOf(fixture.path / "run"), mode_t(0755));
    PHPVPN_CHECK_EQ(modeOf(fixture.path / "logs"), mode_t(0755));

    // Existing directories with a wrong mode are repaired.
    PHPVPN_CHECK(::chmod((fixture.path / "config").c_str(), 0777) == 0);
    PHPVPN_CHECK(::chmod((fixture.path / "run").c_str(), 0700) == 0);
    PHPVPN_CHECK(::chmod((fixture.path / "logs").c_str(), 0775) == 0);
    PHPVPN_REQUIRE_OK(secureServiceDirectories(directories.value(), ::geteuid()));
    PHPVPN_REQUIRE_OK(secureServiceDirectory(directories->logs, kPosixLogDirectoryMode, ::geteuid()));
    PHPVPN_CHECK_EQ(modeOf(fixture.path / "config"), mode_t(0700));
    PHPVPN_CHECK_EQ(modeOf(fixture.path / "run"), mode_t(0755));
    PHPVPN_CHECK_EQ(modeOf(fixture.path / "logs"), mode_t(0755));

    // Only the last component is created, a foreign owner is never taken over.
    PHPVPN_REQUIRE_ERROR(secureServiceDirectory(fixture.path / "missing" / "phpvpn", 0700, ::geteuid()),
                         ErrorCode::config_unreadable);
    PHPVPN_REQUIRE_ERROR(secureServiceDirectory(fixture.path / "config", 0700, ::geteuid() + 1),
                         ErrorCode::config_directory_security_invalid);
    PHPVPN_CHECK_EQ(modeOf(fixture.path / "config"), mode_t(0700));
}

}  // namespace phpvpn
