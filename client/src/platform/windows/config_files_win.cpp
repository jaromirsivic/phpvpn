#include "platform/windows/config_files_win.h"

#include <windows.h>
#include <aclapi.h>
#include <sddl.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <mutex>
#include <sstream>
#include <utility>
#include <vector>

#include "core/base/hex.h"
#include "core/config/config_document.h"
#include "core/crypto/crypto_provider.h"
#include "core/protocol/uuid.h"

namespace phpvpn {
namespace {
constexpr wchar_t kJournal[] = L"config.publish-journal";
constexpr wchar_t kBackup[] = L"config.previous";
constexpr std::size_t kJournalLimit = 1024;
// Windows 10 RS1 FILE_INFO_BY_HANDLE_CLASS, hidden by the project's older NTDDI.
constexpr auto kRenameInfoEx = static_cast<FILE_INFO_BY_HANDLE_CLASS>(22);

class Handle {
public:
    HANDLE value = INVALID_HANDLE_VALUE;
    Handle() = default;
    explicit Handle(HANDLE h) : value(h) {}
    ~Handle() { close(); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    Handle(Handle&& other) noexcept : value(std::exchange(other.value, INVALID_HANDLE_VALUE)) {}
    Handle& operator=(Handle&& other) noexcept {
        if (this != &other) { close(); value = std::exchange(other.value, INVALID_HANDLE_VALUE); }
        return *this;
    }
    void close() { if (value != INVALID_HANDLE_VALUE) CloseHandle(std::exchange(value, INVALID_HANDLE_VALUE)); }
    explicit operator bool() const { return value != INVALID_HANDLE_VALUE; }
};
struct Descriptor {
    PSECURITY_DESCRIPTOR value = nullptr;
    ~Descriptor() { if (value) LocalFree(value); }
    bool create() {
        return ConvertStringSecurityDescriptorToSecurityDescriptorW(
            L"O:BAG:BAD:P(A;;FA;;;SY)(A;;FA;;;BA)", SDDL_REVISION_1, &value, nullptr) != FALSE;
    }
};
Error readError() { return Error(ErrorCode::config_unreadable, "the configuration file cannot be safely read"); }
Error writeError() { return Error(ErrorCode::config_write_failed, "the configuration publication could not be completed"); }
bool missing(DWORD error) { return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND; }

Result<ConfigFileIdentity> identity(HANDLE handle) {
    BY_HANDLE_FILE_INFORMATION info{};
    if (!GetFileInformationByHandle(handle, &info)) return readError();
    return ConfigFileIdentity{info.dwVolumeSerialNumber,
        (static_cast<std::uint64_t>(info.nFileIndexHigh) << 32) | info.nFileIndexLow};
}
bool safeType(HANDLE handle, bool directory) {
    FILE_ATTRIBUTE_TAG_INFO info{};
    return GetFileInformationByHandleEx(handle, FileAttributeTagInfo, &info, sizeof(info)) &&
        !(info.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) &&
        ((info.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) == directory;
}
Result<bool> inspectRestricted(HANDLE handle, bool directory) {
    PSID owner = nullptr;
    PACL acl = nullptr;
    Descriptor descriptor;
    if (GetSecurityInfo(handle, SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION |
        DACL_SECURITY_INFORMATION, &owner, nullptr, &acl, nullptr, &descriptor.value) != ERROR_SUCCESS)
        return readError();
    std::array<unsigned char, SECURITY_MAX_SID_SIZE> system{}, admins{};
    DWORD systemSize = static_cast<DWORD>(system.size()), adminsSize = static_cast<DWORD>(admins.size());
    if (!CreateWellKnownSid(WinLocalSystemSid, nullptr, system.data(), &systemSize) ||
        !CreateWellKnownSid(WinBuiltinAdministratorsSid, nullptr, admins.data(), &adminsSize))
        return readError();
    SECURITY_DESCRIPTOR_CONTROL control = 0;
    DWORD revision = 0;
    if (!GetSecurityDescriptorControl(descriptor.value, &control, &revision)) return readError();
    bool good = owner && (EqualSid(owner, system.data()) || EqualSid(owner, admins.data())) &&
        (!directory || (control & SE_DACL_PROTECTED)) && acl && acl->AceCount == 2;
    unsigned seen = 0;
    if (good) {
        for (DWORD i = 0; i < acl->AceCount; ++i) {
            void* entry = nullptr;
            if (!GetAce(acl, i, &entry)) return readError();
            const auto* ace = static_cast<const ACCESS_ALLOWED_ACE*>(entry);
            if (ace->Header.AceType != ACCESS_ALLOWED_ACE_TYPE ||
                (ace->Header.AceFlags & INHERIT_ONLY_ACE) ||
                (ace->Mask & FILE_ALL_ACCESS) != FILE_ALL_ACCESS) { good = false; break; }
            PSID sid = const_cast<DWORD*>(&ace->SidStart);
            if (EqualSid(sid, system.data())) seen |= 1;
            else if (EqualSid(sid, admins.data())) seen |= 2;
            else { good = false; break; }
        }
    }
    return good && seen == 3;
}
bool restricted(HANDLE handle, bool directory) {
    return inspectRestricted(handle, directory).valueOr(false);
}
Status checkDirectorySecurity(HANDLE handle, const ConfigFileHooks& hooks) {
    // Exercise the actual descriptor API failure path with an invalid handle
    // when the test barrier injects a failed read. Production has no hook.
    if (hooks.proceed && !hooks.proceed(ConfigFileStage::beforeDirectorySecurityRead))
        handle = nullptr;
    auto result = inspectRestricted(handle, true);
    if (!result) return result.error();
    if (!result.value()) return Error(ErrorCode::config_directory_security_invalid);
    return ok();
}
Handle openFile(const std::filesystem::path& path, DWORD access = GENERIC_READ | READ_CONTROL) {
    return Handle(CreateFileW(path.c_str(), access,
        FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
        FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_SEQUENTIAL_SCAN, nullptr));
}
Result<std::string> readHandle(HANDLE handle, std::size_t limit,
                             const ConfigFileHooks* hooks = nullptr) {
    if (limit == std::numeric_limits<std::size_t>::max()) return readError();
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(handle, &size) || size.QuadPart < 0) return readError();
    if (static_cast<std::uint64_t>(size.QuadPart) > limit)
        return Error(ErrorCode::config_too_large);
    LARGE_INTEGER zero{};
    if (!SetFilePointerEx(handle, zero, nullptr, FILE_BEGIN)) return readError();
    std::string bytes;
    bytes.reserve(static_cast<std::size_t>(size.QuadPart));
    std::array<char, 16384> block{};
    while (bytes.size() <= limit) {
        const auto want = static_cast<DWORD>(std::min(block.size(), limit + 1 - bytes.size()));
        DWORD count = 0;
        if (!ReadFile(handle, block.data(), want, &count, nullptr)) return readError();
        if (count == 0) break;
        bytes.append(block.data(), count);
        if (bytes.size() > limit) return Error(ErrorCode::config_too_large);
        if (hooks && hooks->proceed && !hooks->proceed(ConfigFileStage::readChunk)) return readError();
    }
    return bytes;
}
bool writeHandle(HANDLE handle, std::string_view bytes) {
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        const auto want = static_cast<DWORD>(std::min<std::size_t>(16384, bytes.size() - offset));
        DWORD written = 0;
        if (!WriteFile(handle, bytes.data() + offset, want, &written, nullptr) || written != want)
            return false;
        offset += written;
    }
    return true;
}
bool removeHandle(HANDLE handle) {
    FILE_DISPOSITION_INFO disposition{TRUE};
    return SetFileInformationByHandle(handle, FileDispositionInfo, &disposition, sizeof(disposition)) != FALSE;
}
bool renameHandle(HANDLE stage, const std::filesystem::path& target, bool replace) {
    const auto name = target.wstring();
    const auto size = offsetof(FILE_RENAME_INFO, FileName) + (name.size() + 1) * sizeof(wchar_t);
    if (size > std::numeric_limits<DWORD>::max()) return false;
    std::vector<unsigned char> storage(size, 0);
    auto* info = reinterpret_cast<FILE_RENAME_INFO*>(storage.data());
    info->Flags = replace ? FILE_RENAME_FLAG_REPLACE_IF_EXISTS | FILE_RENAME_FLAG_POSIX_SEMANTICS : 0;
    info->RootDirectory = nullptr;
    info->FileNameLength = static_cast<DWORD>(name.size() * sizeof(wchar_t));
    std::memcpy(info->FileName, name.data(), info->FileNameLength);
    return SetFileInformationByHandle(stage, kRenameInfoEx, info, static_cast<DWORD>(size)) != FALSE;
}
struct Journal {
    std::string commit, temporary, hash;
    ConfigFileIdentity staged;
    std::optional<ConfigFileIdentity> original;
    bool confirmed = false;
};
std::string serialize(const Journal& record) {
    std::ostringstream out;
    out << "PHPVPN-CONFIG-1\n" << record.commit << '\n' << record.temporary << '\n'
        << record.hash << '\n' << record.staged.volume << ' ' << record.staged.file << '\n'
        << (record.original ? 1 : 0) << ' ' << (record.original ? record.original->volume : 0)
        << ' ' << (record.original ? record.original->file : 0) << '\n';
    return out.str();
}
Result<Journal> parseJournal(std::string_view bytes) {
    std::istringstream in{std::string(bytes)};
    std::string version;
    Journal result;
    unsigned original = 0;
    ConfigFileIdentity old;
    if (!std::getline(in, version) || version != "PHPVPN-CONFIG-1" ||
        !std::getline(in, result.commit) || !Uuid::isValidV4(result.commit) ||
        !std::getline(in, result.temporary) || !Uuid::isValidV4(result.temporary) ||
        !std::getline(in, result.hash) || result.hash.size() != 64 || !isLowercaseHex(result.hash) ||
        !(in >> result.staged.volume >> result.staged.file >> original >> old.volume >> old.file) ||
        original > 1 || result.staged.file == 0)
        return Error(ErrorCode::journal_corrupt);
    in >> std::ws;
    if (!in.eof()) {
        std::string terminal;
        if (!(in >> terminal) || terminal != "CONFIRMED") return Error(ErrorCode::journal_corrupt);
        result.confirmed = true;
        in >> std::ws;
    }
    if (!in.eof() || (!original && (old.volume || old.file))) return Error(ErrorCode::journal_corrupt);
    if (original) result.original = old;
    const std::string canonical = serialize(result) + (result.confirmed ? "CONFIRMED\n" : "");
    if (bytes != canonical) return Error(ErrorCode::journal_corrupt);
    return result;
}

class WindowsConfigFiles final : public ConfigFiles {
public:
    WindowsConfigFiles(std::filesystem::path fixed, ConfigFileHooks hooks,
        std::unique_ptr<CryptoProvider> crypto, std::vector<Handle> directories)
        : path_(std::move(fixed)), hooks_(std::move(hooks)), crypto_(std::move(crypto)),
          directories_(std::move(directories)) {}
    const std::filesystem::path& path() const noexcept override { return path_; }

    Result<ConfigFileRead> readBounded(std::size_t maxBytes) override {
        std::lock_guard lock(mutex_);
        if (maxBytes > kMaxConfigFileBytes) return Error(ErrorCode::invalid_argument);
        Handle file = openFile(path_);
        if (!file) {
            if (missing(GetLastError())) return ConfigFileRead{};
            return readError();
        }
        if (!safeType(file.value, false) || !restricted(file.value, false)) return readError();
        auto id = identity(file.value);
        if (!id) return id.error();
        auto bytes = readHandle(file.value, maxBytes, &hooks_);
        if (!bytes) return bytes.error();
        auto digest = hash(bytes.value());
        if (!digest) return digest.error();
        return ConfigFileRead{true, std::move(bytes).value(), id.value(), std::move(digest).value()};
    }

    Result<ConfigFileRecovery> recover() override {
        std::lock_guard lock(mutex_);
        return recoverLocked();
    }

    Result<ConfigFilePublication> publish(std::string_view bytes, ConfigPublishMode mode,
                                        std::string_view commitId) override {
        std::lock_guard lock(mutex_);
        if (bytes.size() > kMaxConfigFileBytes) return Error(ErrorCode::config_too_large);
        if (!Uuid::isValidV4(commitId)) return Error(ErrorCode::invalid_argument);
        if (mode != ConfigPublishMode::replace && mode != ConfigPublishMode::createIfMissing)
            return Error(ErrorCode::invalid_argument);
        auto previous = recoverLocked();
        if (!previous) return previous.error();
        if (previous->state == ConfigRecoveryState::unknown) return Error(ErrorCode::journal_ambiguous);
        // Never silently lose the result of an old publication in order to retry it.
        if (previous->commitId == commitId) {
            if (previous->state != ConfigRecoveryState::confirmed) return writeError();
            auto digest = hash(bytes);
            if (!digest) return digest.error();
            if (digest.value() != previous->sha256) return Error(ErrorCode::invalid_argument);
            return ConfigFilePublication{ConfigPublishState::confirmed, std::string(commitId),
                previous->identity, previous->sha256};
        }
        PHPVPN_TRY(checkDirectorySecurity(directories_.back().value, hooks_));
        Handle original = openFile(path_);
        if (!original && !missing(GetLastError())) return writeError();
        if (original && (!safeType(original.value, false) || !restricted(original.value, false))) return writeError();
        if (original && mode == ConfigPublishMode::createIfMissing)
            return ConfigFilePublication{ConfigPublishState::alreadyExists, std::string(commitId), {}, {}};

        Journal record;
        record.commit = commitId;
        auto random = crypto_->randomUuidV4();
        if (!random) return random.error();
        record.temporary = std::move(random).value();
        auto digest = hash(bytes);
        if (!digest) return digest.error();
        record.hash = std::move(digest).value();
        if (original) {
            auto id = identity(original.value);
            if (!id) return id.error();
            record.original = id.value();
        }
        Descriptor descriptor;
        if (!descriptor.create()) return writeError();
        SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES), descriptor.value, FALSE};
        const auto stagePath = temporaryPath(record.temporary);
        Handle stage(CreateFileW(stagePath.c_str(), GENERIC_READ | GENERIC_WRITE | DELETE | READ_CONTROL,
            FILE_SHARE_READ | FILE_SHARE_DELETE, &security, CREATE_NEW,
            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
        if (!stage) return writeError();
        auto stageId = identity(stage.value);
        if (!stageId || !restricted(stage.value, false)) { removeHandle(stage.value); return writeError(); }
        record.staged = stageId.value();
        // Journal precedes secret bytes. A crash before this point can leave
        // only an empty protected staging file, never untracked secret bytes.
        Handle journal(CreateFileW(journalPath().c_str(), GENERIC_READ | GENERIC_WRITE | DELETE | READ_CONTROL,
            FILE_SHARE_READ | FILE_SHARE_DELETE, &security, CREATE_NEW,
            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
        if (!journal) { removeHandle(stage.value); return writeError(); }
        if (!restricted(journal.value, false) || !writeHandle(journal.value, serialize(record)) ||
            !proceed(ConfigFileStage::beforeJournalFlush) || !FlushFileBuffers(journal.value)) {
            removeHandle(stage.value); removeHandle(journal.value);
            return writeError();
        }
        if (!writeHandle(stage.value, bytes) || !proceed(ConfigFileStage::stageWritten) ||
            !proceed(ConfigFileStage::beforeStageFlush) || !FlushFileBuffers(stage.value)) {
            removeHandle(stage.value); removeHandle(journal.value);
            return writeError();
        }
        // Drop write access before publication, so the immutable inode can be
        // opened by concurrent readers without granting write sharing. Reopen
        // through the pinned directory and verify both identity and exact hash.
        stage.close();
        stage = openFile(stagePath, GENERIC_READ | DELETE | READ_CONTROL);
        auto reopenedId = stage ? identity(stage.value) : Result<ConfigFileIdentity>(readError());
        if (!stage || !safeType(stage.value, false) || !restricted(stage.value, false) ||
            !reopenedId || reopenedId.value() != record.staged) return unknown(record);
        auto stagedBytes = readHandle(stage.value, kMaxConfigFileBytes);
        if (!stagedBytes) return unknown(record);
        auto stagedHash = hash(stagedBytes.value());
        if (!stagedHash || stagedHash.value() != record.hash) return unknown(record);
        if (!proceed(ConfigFileStage::journalFlushed))
            return unknown(record);
        if (original) {
            if (!CreateHardLinkW(backupPath().c_str(), path_.c_str(), nullptr))
                return failBeforePublish(record, stage, journal);
            // The source name may have changed. Use only a backup with the exact
            // locked original identity and restricted security descriptor.
            Handle backup = openFile(backupPath());
            auto id = backup ? identity(backup.value) : Result<ConfigFileIdentity>(readError());
            if (!backup || !safeType(backup.value, false) || !restricted(backup.value, false) ||
                !id || id.value() != record.original.value())
                return unknown(record);
        }
        if (!proceed(ConfigFileStage::beforePublish))
            return failBeforePublish(record, stage, journal);
        // Revalidate the final name. The rename itself replaces the entry, never
        // writes through a substituted symlink. Ancestors remain pinned.
        Handle current = openFile(path_);
        if (!current && !missing(GetLastError())) return failBeforePublish(record, stage, journal);
        if (current && (!safeType(current.value, false) || !restricted(current.value, false)))
            return failBeforePublish(record, stage, journal);
        const bool replaced = renameHandle(stage.value, path_, mode == ConfigPublishMode::replace);
        const DWORD renameError = replaced ? ERROR_SUCCESS : GetLastError();
        if (!replaced) {
            auto failure = failBeforePublish(record, stage, journal);
            if (mode == ConfigPublishMode::createIfMissing &&
                (renameError == ERROR_ALREADY_EXISTS || renameError == ERROR_FILE_EXISTS) &&
                !failure.ok())
                return ConfigFilePublication{ConfigPublishState::alreadyExists, std::string(commitId), {}, {}};
            return failure;
        }
        if (!proceed(ConfigFileStage::afterPublish)) return unknown(record);
        Handle published = openFile(path_);
        auto publishedId = published ? identity(published.value) : Result<ConfigFileIdentity>(readError());
        if (!published || !safeType(published.value, false) || !restricted(published.value, false) ||
            !publishedId || publishedId.value() != record.staged)
            return unknown(record);
        // Persist the historical outcome. Later external edits or deletion must
        // not invalidate this receipt or impose an old-content revision guard.
        if (!writeHandle(journal.value, "CONFIRMED\n") || !FlushFileBuffers(journal.value))
            return unknown(record);
        // Journal and one backup remain until recovery or the next publication.
        // A later external overwrite cannot turn an ambiguous result into success.
        return ConfigFilePublication{ConfigPublishState::confirmed, record.commit, record.staged, record.hash};
    }

private:
    bool proceed(ConfigFileStage stage) { return !hooks_.proceed || hooks_.proceed(stage); }
    std::filesystem::path temporaryPath(const std::string& uuid) const {
        return path_.parent_path() / ("config.stage." + uuid + ".tmp");
    }
    std::filesystem::path backupPath() const { return path_.parent_path() / kBackup; }
    std::filesystem::path journalPath() const { return path_.parent_path() / kJournal; }
    Result<std::string> hash(std::string_view bytes) {
        auto result = crypto_->sha256({reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size()});
        if (!result) return result.error();
        return toLowercaseHex(result.value());
    }
    ConfigFilePublication unknown(const Journal& record) {
        return {ConfigPublishState::unknown, record.commit, {}, {}};
    }
    bool removeKnown(const std::filesystem::path& path, ConfigFileIdentity expected) {
        Handle file = openFile(path, GENERIC_READ | READ_CONTROL | DELETE);
        if (!file) return missing(GetLastError());
        auto actual = identity(file.value);
        return safeType(file.value, false) && restricted(file.value, false) && actual &&
            actual.value() == expected && removeHandle(file.value);
    }
    Result<ConfigFilePublication> failBeforePublish(const Journal& record, Handle& stage, Handle& journal) {
        // This branch is reached only when our rename did not happen.
        if (record.original && !removeKnown(backupPath(), record.original.value())) return unknown(record);
        if (!removeHandle(stage.value) || !removeHandle(journal.value)) return unknown(record);
        return writeError();
    }
    Result<ConfigFileRecovery> recoverLocked() {
        PHPVPN_TRY(checkDirectorySecurity(directories_.back().value, hooks_));
        Handle journal = openFile(journalPath(), GENERIC_READ | READ_CONTROL | DELETE);
        if (!journal) {
            if (missing(GetLastError())) return ConfigFileRecovery{};
            return readError();
        }
        if (!safeType(journal.value, false) || !restricted(journal.value, false))
            return Error(ErrorCode::journal_ambiguous);
        auto bytes = readHandle(journal.value, kJournalLimit);
        if (!bytes) return bytes.error();
        auto parsed = parseJournal(bytes.value());
        if (!parsed) return parsed.error();
        const auto& record = parsed.value();
        ConfigFileRecovery result{ConfigRecoveryState::unknown, record.commit, {}, {}};
        Handle target;
        if (!record.confirmed) {
            target = openFile(path_);
            if (!target && !missing(GetLastError())) return result;
            if (target && (!safeType(target.value, false) || !restricted(target.value, false))) return result;
        }
        auto targetId = target ? identity(target.value) : Result<ConfigFileIdentity>(ConfigFileIdentity{});
        if (!targetId) return result;
        if (record.confirmed) {
            result = {ConfigRecoveryState::confirmed, record.commit, record.staged, record.hash};
        } else if (target && targetId.value() == record.staged) {
            auto targetBytes = readHandle(target.value, kMaxConfigFileBytes);
            if (!targetBytes) return result;
            auto digest = hash(targetBytes.value());
            if (!digest || digest.value() != record.hash) return result;
            result = {ConfigRecoveryState::confirmed, record.commit, record.staged, record.hash};
        } else {
            Handle stage = openFile(temporaryPath(record.temporary));
            auto stageId = stage ? identity(stage.value) : Result<ConfigFileIdentity>(readError());
            if (!stage || !safeType(stage.value, false) || !restricted(stage.value, false) ||
                !stageId || stageId.value() != record.staged)
                return result;
            // Staged inode still has its original name: our atomic rename never
            // consumed it, regardless of a concurrent external target change.
            result.state = ConfigRecoveryState::notPublished;
        }
        if (!proceed(ConfigFileStage::beforeRecoveryCleanup)) return result;
        if (record.original && !removeKnown(backupPath(), record.original.value())) {
            if (result.state != ConfigRecoveryState::confirmed) result.state = ConfigRecoveryState::unknown;
            return result;
        }
        if (!removeKnown(temporaryPath(record.temporary), record.staged) || !removeHandle(journal.value)) {
            if (result.state != ConfigRecoveryState::confirmed) result.state = ConfigRecoveryState::unknown;
            return result;
        }
        return result;
    }
    std::filesystem::path path_;
    ConfigFileHooks hooks_;
    std::unique_ptr<CryptoProvider> crypto_;
    std::vector<Handle> directories_;
    std::mutex mutex_;
};
}  // namespace

Result<std::unique_ptr<ConfigFiles>> createWindowsConfigFiles(
    const std::filesystem::path& fixedPath, ConfigFileHooks hooks) {
    if (!fixedPath.is_absolute() || fixedPath.filename() != L"config.json" ||
        fixedPath != fixedPath.lexically_normal() || fixedPath.root_name().wstring().size() != 2)
        return Error(ErrorCode::invalid_argument);
    auto root = fixedPath.root_path();
    if (GetDriveTypeW(root.c_str()) != DRIVE_FIXED) return Error(ErrorCode::not_supported);
    std::array<wchar_t, 32> filesystem{};
    if (!GetVolumeInformationW(root.c_str(), nullptr, 0, nullptr, nullptr, nullptr,
            filesystem.data(), static_cast<DWORD>(filesystem.size())) ||
        _wcsicmp(filesystem.data(), L"NTFS") != 0)
        return Error(ErrorCode::not_supported);
    std::vector<Handle> directories;
    auto accumulated = root;
    auto openDirectory = [&](const std::filesystem::path& path) {
        Handle handle(CreateFileW(path.c_str(), FILE_LIST_DIRECTORY | READ_CONTROL,
            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
        if (!handle || !safeType(handle.value, true)) return false;
        directories.push_back(std::move(handle));
        return true;
    };
    if (!openDirectory(accumulated)) return readError();
    for (const auto& part : fixedPath.parent_path().relative_path()) {
        if (part.wstring().find_first_of(L":") != std::wstring::npos) return Error(ErrorCode::invalid_argument);
        accumulated /= part;
        if (!openDirectory(accumulated)) return readError();
    }
    PHPVPN_TRY(checkDirectorySecurity(directories.back().value, hooks));
    auto crypto = createPlatformCryptoProvider();
    if (!crypto) return crypto.error();
    return std::unique_ptr<ConfigFiles>(new WindowsConfigFiles(fixedPath,
        std::move(hooks), std::move(crypto).value(), std::move(directories)));
}
}  // namespace phpvpn
