#include "harness.h"
#include <windows.h>
#include <algorithm>
#include <atomic>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <stdexcept>
#include "core/crypto/crypto_provider.h"

namespace phpvpn {
namespace {
class ProbeHandle {
public:
    HANDLE value = INVALID_HANDLE_VALUE;
    explicit ProbeHandle(HANDLE h = INVALID_HANDLE_VALUE) : value(h) {}
    ~ProbeHandle() { if (value != INVALID_HANDLE_VALUE) CloseHandle(value); }
    ProbeHandle(const ProbeHandle&) = delete;
    ProbeHandle& operator=(const ProbeHandle&) = delete;
};
struct ProbeDirectory {
    std::filesystem::path path;
    bool owned = false;
    ProbeDirectory() {
        auto crypto = createPlatformCryptoProvider();
        if (!crypto) throw std::runtime_error("test random provider unavailable");
        auto id = crypto.value()->randomUuidV4();
        if (!id) throw std::runtime_error("test random identity unavailable");
        path = std::filesystem::temp_directory_path() / ("phpvpn-config-files-" + id.value());
        owned = CreateDirectoryW(path.c_str(), nullptr) != FALSE;
        if (!owned) throw std::runtime_error("test directory could not be created exclusively");
    }
    ~ProbeDirectory() {
        std::error_code ignored;
        if (owned && path.is_absolute() && path.parent_path() == std::filesystem::temp_directory_path() &&
            path.filename().string().starts_with("phpvpn-config-files-"))
            std::filesystem::remove_all(path, ignored);
    }
};
void probeWrite(const std::filesystem::path& path, std::string_view text) {
    std::ofstream stream(path, std::ios::binary);
    stream << text;
}
std::string probeRead(const std::filesystem::path& path) {
    ProbeHandle file(CreateFileW(path.c_str(), GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        OPEN_EXISTING, 0, nullptr));
    if (file.value == INVALID_HANDLE_VALUE) return "READ ERROR";
    char bytes[512]{};
    DWORD count = 0;
    if (!ReadFile(file.value, bytes, sizeof(bytes), &count, nullptr)) return "READ ERROR";
    return {bytes, count};
}
bool probeRename(HANDLE file, HANDLE directory, std::wstring_view name, bool replace) {
    std::vector<wchar_t> parent(32768);
    const DWORD count = GetFinalPathNameByHandleW(directory, parent.data(),
        static_cast<DWORD>(parent.size()), FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    if (count == 0 || count >= parent.size()) return false;
    const std::wstring absolute = std::wstring(parent.data(), count) + L"\\" + std::wstring(name);
    name = absolute;
    const std::size_t size = offsetof(FILE_RENAME_INFO, FileName) +
        (name.size() + 1) * sizeof(wchar_t);
    std::vector<unsigned char> bytes(size, 0);
    auto* info = reinterpret_cast<FILE_RENAME_INFO*>(bytes.data());
    info->Flags = replace ? (FILE_RENAME_FLAG_REPLACE_IF_EXISTS | FILE_RENAME_FLAG_POSIX_SEMANTICS) : 0;
    info->RootDirectory = nullptr;
    info->FileNameLength = static_cast<DWORD>(name.size() * sizeof(wchar_t));
    std::memcpy(info->FileName, name.data(), info->FileNameLength);
    constexpr auto renameInfoEx = static_cast<FILE_INFO_BY_HANDLE_CLASS>(22);
    const bool success = SetFileInformationByHandle(file, renameInfoEx, info,
                                     static_cast<DWORD>(size)) != FALSE;
    if (!success) std::cout << "[ PROBE ] rename error " << GetLastError() << "\n";
    return success;
}
std::uint64_t probeIdentity(HANDLE handle) {
    BY_HANDLE_FILE_INFORMATION info{};
    if (!GetFileInformationByHandle(handle, &info)) return 0;
    return (static_cast<std::uint64_t>(info.nFileIndexHigh) << 32) | info.nFileIndexLow;
}
}

PHPVPN_TEST(config_files_probe, atomic_replace_create_only_and_locked_target) {
    ProbeDirectory fixture;
    const auto target = fixture.path / "config.json";
    const auto stage = fixture.path / "stage";
    const auto backup = fixture.path / "backup";
    probeWrite(target, "original");
    probeWrite(stage, "replacement");
    ProbeHandle directory(CreateFileW(fixture.path.c_str(), FILE_LIST_DIRECTORY,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    ProbeHandle original(CreateFileW(target.c_str(), GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
        FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    ProbeHandle staged(CreateFileW(stage.c_str(), GENERIC_READ | GENERIC_WRITE | DELETE,
        FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
        FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    PHPVPN_CHECK(directory.value != INVALID_HANDLE_VALUE);
    PHPVPN_CHECK(original.value != INVALID_HANDLE_VALUE);
    PHPVPN_CHECK(staged.value != INVALID_HANDLE_VALUE);
    ProbeHandle prohibitedWrite(CreateFileW(target.c_str(), GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr));
    PHPVPN_CHECK(prohibitedWrite.value == INVALID_HANDLE_VALUE);
    PHPVPN_CHECK(CreateHardLinkW(backup.c_str(), target.c_str(), nullptr) != FALSE);
    ProbeHandle saved(CreateFileW(backup.c_str(), GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
        FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    PHPVPN_CHECK_EQ(probeIdentity(original.value), probeIdentity(saved.value));
    PHPVPN_CHECK(!probeRename(staged.value, directory.value, L"config.json", false));
    PHPVPN_CHECK_EQ(probeRead(target), std::string("original"));
    {
        ProbeHandle locked(CreateFileW(target.c_str(), GENERIC_READ,
            FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr));
        PHPVPN_CHECK(locked.value != INVALID_HANDLE_VALUE);
        PHPVPN_CHECK(!probeRename(staged.value, directory.value, L"config.json", true));
        PHPVPN_CHECK_EQ(probeRead(target), std::string("original"));
    }
    PHPVPN_CHECK(probeRename(staged.value, directory.value, L"config.json", true));
    PHPVPN_CHECK_EQ(probeRead(target), std::string("replacement"));
    PHPVPN_CHECK_EQ(probeRead(backup), std::string("original"));
    std::cout << "[ EVIDENCE ] pinned-path FileRenameInfoEx overwrite/create-only/lock and hardlink identity\n";
}

PHPVPN_TEST(config_files_probe, replacement_of_target_link_does_not_modify_referent) {
    ProbeDirectory fixture;
    const auto outside = fixture.path / "unrelated";
    const auto target = fixture.path / "config.json";
    const auto stage = fixture.path / "stage";
    probeWrite(outside, "unrelated");
    probeWrite(stage, "replacement");
    PHPVPN_CHECK(CreateHardLinkW(target.c_str(), outside.c_str(), nullptr) != FALSE);
    ProbeHandle directory(CreateFileW(fixture.path.c_str(), FILE_LIST_DIRECTORY,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    {
        ProbeHandle staged(CreateFileW(stage.c_str(), GENERIC_READ | GENERIC_WRITE | DELETE,
            FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
            FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
        PHPVPN_CHECK(probeRename(staged.value, directory.value, L"config.json", true));
    }
    PHPVPN_CHECK_EQ(probeRead(target), std::string("replacement"));
    PHPVPN_CHECK_EQ(probeRead(outside), std::string("unrelated"));
    DeleteFileW(target.c_str());
    probeWrite(stage, "second");
    if (!CreateSymbolicLinkW(target.c_str(), outside.c_str(),
                            SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE)) {
        std::cout << "[ NOT RUN ] symbolic reparse race requires symlink privilege/developer mode\n";
        return;
    }
    ProbeHandle staged(CreateFileW(stage.c_str(), GENERIC_READ | GENERIC_WRITE | DELETE,
        FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
        FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    PHPVPN_CHECK(probeRename(staged.value, directory.value, L"config.json", true));
    PHPVPN_CHECK_EQ(probeRead(target), std::string("second"));
    PHPVPN_CHECK_EQ(probeRead(outside), std::string("unrelated"));
    PHPVPN_CHECK((GetFileAttributesW(target.c_str()) & FILE_ATTRIBUTE_REPARSE_POINT) == 0);
}
}  // namespace phpvpn

#include <sddl.h>
#include "platform/windows/config_files_win.h"
#include "core/config/config_document.h"
#include "core/config/config_store.h"
#include "core/config/config_defaults.h"
#include "platform/windows/paths_win.h"

namespace phpvpn {
namespace {
constexpr char kCommitA[] = "00000000-0000-4000-8000-000000000001";
constexpr char kCommitB[] = "00000000-0000-4000-8000-000000000002";
bool administrator() {
    BYTE sid[SECURITY_MAX_SID_SIZE]{};
    DWORD size = sizeof(sid);
    BOOL member = FALSE;
    return CreateWellKnownSid(WinBuiltinAdministratorsSid, nullptr, sid, &size) &&
        CheckTokenMembership(nullptr, sid, &member) && member;
}
struct SecureFixture {
    ProbeDirectory outer;
    std::filesystem::path directory = outer.path / "protected";
    bool create() {
        if (!administrator()) {
            std::cout << "[ NOT RUN ] ConfigFiles protected ACL/publication requires elevated token\n";
            return false;
        }
        PSECURITY_DESCRIPTOR descriptor = nullptr;
        if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
            L"O:BAG:BAD:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)", SDDL_REVISION_1,
            &descriptor, nullptr)) return false;
        SECURITY_ATTRIBUTES attributes{sizeof(attributes), descriptor, FALSE};
        const bool created = CreateDirectoryW(directory.c_str(), &attributes) != FALSE;
        LocalFree(descriptor);
        PHPVPN_CHECK(created);
        return created;
    }
    std::filesystem::path path() const { return directory / "config.json"; }
};
}

PHPVPN_TEST(config_files, rejects_unsafe_directory_and_nonfixed_paths_without_writing) {
    ProbeDirectory fixture;
    PHPVPN_REQUIRE_ERROR(createWindowsConfigFiles(fixture.path / "config.json"), ErrorCode::config_directory_security_invalid);
    PHPVPN_REQUIRE_ERROR(createWindowsConfigFiles("config.json"), ErrorCode::invalid_argument);
    PHPVPN_REQUIRE_ERROR(createWindowsConfigFiles(fixture.path / "other.json"), ErrorCode::invalid_argument);
    PHPVPN_CHECK(!std::filesystem::exists(fixture.path / "config.json"));
    PHPVPN_CHECK(!std::filesystem::exists(fixture.path / "config.publish-journal"));
}

PHPVPN_TEST(config_files, deleted_default_recreated_with_same_backend_and_confirmed_receipt) {
    SecureFixture fixture;
    if (!fixture.create()) return;
    auto crypto = createPlatformCryptoProvider();
    PHPVPN_REQUIRE_OK(crypto);
    auto backend = createWindowsConfigFiles(fixture.path());
    PHPVPN_REQUIRE_OK(backend);
    ConfigStore store(*crypto.value(), std::move(backend).value());
    PHPVPN_REQUIRE_OK(store.createDefault());
    PHPVPN_REQUIRE_OK(store.save(makeDefaultEditorDocument() + "\n", kCommitA));
    PHPVPN_CHECK(std::filesystem::exists(fixture.directory / "config.previous"));
    PHPVPN_CHECK(std::filesystem::exists(fixture.directory / "config.publish-journal"));
    PHPVPN_CHECK(DeleteFileW(fixture.path().c_str()) != FALSE);
    PHPVPN_CHECK_EQ(store.read()->availability, ConfigAvailability::missing);
    auto recreated = store.createDefault();
    PHPVPN_REQUIRE_OK(recreated);
    PHPVPN_CHECK(recreated->created && recreated->snapshot.config != nullptr);
    PHPVPN_CHECK_EQ(*recreated->snapshot.document, makeDefaultEditorDocument());
    const auto reread = store.read();
    PHPVPN_REQUIRE_OK(reread);
    PHPVPN_CHECK_EQ(*reread->document, makeDefaultEditorDocument());
    std::cout << "[ EVIDENCE ] same backend: delete/recreate with confirmed journal and backup ran on NTFS\n";
}

PHPVPN_TEST(config_files, fix2_startup_recovery_preserves_bytes_and_recreates_only_missing) {
    SecureFixture fixture;
    if (!fixture.create()) return;
    auto crypto = createPlatformCryptoProvider();
    PHPVPN_REQUIRE_OK(crypto);
    auto backend = createWindowsConfigFiles(fixture.path());
    PHPVPN_REQUIRE_OK(backend);
    ConfigStore store(*crypto.value(), std::move(backend).value());
    PHPVPN_REQUIRE_OK(store.recover());
    auto initial = store.ensureDefaultAtStartup();
    PHPVPN_REQUIRE_OK(initial);
    PHPVPN_CHECK_EQ(initial.value(), StartupConfigState::created);
    auto snapshot = store.read();
    PHPVPN_CHECK_EQ(*snapshot->document, makeDefaultEditorDocument());
    const auto customized = makeDefaultEditorDocument() + "\n\n";
    PHPVPN_REQUIRE_OK(store.save(customized, kCommitA));
    PHPVPN_REQUIRE_OK(store.recover());
    auto repeated = store.ensureDefaultAtStartup();
    PHPVPN_CHECK_EQ(repeated.value(), StartupConfigState::existing);
    snapshot = store.read();
    PHPVPN_CHECK_EQ(*snapshot->document, customized);
    PHPVPN_CHECK(DeleteFileW(fixture.path().c_str()) != FALSE);
    PHPVPN_REQUIRE_OK(store.recover());
    auto recreated = store.ensureDefaultAtStartup();
    PHPVPN_CHECK_EQ(recreated.value(), StartupConfigState::created);
    snapshot = store.read();
    PHPVPN_CHECK_EQ(*snapshot->document, makeDefaultEditorDocument());
    std::cout << "[ EVIDENCE ] startup operation, recovery and missing recreation ran on protected NTFS\n";
}

PHPVPN_TEST(config_files, changed_directory_acl_blocks_create_until_explicit_administrator_repair) {
    SecureFixture fixture;
    if (!fixture.create()) return;
    auto crypto = createPlatformCryptoProvider();
    PHPVPN_REQUIRE_OK(crypto);
    auto backend = createWindowsConfigFiles(fixture.path());
    PHPVPN_REQUIRE_OK(backend);
    auto* files = backend.value().get();
    ConfigStore store(*crypto.value(), std::move(backend).value());
    PHPVPN_REQUIRE_OK(store.createDefault());
    PHPVPN_REQUIRE_OK(store.save(makeDefaultEditorDocument() + "\n", kCommitA));
    PHPVPN_CHECK(DeleteFileW(fixture.path().c_str()) != FALSE);
    PHPVPN_CHECK_EQ(store.read()->availability, ConfigAvailability::missing);

    // Add the current test user's explicit full-control ACE after the read,
    // representing a permissions change while the confirmation dialog is open.
    HANDLE token = nullptr;
    PHPVPN_CHECK(OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token));
    ProbeHandle tokenHandle(token);
    DWORD size = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &size);
    std::vector<unsigned char> user(size);
    PHPVPN_CHECK(GetTokenInformation(token, TokenUser, user.data(), size, &size));
    LPWSTR sid = nullptr;
    PHPVPN_CHECK(ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(user.data())->User.Sid, &sid));
    const std::wstring sddl = std::wstring(L"D:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)(A;;FA;;;") + sid + L")";
    LocalFree(sid);
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    PHPVPN_CHECK(ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1, &descriptor, nullptr));
    const BOOL changed = SetFileSecurityW(fixture.directory.c_str(),
        DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION, descriptor);
    LocalFree(descriptor);
    PHPVPN_CHECK(changed);
    const auto journal = probeRead(fixture.directory / "config.publish-journal");
    const auto backup = probeRead(fixture.directory / "config.previous");
    PHPVPN_CHECK_EQ(store.read()->availability, ConfigAvailability::missing);
    PHPVPN_REQUIRE_ERROR(store.createDefault(), ErrorCode::config_directory_security_invalid);
    PHPVPN_REQUIRE_ERROR(files->recover(), ErrorCode::config_directory_security_invalid);
    PHPVPN_REQUIRE_ERROR(createWindowsConfigFiles(fixture.path()), ErrorCode::config_directory_security_invalid);
    PHPVPN_CHECK(!std::filesystem::exists(fixture.path()));
    PHPVPN_CHECK_EQ(probeRead(fixture.directory / "config.publish-journal"), journal);
    PHPVPN_CHECK_EQ(probeRead(fixture.directory / "config.previous"), backup);
    PHPVPN_REQUIRE_OK(restrictDirectoryToAdministrators(fixture.directory));
    auto repaired = store.createDefault();
    PHPVPN_REQUIRE_OK(repaired);
    PHPVPN_CHECK(repaired->created && repaired->snapshot.config != nullptr);
    std::cout << "[ EVIDENCE ] live third ACE rejected without writing; production ACL repair and explicit create succeeded\n";
}

PHPVPN_TEST(config_files, bounded_read_exact_hash_and_replace_create_only) {
    SecureFixture fixture;
    if (!fixture.create()) return;
    auto created = createWindowsConfigFiles(fixture.path());
    PHPVPN_REQUIRE_OK(created);
    auto files = std::move(created).value();
    auto absent = files->readBounded(kMaxConfigFileBytes);
    PHPVPN_REQUIRE_OK(absent);
    PHPVPN_CHECK(!absent->found);
    auto first = files->publish("abc", ConfigPublishMode::replace, kCommitA);
    PHPVPN_REQUIRE_OK(first);
    PHPVPN_CHECK(first->state == ConfigPublishState::confirmed);
    auto read = files->readBounded(3);
    PHPVPN_REQUIRE_OK(read);
    PHPVPN_CHECK_EQ(read->bytes, std::string("abc"));
    PHPVPN_CHECK_EQ(read->sha256, std::string("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
    PHPVPN_CHECK(read->identity == first->identity);
    PHPVPN_REQUIRE_ERROR(files->readBounded(2), ErrorCode::config_too_large);
    auto existing = files->publish("def", ConfigPublishMode::createIfMissing, kCommitB);
    PHPVPN_REQUIRE_OK(existing);
    PHPVPN_CHECK(existing->state == ConfigPublishState::alreadyExists);
    auto second = files->publish("new", ConfigPublishMode::replace, kCommitB);
    PHPVPN_REQUIRE_OK(second);
    PHPVPN_CHECK(second->state == ConfigPublishState::confirmed);
    PHPVPN_CHECK_EQ(probeRead(fixture.path()), std::string("new"));
    PHPVPN_CHECK_EQ(probeRead(fixture.directory / "config.previous"), std::string("abc"));
}

PHPVPN_TEST(config_files, directory_descriptor_api_failure_is_not_a_policy_mismatch) {
    SecureFixture fixture;
    if (!fixture.create()) return;
    for (unsigned failureAt : {1u, 2u, 3u}) {
        unsigned reads = 0;
        auto backend = createWindowsConfigFiles(fixture.path(), ConfigFileHooks{[&](ConfigFileStage stage) {
            return stage != ConfigFileStage::beforeDirectorySecurityRead || ++reads != failureAt;
        }});
        if (failureAt == 1) {
            PHPVPN_REQUIRE_ERROR(backend, ErrorCode::config_unreadable);
        } else {
            PHPVPN_REQUIRE_OK(backend);
            PHPVPN_REQUIRE_ERROR(backend.value()->publish("template", ConfigPublishMode::createIfMissing, kCommitA),
                ErrorCode::config_unreadable);
        }
        PHPVPN_CHECK_EQ(reads, failureAt);
        PHPVPN_CHECK(!std::filesystem::exists(fixture.path()));
        PHPVPN_CHECK(!std::filesystem::exists(fixture.directory / "config.publish-journal"));
    }
    std::cout << "[ EVIDENCE ] descriptor API failure at factory/recovery/publication remains config_unreadable\n";
}

PHPVPN_TEST(config_files, corrupt_publication_journal_keeps_its_error_after_target_deletion) {
    SecureFixture fixture;
    if (!fixture.create()) return;
    auto backend = createWindowsConfigFiles(fixture.path());
    PHPVPN_REQUIRE_OK(backend);
    PHPVPN_REQUIRE_OK(backend.value()->publish("original", ConfigPublishMode::replace, kCommitA));
    PHPVPN_CHECK(DeleteFileW(fixture.path().c_str()));
    const auto journal = fixture.directory / "config.publish-journal";
    probeWrite(journal, "UNKNOWN JOURNAL VERSION\n");
    PHPVPN_REQUIRE_ERROR(backend.value()->recover(), ErrorCode::journal_corrupt);
    PHPVPN_REQUIRE_ERROR(backend.value()->publish("default", ConfigPublishMode::createIfMissing, kCommitB),
        ErrorCode::journal_corrupt);
    PHPVPN_CHECK(!std::filesystem::exists(fixture.path()));
    PHPVPN_CHECK_EQ(probeRead(journal), std::string("UNKNOWN JOURNAL VERSION\n"));
}

PHPVPN_TEST(config_files, exact_file_limit_and_no_full_read_for_oversize) {
    SecureFixture fixture;
    if (!fixture.create()) return;
    std::size_t chunks = 0;
    auto made = createWindowsConfigFiles(fixture.path(), ConfigFileHooks{[&](ConfigFileStage stage) {
        if (stage == ConfigFileStage::readChunk) ++chunks;
        return true;
    }});
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
    PHPVPN_REQUIRE_ERROR(made.value()->publish(full + "x", ConfigPublishMode::replace, kCommitB),
        ErrorCode::config_too_large);
}

PHPVPN_TEST(config_files, injected_prepublication_failures_preserve_original) {
    for (const auto failure : {ConfigFileStage::stageWritten, ConfigFileStage::beforeStageFlush,
                              ConfigFileStage::beforeJournalFlush, ConfigFileStage::beforePublish}) {
        SecureFixture fixture;
        if (!fixture.create()) return;
        bool inject = false;
        auto made = createWindowsConfigFiles(fixture.path(),
            ConfigFileHooks{[&](ConfigFileStage stage) { return !inject || stage != failure; }});
        PHPVPN_REQUIRE_OK(made);
        PHPVPN_REQUIRE_OK(made.value()->publish("old", ConfigPublishMode::replace, kCommitA));
        PHPVPN_REQUIRE_OK(made.value()->recover());
        inject = true;
        PHPVPN_REQUIRE_ERROR(made.value()->publish("secret", ConfigPublishMode::replace, kCommitB),
            ErrorCode::config_write_failed);
        PHPVPN_CHECK_EQ(probeRead(fixture.path()), std::string("old"));
        std::size_t count = 0;
        for (const auto& item : std::filesystem::directory_iterator(fixture.directory)) {
            PHPVPN_CHECK_EQ(item.path().filename().string(), std::string("config.json"));
            ++count;
        }
        PHPVPN_CHECK_EQ(count, std::size_t(1));
    }
}

PHPVPN_TEST(config_files, crash_boundaries_resolve_without_replaying) {
    for (const auto failure : {ConfigFileStage::journalFlushed, ConfigFileStage::afterPublish}) {
        SecureFixture fixture;
        if (!fixture.create()) return;
        {
            auto made = createWindowsConfigFiles(fixture.path(),
                ConfigFileHooks{[&](ConfigFileStage stage) { return stage != failure; }});
            PHPVPN_REQUIRE_OK(made);
            auto result = made.value()->publish("new", ConfigPublishMode::replace, kCommitA);
            PHPVPN_REQUIRE_OK(result);
            PHPVPN_CHECK(result->state == ConfigPublishState::unknown);
        }
        auto reopened = createWindowsConfigFiles(fixture.path());
        PHPVPN_REQUIRE_OK(reopened);
        auto recovered = reopened.value()->recover();
        PHPVPN_REQUIRE_OK(recovered);
        PHPVPN_CHECK(recovered->state == (failure == ConfigFileStage::afterPublish ?
            ConfigRecoveryState::confirmed : ConfigRecoveryState::notPublished));
        PHPVPN_CHECK_EQ(recovered->commitId, std::string(kCommitA));
        auto read = reopened.value()->readBounded(kMaxConfigFileBytes);
        PHPVPN_REQUIRE_OK(read);
        PHPVPN_CHECK_EQ(read->found, failure == ConfigFileStage::afterPublish);
    }
}

PHPVPN_TEST(config_files, locked_target_and_foreign_backup_are_preserved) {
    SecureFixture fixture;
    if (!fixture.create()) return;
    auto made = createWindowsConfigFiles(fixture.path());
    PHPVPN_REQUIRE_OK(made);
    PHPVPN_REQUIRE_OK(made.value()->publish("old", ConfigPublishMode::replace, kCommitA));
    PHPVPN_REQUIRE_OK(made.value()->recover());
    {
        ProbeHandle locked(CreateFileW(fixture.path().c_str(), GENERIC_READ, FILE_SHARE_READ,
            nullptr, OPEN_EXISTING, 0, nullptr));
        PHPVPN_CHECK(locked.value != INVALID_HANDLE_VALUE);
        auto result = made.value()->publish("new", ConfigPublishMode::replace, kCommitB);
        PHPVPN_CHECK(!result || result->state != ConfigPublishState::confirmed);
        PHPVPN_CHECK_EQ(probeRead(fixture.path()), std::string("old"));
    }
    probeWrite(fixture.directory / "config.previous", "foreign");
    auto result = made.value()->publish("new", ConfigPublishMode::replace, kCommitB);
    PHPVPN_CHECK(!result || result->state != ConfigPublishState::confirmed);
    PHPVPN_CHECK_EQ(probeRead(fixture.directory / "config.previous"), std::string("foreign"));
    PHPVPN_CHECK_EQ(probeRead(fixture.path()), std::string("old"));
}

PHPVPN_TEST(config_files, corrupt_journal_and_reparse_target_fail_closed) {
    SecureFixture fixture;
    if (!fixture.create()) return;
    auto made = createWindowsConfigFiles(fixture.path());
    PHPVPN_REQUIRE_OK(made);
    probeWrite(fixture.directory / "config.publish-journal", "garbage");
    PHPVPN_REQUIRE_ERROR(made.value()->recover(), ErrorCode::journal_corrupt);
    PHPVPN_CHECK(!made.value()->publish("new", ConfigPublishMode::replace, kCommitA));
    DeleteFileW((fixture.directory / "config.publish-journal").c_str());
    const auto outside = fixture.outer.path / "unrelated";
    probeWrite(outside, "unrelated");
    PHPVPN_CHECK(CreateSymbolicLinkW(fixture.path().c_str(), outside.c_str(), 0) != FALSE);
    PHPVPN_REQUIRE_ERROR(made.value()->readBounded(kMaxConfigFileBytes), ErrorCode::config_unreadable);
    PHPVPN_CHECK(!made.value()->publish("new", ConfigPublishMode::replace, kCommitA));
    PHPVPN_CHECK_EQ(probeRead(outside), std::string("unrelated"));
}


PHPVPN_TEST(config_files, confirmed_receipt_survives_external_change_and_allows_later_save) {
    SecureFixture fixture;
    if (!fixture.create()) return;
    auto made = createWindowsConfigFiles(fixture.path());
    PHPVPN_REQUIRE_OK(made);
    PHPVPN_REQUIRE_OK(made.value()->publish("first", ConfigPublishMode::replace, kCommitA));
    probeWrite(fixture.path(), "external edit");
    auto historical = made.value()->recover();
    PHPVPN_REQUIRE_OK(historical);
    PHPVPN_CHECK(historical->state == ConfigRecoveryState::confirmed);
    PHPVPN_CHECK_EQ(historical->commitId, std::string(kCommitA));
    PHPVPN_CHECK_EQ(probeRead(fixture.path()), std::string("external edit"));
    auto next = made.value()->publish("last writer", ConfigPublishMode::replace, kCommitB);
    PHPVPN_REQUIRE_OK(next);
    PHPVPN_CHECK(next->state == ConfigPublishState::confirmed);
    PHPVPN_CHECK_EQ(probeRead(fixture.path()), std::string("last writer"));
}

PHPVPN_TEST(config_files, read_pins_original_inode_and_denies_growth_writes) {
    SecureFixture fixture;
    if (!fixture.create()) return;
    std::size_t attempts = 0;
    auto made = createWindowsConfigFiles(fixture.path(), ConfigFileHooks{[&](ConfigFileStage stage) {
        if (stage == ConfigFileStage::readChunk) {
            ++attempts;
            ProbeHandle writer(CreateFileW(fixture.path().c_str(), FILE_APPEND_DATA,
                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr));
            PHPVPN_CHECK(writer.value == INVALID_HANDLE_VALUE);
        }
        return true;
    }});
    PHPVPN_REQUIRE_OK(made);
    PHPVPN_REQUIRE_OK(made.value()->publish(std::string(32768, 'a'), ConfigPublishMode::replace, kCommitA));
    auto read = made.value()->readBounded(32768);
    PHPVPN_REQUIRE_OK(read);
    PHPVPN_CHECK_EQ(read->bytes.size(), std::size_t(32768));
    PHPVPN_CHECK_EQ(attempts, std::size_t(2));
}

PHPVPN_TEST(config_files, unrelated_target_after_unconfirmed_commit_stays_unknown) {
    SecureFixture fixture;
    if (!fixture.create()) return;
    auto made = createWindowsConfigFiles(fixture.path(),
        ConfigFileHooks{[](ConfigFileStage stage) { return stage != ConfigFileStage::afterPublish; }});
    PHPVPN_REQUIRE_OK(made);
    auto published = made.value()->publish("uncertain", ConfigPublishMode::replace, kCommitA);
    PHPVPN_REQUIRE_OK(published);
    PHPVPN_CHECK(published->state == ConfigPublishState::unknown);
    const auto moved = fixture.directory / "external-move";
    PHPVPN_CHECK(MoveFileExW(fixture.path().c_str(), moved.c_str(), 0) != FALSE);
    probeWrite(fixture.path(), "unrelated");
    auto recovery = made.value()->recover();
    PHPVPN_REQUIRE_OK(recovery);
    PHPVPN_CHECK(recovery->state == ConfigRecoveryState::unknown);
    PHPVPN_CHECK_EQ(probeRead(fixture.path()), std::string("unrelated"));
    PHPVPN_CHECK_EQ(probeRead(moved), std::string("uncertain"));
}

PHPVPN_TEST(config_files, create_race_and_pinned_directory) {
    SecureFixture fixture;
    if (!fixture.create()) return;
    bool injected = false;
    auto made = createWindowsConfigFiles(fixture.path(), ConfigFileHooks{[&](ConfigFileStage stage) {
        if (!injected && stage == ConfigFileStage::beforePublish) {
            injected = true;
            probeWrite(fixture.path(), "external");
            PHPVPN_CHECK(!MoveFileExW(fixture.directory.c_str(),
                (fixture.outer.path / "moved").c_str(), 0));
        }
        return true;
    }});
    PHPVPN_REQUIRE_OK(made);
    auto result = made.value()->publish("template", ConfigPublishMode::createIfMissing, kCommitA);
    PHPVPN_REQUIRE_OK(result);
    PHPVPN_CHECK(result->state == ConfigPublishState::alreadyExists);
    PHPVPN_CHECK_EQ(probeRead(fixture.path()), std::string("external"));
}
}  // namespace phpvpn

namespace phpvpn {
namespace {
Result<bool> inspectSddl(const wchar_t* sddl, bool directory) {
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl, SDDL_REVISION_1, &descriptor, nullptr))
        return Error(ErrorCode::internal);
    auto result = validateWindowsConfigDescriptor(descriptor, directory);
    LocalFree(descriptor);
    return result;
}
}
PHPVPN_TEST(config_acl, canonical_and_safe_extra_reads_without_elevation) {
    for (const auto* sddl : {
        L"O:BAG:BAD:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)(A;;FRFX;;;BU)",
        L"O:SYG:SYD:P(A;;FA;;;BA)(A;;FA;;;SY)(A;OICI;FRFX;;;BU)(A;ID;GR;;;AU)",
        L"O:BAG:BAD:P(A;;GA;;;SY)(A;;FA;;;BA)(A;;FR;;;BU)(A;;FR;;;BU)",
        L"O:BAG:BAD:P(A;;0x00120089;;;SY)(A;;0x000d0176;;;SY)(A;;FA;;;BA)(A;;FR;;;BU)"
    }) {
        auto result = inspectSddl(sddl, true);
        PHPVPN_REQUIRE_OK(result);
        PHPVPN_CHECK(result.value());
    }
    auto config = inspectSddl(L"O:BAG:BAD:P(A;;FA;;;SY)(A;;FA;;;BA)(A;;FR;;;BU)(A;;GR;;;AU)", false);
    PHPVPN_REQUIRE_OK(config);
    PHPVPN_CHECK(config.value());
    auto internal = inspectSddl(L"O:BAG:BAD:P(A;;FA;;;SY)(A;;FA;;;BA)(A;;FR;;;BU)", false);
    PHPVPN_REQUIRE_OK(internal);
    PHPVPN_CHECK(internal.value());
}
PHPVPN_TEST(config_acl, rejects_mutation_inheritance_and_privileged_restriction) {
    for (const auto* extra : {L"(A;;FW;;;BU)", L"(A;;FA;;;AU)", L"(A;;GW;;;WD)",
        L"(A;;SD;;;BU)", L"(A;;WD;;;BU)", L"(A;;WO;;;BU)", L"(A;;0x40;;;BU)",
        L"(A;;0x2;;;BU)", L"(A;;0x4;;;BU)", L"(A;;0x100;;;BU)", L"(A;;0x10;;;BU)",
        L"(A;OI;FW;;;BU)", L"(A;CI;FW;;;BU)", L"(A;IO;FW;;;BU)", L"(A;ID;FW;;;BU)",
        L"(D;;FW;;;BA)", L"(D;;FW;;;WD)"}) {
        const auto sddl = std::wstring(L"O:BAG:BAD:P(A;;FA;;;SY)(A;;FA;;;BA)") + extra;
        for (const bool directory : {false, true}) {
            auto result = inspectSddl(sddl.c_str(), directory);
            PHPVPN_REQUIRE_OK(result);
            PHPVPN_CHECK(!result.value());
        }
    }
    for (const auto* sddl : {L"O:BUG:BAD:P(A;;FA;;;SY)(A;;FA;;;BA)",
        L"O:BAG:BAD:(A;;FA;;;SY)(A;;FA;;;BA)",
        L"O:BAG:BAD:P(A;;FR;;;SY)(A;;FA;;;BA)",
        L"O:BAG:BAD:P(A;IO;FA;;;SY)(A;;FA;;;BA)", L"O:BAG:BAD:P"}) {
        auto result = inspectSddl(sddl, true);
        PHPVPN_REQUIRE_OK(result);
        PHPVPN_CHECK(!result.value());
    }
    PHPVPN_REQUIRE_ERROR(validateWindowsConfigDescriptor(nullptr, true), ErrorCode::config_unreadable);
}
}  // namespace phpvpn

namespace phpvpn {
PHPVPN_TEST(config_acl, standard_token_effective_access) {
    HANDLE process = nullptr;
    PHPVPN_CHECK(OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY | TOKEN_DUPLICATE, &process));
    if (!process) return;
    ProbeHandle processToken(process);
    BYTE admins[SECURITY_MAX_SID_SIZE]{};
    DWORD size = sizeof(admins);
    PHPVPN_CHECK(CreateWellKnownSid(WinBuiltinAdministratorsSid, nullptr, admins, &size));
    SID_AND_ATTRIBUTES disabled{admins, 0};
    HANDLE restricted = nullptr;
    PHPVPN_CHECK(CreateRestrictedToken(process, DISABLE_MAX_PRIVILEGE, 1, &disabled,
        0, nullptr, 0, nullptr, &restricted));
    if (!restricted) return;
    ProbeHandle restrictedToken(restricted);
    HANDLE impersonation = nullptr;
    PHPVPN_CHECK(DuplicateToken(restricted, SecurityImpersonation, &impersonation));
    if (!impersonation) return;
    ProbeHandle token(impersonation);
    for (unsigned kind = 0; kind < 3; ++kind) {
        const wchar_t* sddl = kind == 0 ? L"O:BAG:BAD:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)(A;;FRFX;;;BU)" :
            L"O:BAG:BAD:P(A;;FA;;;SY)(A;;FA;;;BA)(A;;FRFX;;;BU)";
        PSECURITY_DESCRIPTOR descriptor = nullptr;
        PHPVPN_CHECK(ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl, SDDL_REVISION_1, &descriptor, nullptr));
        if (!descriptor) return;
        GENERIC_MAPPING mapping{FILE_GENERIC_READ, FILE_GENERIC_WRITE, FILE_GENERIC_EXECUTE, FILE_ALL_ACCESS};
        for (const DWORD request : {DWORD(FILE_GENERIC_READ), DWORD(FILE_TRAVERSE), DWORD(FILE_WRITE_DATA),
            DWORD(FILE_APPEND_DATA), DWORD(FILE_DELETE_CHILD), DWORD(DELETE), DWORD(WRITE_DAC), DWORD(WRITE_OWNER),
            DWORD(FILE_WRITE_ATTRIBUTES), DWORD(FILE_WRITE_EA)}) {
            BYTE privileges[1024]{};
            DWORD privilegeSize = sizeof(privileges), granted = 0;
            BOOL allowed = FALSE;
            PHPVPN_CHECK(AccessCheck(descriptor, impersonation, request, &mapping,
                reinterpret_cast<PRIVILEGE_SET*>(privileges), &privilegeSize, &granted, &allowed));
            const bool expected = (request == FILE_GENERIC_READ || request == FILE_TRAVERSE);
            PHPVPN_CHECK_EQ(allowed != FALSE, expected);
        }
        LocalFree(descriptor);
    }
    std::cout << "[ EVIDENCE ] AccessCheck with nonadministrator token: directory/config reads, mutation denied, internal reads allowed\n";
}
}  // namespace phpvpn

namespace phpvpn {
PHPVPN_TEST(config_files, readonly_tree_actual_ntfs_access_and_publication) {
    SecureFixture fixture;
    if (!fixture.create()) return;
    PHPVPN_REQUIRE_OK(restrictDirectoryToAdministrators(fixture.directory));
    auto files = createWindowsConfigFiles(fixture.path());
    PHPVPN_REQUIRE_OK(files);
    PHPVPN_REQUIRE_OK(files.value()->publish("first", ConfigPublishMode::replace, kCommitA));
    PHPVPN_REQUIRE_OK(files.value()->publish("second", ConfigPublishMode::replace, kCommitB));
    const auto logDir = fixture.directory / "logs";
    PHPVPN_CHECK(CreateDirectoryW(logDir.c_str(), nullptr));
    probeWrite(logDir / "future.log", "future state");
    probeWrite(fixture.directory / "ownership.json", "metadata");
    HANDLE process = nullptr;
    PHPVPN_CHECK(OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY | TOKEN_DUPLICATE, &process));
    if (!process) return;
    ProbeHandle processToken(process);
    BYTE admins[SECURITY_MAX_SID_SIZE]{};
    DWORD size = sizeof(admins);
    PHPVPN_CHECK(CreateWellKnownSid(WinBuiltinAdministratorsSid, nullptr, admins, &size));
    SID_AND_ATTRIBUTES disabled{admins, 0};
    HANDLE restricted = nullptr;
    PHPVPN_CHECK(CreateRestrictedToken(process, DISABLE_MAX_PRIVILEGE, 1, &disabled,
        0, nullptr, 0, nullptr, &restricted));
    if (!restricted) return;
    ProbeHandle token(restricted);
    {
        struct Revert { ~Revert() { RevertToSelf(); } } revert;
        const bool impersonated = ImpersonateLoggedOnUser(restricted) != FALSE;
        PHPVPN_CHECK(impersonated);
        if (!impersonated) return;
        // Opening for each forbidden right exercises the NTFS access check without
        // relying on an application choosing to respect a read-only attribute.
        ProbeHandle folder(CreateFileW(fixture.directory.c_str(), FILE_LIST_DIRECTORY | FILE_TRAVERSE,
            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr));
        PHPVPN_CHECK(folder.value != INVALID_HANDLE_VALUE);
        PHPVPN_CHECK_EQ(probeRead(fixture.path()), std::string("second"));
        PHPVPN_CHECK_EQ(probeRead(fixture.directory / "config.previous"), std::string("first"));
        PHPVPN_CHECK(probeRead(fixture.directory / "config.publish-journal").starts_with("PHPVPN-CONFIG-1"));
        PHPVPN_CHECK_EQ(probeRead(logDir / "future.log"), std::string("future state"));
        PHPVPN_CHECK_EQ(probeRead(fixture.directory / "ownership.json"), std::string("metadata"));
        const auto copy = fixture.outer.path / "copy.json";
        PHPVPN_CHECK(CopyFileW(fixture.path().c_str(), copy.c_str(), TRUE));
        for (const auto& path : {fixture.path(), fixture.directory / "config.previous",
            fixture.directory / "config.publish-journal", logDir / "future.log"}) {
            for (const DWORD right : {DWORD(GENERIC_WRITE), DWORD(DELETE), DWORD(WRITE_DAC), DWORD(WRITE_OWNER)}) {
                ProbeHandle denied(CreateFileW(path.c_str(), right, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                    nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
                PHPVPN_CHECK(denied.value == INVALID_HANDLE_VALUE);
                PHPVPN_CHECK_EQ(GetLastError(), DWORD(ERROR_ACCESS_DENIED));
            }
        }
        ProbeHandle denied(CreateFileW((fixture.directory / "forbidden").c_str(), GENERIC_WRITE,
            FILE_SHARE_READ, nullptr, CREATE_NEW, 0, nullptr));
        PHPVPN_CHECK(denied.value == INVALID_HANDLE_VALUE);
        PHPVPN_CHECK(!CreateDirectoryW((logDir / "forbidden").c_str(), nullptr));
        PHPVPN_CHECK(!DeleteFileW(fixture.path().c_str()));
        PHPVPN_CHECK(!MoveFileExW(fixture.path().c_str(), (fixture.directory / "renamed").c_str(), 0));
    }
    PHPVPN_REQUIRE_OK(files.value()->recover());
    PHPVPN_CHECK_EQ(files.value()->readBounded(kMaxConfigFileBytes)->bytes, std::string("second"));
    PHPVPN_REQUIRE_OK(files.value()->publish("third", ConfigPublishMode::replace, kCommitA));
    std::cout << "[ EVIDENCE ] elevated NTFS fixture, restricted-token read/copy, mutation denied, publication after reads\n";
}
}  // namespace phpvpn

namespace phpvpn {
namespace {
std::vector<unsigned char> securityBytes(const std::filesystem::path& path) {
    constexpr DWORD parts = OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION;
    DWORD size = 0;
    GetFileSecurityW(path.c_str(), parts, nullptr, 0, &size);
    std::vector<unsigned char> bytes(size);
    if (!size || !GetFileSecurityW(path.c_str(), parts, bytes.data(), size, &size)) return {};
    return bytes;
}
bool setSddl(const std::filesystem::path& path, const std::wstring& sddl) {
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1, &descriptor, nullptr)) return false;
    const bool result = SetFileSecurityW(path.c_str(), OWNER_SECURITY_INFORMATION |
        DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION, descriptor) != FALSE;
    LocalFree(descriptor);
    return result;
}
}
PHPVPN_TEST(config_files, startup_migrates_legacy_and_explorer_acl_without_byte_or_identity_changes) {
    SecureFixture fixture;
    if (!fixture.create()) return;
    const std::string original = "invalid or old configuration bytes\r\n";
    probeWrite(fixture.path(), original);
    PHPVPN_CHECK(CreateHardLinkW((fixture.directory / "config.previous").c_str(), fixture.path().c_str(), nullptr));
    const auto nested = fixture.directory / "logs";
    PHPVPN_CHECK(CreateDirectoryW(nested.c_str(), nullptr));
    probeWrite(nested / "old.log", "old log");
    ProbeHandle before(CreateFileW(fixture.path().c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr, OPEN_EXISTING, 0, nullptr));
    const auto oldIdentity = probeIdentity(before.value);
    HANDLE token = nullptr;
    PHPVPN_CHECK(OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token));
    if (!token) return;
    ProbeHandle tokenHandle(token);
    DWORD size = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &size);
    std::vector<unsigned char> user(size);
    PHPVPN_CHECK(GetTokenInformation(token, TokenUser, user.data(), size, &size));
    LPWSTR userSid = nullptr;
    PHPVPN_CHECK(ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(user.data())->User.Sid, &userSid));
    if (!userSid) return;
    const auto explorer = std::wstring(L"O:BAG:BAD:P(A;;FA;;;SY)(A;;FA;;;BA)(A;;FA;;;") + userSid + L")";
    LocalFree(userSid);
    for (const bool extra : {false, true}) {
        const std::wstring old = L"O:BAG:BAD:P(A;;FA;;;SY)(A;;FA;;;BA)";
        PHPVPN_CHECK(setSddl(fixture.directory, extra ? explorer : old));
        PHPVPN_CHECK(setSddl(fixture.path(), extra ? explorer : old));
        PHPVPN_CHECK(setSddl(nested / "old.log", extra ? explorer : old));
        PHPVPN_REQUIRE_OK(restrictDirectoryToAdministrators(fixture.directory));
        PHPVPN_CHECK_EQ(probeRead(fixture.path()), original);
        PHPVPN_CHECK_EQ(probeRead(fixture.directory / "config.previous"), original);
        for (const auto& path : {fixture.path(), fixture.directory / "config.previous", nested / "old.log"}) {
            const auto descriptor = securityBytes(path);
            PHPVPN_CHECK(!descriptor.empty());
            auto valid = validateWindowsConfigDescriptor(const_cast<unsigned char*>(descriptor.data()), false);
            PHPVPN_REQUIRE_OK(valid);
            PHPVPN_CHECK(valid.value());
        }
        ProbeHandle after(CreateFileW(fixture.path().c_str(), GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr));
        PHPVPN_CHECK_EQ(probeIdentity(after.value), oldIdentity);
        PHPVPN_REQUIRE_OK(createWindowsConfigFiles(fixture.path()));
    }
    std::cout << "[ EVIDENCE ] legacy/Explorer tree migrated without changing bytes or internal hardlink identity\n";
}
PHPVPN_TEST(config_files, startup_rejects_external_hardlinks_without_external_acl_change) {
    SecureFixture fixture;
    if (!fixture.create()) return;
    const auto outside = fixture.outer.path / "outside";
    probeWrite(outside, "outside content");
    PHPVPN_CHECK(CreateHardLinkW(fixture.path().c_str(), outside.c_str(), nullptr));
    const auto security = securityBytes(outside);
    PHPVPN_CHECK(!security.empty());
    PHPVPN_REQUIRE_ERROR(restrictDirectoryToAdministrators(fixture.directory), ErrorCode::config_unreadable);
    PHPVPN_CHECK_EQ(securityBytes(outside), security);
    PHPVPN_CHECK_EQ(probeRead(outside), std::string("outside content"));
}
PHPVPN_TEST(config_files, startup_rejects_reparse_without_external_acl_change) {
    SecureFixture fixture;
    if (!fixture.create()) return;
    const auto outside = fixture.outer.path / "outside";
    probeWrite(outside, "outside content");
    if (!CreateSymbolicLinkW(fixture.path().c_str(), outside.c_str(), SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE)) {
        std::cout << "[ NOT RUN ] startup reparse test requires symbolic-link privilege\n";
        return;
    }
    const auto security = securityBytes(outside);
    PHPVPN_REQUIRE_ERROR(restrictDirectoryToAdministrators(fixture.directory), ErrorCode::config_unreadable);
    PHPVPN_CHECK_EQ(securityBytes(outside), security);
    PHPVPN_CHECK_EQ(probeRead(outside), std::string("outside content"));
}
}  // namespace phpvpn

namespace phpvpn {
PHPVPN_TEST(config_acl, pinned_low_level_setter_does_not_propagate_to_uninspected_child) {
    ProbeDirectory fixture;
    const auto child = fixture.path / "child";
    probeWrite(child, "unchanged");
    const auto before = securityBytes(child);
    PHPVPN_CHECK(!before.empty());
    HANDLE token = nullptr;
    PHPVPN_CHECK(OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token));
    if (!token) return;
    ProbeHandle tokenHandle(token);
    DWORD size = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &size);
    std::vector<unsigned char> user(size);
    PHPVPN_CHECK(GetTokenInformation(token, TokenUser, user.data(), size, &size));
    LPWSTR userSid = nullptr;
    PHPVPN_CHECK(ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(user.data())->User.Sid, &userSid));
    if (!userSid) return;
    const auto sddl = std::wstring(L"D:P(A;OICI;FA;;;") + userSid + L")(A;OICI;FRFX;;;BU)";
    LocalFree(userSid);
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    PHPVPN_CHECK(ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1, &descriptor, nullptr));
    if (!descriptor) return;
    ProbeHandle pinned(CreateFileW(fixture.path.c_str(), READ_CONTROL | WRITE_DAC,
        FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    PHPVPN_CHECK(pinned.value != INVALID_HANDLE_VALUE);
    PHPVPN_CHECK(SetFileSecurityW(fixture.path.c_str(), DACL_SECURITY_INFORMATION |
        PROTECTED_DACL_SECURITY_INFORMATION, descriptor));
    LocalFree(descriptor);
    PHPVPN_CHECK_EQ(securityBytes(child), before);
    PHPVPN_CHECK_EQ(probeRead(child), std::string("unchanged"));
    std::cout << "[ EVIDENCE ] low-level ACL setter did not propagate into existing child on NTFS\n";
}
}  // namespace phpvpn

namespace phpvpn {
PHPVPN_TEST(config_files, safe_extra_read_ace_allows_save_unsafe_inherited_write_blocks_it) {
    SecureFixture fixture;
    if (!fixture.create()) return;
    PHPVPN_REQUIRE_OK(restrictDirectoryToAdministrators(fixture.directory));
    auto files = createWindowsConfigFiles(fixture.path());
    PHPVPN_REQUIRE_OK(files);
    PHPVPN_REQUIRE_OK(files.value()->publish("first", ConfigPublishMode::replace, kCommitA));
    PHPVPN_CHECK(setSddl(fixture.directory,
        L"O:BAG:BAD:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)(A;OICI;FRFX;;;BU)(A;;GR;;;AU)"));
    PHPVPN_CHECK(setSddl(fixture.path(),
        L"O:BAG:BAD:P(A;;FA;;;SY)(A;;FA;;;BA)(A;;FRFX;;;BU)(A;;GR;;;AU)"));
    PHPVPN_REQUIRE_OK(files.value()->publish("second", ConfigPublishMode::replace, kCommitB));
    const auto journal = probeRead(fixture.directory / "config.publish-journal");
    PHPVPN_CHECK(setSddl(fixture.directory,
        L"O:BAG:BAD:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)(A;OICI;FRFX;;;BU)(A;OIIO;FW;;;AU)"));
    PHPVPN_REQUIRE_ERROR(files.value()->publish("blocked", ConfigPublishMode::replace, kCommitA),
        ErrorCode::config_directory_security_invalid);
    PHPVPN_CHECK_EQ(probeRead(fixture.path()), std::string("second"));
    PHPVPN_CHECK_EQ(probeRead(fixture.directory / "config.publish-journal"), journal);
}
}  // namespace phpvpn
