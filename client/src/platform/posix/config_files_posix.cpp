#include "platform/posix/config_files_posix.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#if defined(__APPLE__)
// sys/param.h first, as the statfs manual page orders them.
#include <sys/param.h>
#include <sys/mount.h>
#else
#include <sys/statfs.h>
#endif

#include <algorithm>
#include <array>
#include <iterator>
#include <limits>
#include <mutex>
#include <sstream>
#include <string>
#include <utility>

#include "core/base/hex.h"
#include "core/config/config_document.h"
#include "core/crypto/crypto_provider.h"
#include "core/protocol/uuid.h"
#include "platform/posix/fd.h"
#include "platform/posix/paths_posix.h"

namespace phpvpn {
namespace {
constexpr char kTarget[] = "config.json";
constexpr char kJournal[] = "config.publish-journal";
constexpr char kBackup[] = "config.previous";
constexpr std::size_t kJournalLimit = 1024;
constexpr std::size_t kBlock = 16384;
constexpr mode_t kFileMode = 0600;

Error readError() { return Error(ErrorCode::config_unreadable, "the configuration file cannot be safely read"); }
Error writeError() { return Error(ErrorCode::config_write_failed, "the configuration publication could not be completed"); }
Error insecureDirectory() { return Error(ErrorCode::config_directory_security_invalid); }
Error crashed() { return Error(ErrorCode::internal, "simulated crash"); }
Error remoteFilesystem() {
    return Error(ErrorCode::not_supported, "the configuration directory is not on a local filesystem");
}

ConfigFileIdentity identityOf(const struct stat& info) {
    return ConfigFileIdentity{static_cast<std::uint64_t>(info.st_dev), static_cast<std::uint64_t>(info.st_ino)};
}

// Linux fsync reaches stable storage. macOS fsync only hands the data to the
// drive; F_FULLFSYNC also flushes the drive cache, so the journal, the stage
// and the directory are durable before the next step. Filesystems without
// F_FULLFSYNC fall back to fsync.
int fullSync(int fd) {
#if defined(__APPLE__)
    if (::fcntl(fd, F_FULLFSYNC) == 0) return 0;
#endif
    return ::fsync(fd);
}

// Atomic rename that fails with EEXIST instead of replacing an existing target.
int renameNoReplace(int directory, const char* from, const char* to) {
#if defined(__APPLE__)
    return ::renameatx_np(directory, from, directory, to, RENAME_EXCL);
#else
    return ::renameat2(directory, from, directory, to, RENAME_NOREPLACE);
#endif
}

// Atomic swap of two existing names (exchange backup strategy). macOS has no
// RENAME_EXCHANGE; createPosixConfigFiles refuses the strategy there, so the
// stub is never reached.
int exchangeNames(int directory, const char* first, const char* second) {
#if defined(__APPLE__)
    (void)directory;
    (void)first;
    (void)second;
    errno = ENOTSUP;
    return -1;
#else
    return ::renameat2(directory, first, directory, second, RENAME_EXCHANGE);
#endif
}

// False when statfs fails. Otherwise `supported` tells whether the
// configuration may live on that filesystem.
bool readFilesystem(int fd, bool& supported) {
    struct statfs filesystem {};
    if (::fstatfs(fd, &filesystem) != 0) return false;
#if defined(__APPLE__)
    const char* name = filesystem.f_fstypename;
    supported = macosConfigFilesystemSupported(
        std::string_view(name, ::strnlen(name, sizeof(filesystem.f_fstypename))));
#else
    supported = linuxConfigFilesystemSupported(static_cast<std::uint32_t>(filesystem.f_type));
#endif
    return true;
}

bool writeAll(int fd, std::string_view bytes) {
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        const std::size_t want = std::min(kBlock, bytes.size() - offset);
        const ssize_t written = ::write(fd, bytes.data() + offset, want);
        if (written < 0 && errno == EINTR) continue;
        if (written <= 0) return false;
        offset += static_cast<std::size_t>(written);
    }
    return true;
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

std::string stageName(const std::string& uuid) { return "config.stage." + uuid + ".tmp"; }

class PosixConfigFiles final : public ConfigFiles {
public:
    PosixConfigFiles(std::filesystem::path fixed, PosixConfigFilePolicy policy,
        std::unique_ptr<CryptoProvider> crypto, UniqueFd directory)
        : path_(std::move(fixed)), policy_(std::move(policy)), crypto_(std::move(crypto)),
          directory_(std::move(directory)) {}
    const std::filesystem::path& path() const noexcept override { return path_; }

    Result<ConfigFileRead> readBounded(std::size_t maxBytes) override {
        std::lock_guard lock(mutex_);
        if (maxBytes > kMaxConfigFileBytes) return Error(ErrorCode::invalid_argument);
        UniqueFd file = openEntry(kTarget);
        if (!file) {
            if (errno == ENOENT) return ConfigFileRead{};
            return readError();
        }
        struct stat info {};
        if (!inspect(file.get(), info)) return readError();
        auto bytes = readHandle(file.get(), maxBytes, true);
        if (!bytes) return bytes.error();
        auto digest = hash(bytes.value());
        if (!digest) return digest.error();
        return ConfigFileRead{true, std::move(bytes).value(), identityOf(info), std::move(digest).value()};
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
        PHPVPN_TRY(checkDirectorySecurity());
        UniqueFd original = openEntry(kTarget);
        if (!original && errno != ENOENT) return writeError();
        struct stat originalInfo {};
        if (original && !inspect(original.get(), originalInfo)) return writeError();
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
        if (original) record.original = identityOf(originalInfo);
        original.reset();

        // The stage is created exclusively, its mode and owner are pinned before
        // any secret byte is written.
        const std::string stage = stageName(record.temporary);
        UniqueFd stageFile(::openat(directory_.get(), stage.c_str(),
            O_RDWR | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, kFileMode));
        if (!stageFile) return writeError();
        struct stat info {};
        if (::fstat(stageFile.get(), &info) != 0) {
            ::unlinkat(directory_.get(), stage.c_str(), 0);
            return writeError();
        }
        record.staged = identityOf(info);
        if (::fchmod(stageFile.get(), kFileMode) != 0 || ::fstat(stageFile.get(), &info) != 0 ||
            !freshlyCreated(info)) {
            removeKnown(stage.c_str(), record.staged);
            return writeError();
        }
        switch (at(PosixConfigFileStage::stageCreated)) {
        case PosixConfigFileAction::crash: return crashed();
        case PosixConfigFileAction::fail: removeKnown(stage.c_str(), record.staged); return writeError();
        case PosixConfigFileAction::proceed: break;
        }

        // Journal precedes secret bytes. A crash before this point can leave
        // only an empty protected staging file, never untracked secret bytes.
        UniqueFd journal(::openat(directory_.get(), kJournal,
            O_RDWR | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, kFileMode));
        if (!journal) { removeKnown(stage.c_str(), record.staged); return writeError(); }
        ConfigFileIdentity journalId;
        if (::fstat(journal.get(), &info) == 0) journalId = identityOf(info);
        const auto discard = [&] {
            removeKnown(stage.c_str(), record.staged);
            if (journalId.file != 0) removeKnown(kJournal, journalId);
            else ::unlinkat(directory_.get(), kJournal, 0);
            return writeError();
        };
        if (journalId.file == 0 || ::fchmod(journal.get(), kFileMode) != 0 ||
            ::fstat(journal.get(), &info) != 0 || !freshlyCreated(info) ||
            !writeAll(journal.get(), serialize(record)))
            return discard();
        switch (at(PosixConfigFileStage::beforeJournalFlush)) {
        case PosixConfigFileAction::crash: return crashed();
        case PosixConfigFileAction::fail: return discard();
        case PosixConfigFileAction::proceed: break;
        }
        // The directory sync makes both new names durable before the secret.
        if (fullSync(journal.get()) != 0 || fullSync(directory_.get()) != 0) return discard();
        switch (at(PosixConfigFileStage::journalFlushed)) {
        case PosixConfigFileAction::crash: return crashed();
        case PosixConfigFileAction::fail: return discard();
        case PosixConfigFileAction::proceed: break;
        }
        if (!writeAll(stageFile.get(), bytes)) return discard();
        for (const auto point : {PosixConfigFileStage::stageWritten, PosixConfigFileStage::beforeStageFlush}) {
            switch (at(point)) {
            case PosixConfigFileAction::crash: return crashed();
            case PosixConfigFileAction::fail: return discard();
            case PosixConfigFileAction::proceed: break;
            }
        }
        if (fullSync(stageFile.get()) != 0) return discard();

        // Drop write access before publication and reopen through the pinned
        // directory. Verify both identity and the exact hash of what will be
        // published.
        stageFile.reset();
        stageFile = openEntry(stage.c_str());
        if (!stageFile || !inspect(stageFile.get(), info) || identityOf(info) != record.staged)
            return unknown(record);
        auto stagedBytes = readHandle(stageFile.get(), kMaxConfigFileBytes, false);
        if (!stagedBytes) return unknown(record);
        auto stagedHash = hash(stagedBytes.value());
        if (!stagedHash || stagedHash.value() != record.hash) return unknown(record);
        stageFile.reset();
        switch (at(PosixConfigFileStage::stageVerified)) {
        case PosixConfigFileAction::crash: return crashed();
        case PosixConfigFileAction::fail: return unknown(record);
        case PosixConfigFileAction::proceed: break;
        }
        // The exchange strategy keeps the original by the swap below instead of
        // a hard link here (header of config_files_posix.h).
        const bool exchange = record.original.has_value() &&
            policy_.backupStrategy == PosixConfigBackupStrategy::exchange;
        if (record.original && !exchange) {
            if (::linkat(directory_.get(), kTarget, directory_.get(), kBackup, 0) != 0)
                return failBeforePublish(record, stage, journalId);
            // The source name may have changed. Use only a backup with the exact
            // original identity and a safe owner and mode.
            UniqueFd backup = openEntry(kBackup);
            if (!backup || !inspect(backup.get(), info) || identityOf(info) != record.original.value())
                return unknown(record);
            switch (at(PosixConfigFileStage::backupLinked)) {
            case PosixConfigFileAction::crash: return crashed();
            case PosixConfigFileAction::fail: return failBeforePublish(record, stage, journalId);
            case PosixConfigFileAction::proceed: break;
            }
        }
        switch (at(PosixConfigFileStage::beforePublish)) {
        case PosixConfigFileAction::crash: return crashed();
        case PosixConfigFileAction::fail: return failBeforePublish(record, stage, journalId);
        case PosixConfigFileAction::proceed: break;
        }
        // Revalidate the final name. The rename replaces the directory entry,
        // it never writes through a substituted symlink.
        {
            UniqueFd current = openEntry(kTarget);
            if (!current && errno != ENOENT) return failBeforePublish(record, stage, journalId);
            if (current && !inspect(current.get(), info)) return failBeforePublish(record, stage, journalId);
            // The swap moves whatever config.json is to the stage name, so it
            // runs only on the journaled original and only while no backup
            // exists. A target replaced during the save is never overwritten.
            if (exchange) {
                if (!current || identityOf(info) != record.original.value())
                    return failBeforePublish(record, stage, journalId);
                struct stat backupInfo {};
                if (::fstatat(directory_.get(), kBackup, &backupInfo, AT_SYMLINK_NOFOLLOW) == 0 || errno != ENOENT)
                    return failBeforePublish(record, stage, journalId);
            }
        }
        const int renamed = exchange ? exchangeNames(directory_.get(), stage.c_str(), kTarget) :
            mode == ConfigPublishMode::replace ?
            ::renameat(directory_.get(), stage.c_str(), directory_.get(), kTarget) :
            renameNoReplace(directory_.get(), stage.c_str(), kTarget);
        const int renameError = renamed == 0 ? 0 : errno;
        if (renamed != 0) {
            auto failure = failBeforePublish(record, stage, journalId);
            if (mode == ConfigPublishMode::createIfMissing && renameError == EEXIST && !failure.ok())
                return ConfigFilePublication{ConfigPublishState::alreadyExists, std::string(commitId), {}, {}};
            return failure;
        }
        switch (at(PosixConfigFileStage::afterPublish)) {
        case PosixConfigFileAction::crash: return crashed();
        case PosixConfigFileAction::fail: return unknown(record);
        case PosixConfigFileAction::proceed: break;
        }
        if (fullSync(directory_.get()) != 0) return unknown(record);
        switch (at(PosixConfigFileStage::afterDirectorySync)) {
        case PosixConfigFileAction::crash: return crashed();
        case PosixConfigFileAction::fail: return unknown(record);
        case PosixConfigFileAction::proceed: break;
        }
        if (exchange) {
            // Intermediate state of the exchange strategy: config.json is the
            // staged inode, the stage name holds the original. Move it to the
            // backup name; recoverLocked finishes the same move after a crash.
            if (!entryIs(stage.c_str(), record.original.value()) ||
                renameNoReplace(directory_.get(), stage.c_str(), kBackup) != 0 ||
                fullSync(directory_.get()) != 0)
                return unknown(record);
            switch (at(PosixConfigFileStage::backupMoved)) {
            case PosixConfigFileAction::crash: return crashed();
            case PosixConfigFileAction::fail: return unknown(record);
            case PosixConfigFileAction::proceed: break;
            }
        }
        {
            UniqueFd published = openEntry(kTarget);
            if (!published || !inspect(published.get(), info) || identityOf(info) != record.staged)
                return unknown(record);
        }
        switch (at(PosixConfigFileStage::beforeConfirm)) {
        case PosixConfigFileAction::crash: return crashed();
        case PosixConfigFileAction::fail: return unknown(record);
        case PosixConfigFileAction::proceed: break;
        }
        // Persist the historical outcome. Later external edits or deletion must
        // not invalidate this receipt or impose an old-content revision guard.
        if (!writeAll(journal.get(), "CONFIRMED\n") || fullSync(journal.get()) != 0)
            return unknown(record);
        // Journal and one backup remain until recovery or the next publication.
        // A later external overwrite cannot turn an ambiguous result into success.
        return ConfigFilePublication{ConfigPublishState::confirmed, record.commit, record.staged, record.hash};
    }

    Status checkDirectorySecurity() {
        return checkDirectory(directory_.get(), path_, policy_);
    }

    static Status checkDirectory(int directory, const std::filesystem::path& fixed,
                                 const PosixConfigFilePolicy& policy) {
        // Exercise the actual fstat failure path with an invalid descriptor when
        // the test barrier injects a failed read. Production has no hook.
        if (policy.hook) {
            switch (policy.hook(PosixConfigFileStage::beforeDirectorySecurityRead)) {
            case PosixConfigFileAction::crash: return crashed();
            case PosixConfigFileAction::fail: directory = -1; break;
            case PosixConfigFileAction::proceed: break;
            }
        }
        struct stat info {};
        bool supported = false;
        if (::fstat(directory, &info) != 0 || !readFilesystem(directory, supported)) return readError();
        if (!supported) return remoteFilesystem();
        if (!S_ISDIR(info.st_mode) || info.st_uid != policy.expectedOwner ||
            (info.st_mode & (S_IWGRP | S_IWOTH)) != 0)
            return insecureDirectory();
        // The fixed name must still lead to the pinned directory through safe
        // ancestors, a moved or replaced directory is never used.
        auto current = openTrustedDirectory(fixed.parent_path(), policy.expectedOwner, policy.trustRoot);
        if (!current) return current.error();
        struct stat named {};
        if (::fstat(current->get(), &named) != 0) return readError();
        if (identityOf(named) != identityOf(info)) return insecureDirectory();
        return ok();
    }

private:
    PosixConfigFileAction at(PosixConfigFileStage stage) const {
        return policy_.hook ? policy_.hook(stage) : PosixConfigFileAction::proceed;
    }
    UniqueFd openEntry(const char* name) const {
        // O_NONBLOCK keeps a planted FIFO from blocking the open, the type check
        // rejects it afterwards.
        return UniqueFd(::openat(directory_.get(), name,
            O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_NOCTTY | O_CLOEXEC));
    }
    bool sameEntry(const char* name, const struct stat& info) const {
        struct stat other {};
        return ::fstatat(directory_.get(), name, &other, AT_SYMLINK_NOFOLLOW) == 0 &&
            S_ISREG(other.st_mode) && identityOf(other) == identityOf(info);
    }
    // The name is a regular file with exactly this inode identity.
    bool entryIs(const char* name, ConfigFileIdentity expected) const {
        struct stat other {};
        return ::fstatat(directory_.get(), name, &other, AT_SYMLINK_NOFOLLOW) == 0 &&
            S_ISREG(other.st_mode) && identityOf(other) == expected;
    }
    // A single name, or exactly the transient pair config.json/config.previous
    // of one inode that the publication itself creates.
    bool linkCountSafe(const struct stat& info) const {
        if (info.st_nlink == 1) return true;
        return info.st_nlink == 2 && sameEntry(kTarget, info) && sameEntry(kBackup, info);
    }
    bool safe(const struct stat& info) const {
        return S_ISREG(info.st_mode) && info.st_uid == policy_.expectedOwner &&
            (info.st_mode & (S_IWGRP | S_IWOTH)) == 0 && linkCountSafe(info);
    }
    bool freshlyCreated(const struct stat& info) const {
        return S_ISREG(info.st_mode) && info.st_uid == policy_.expectedOwner &&
            (info.st_mode & 07777) == kFileMode && info.st_nlink == 1;
    }
    bool inspect(int fd, struct stat& info) const {
        return ::fstat(fd, &info) == 0 && safe(info);
    }
    Result<std::string> readHandle(int fd, std::size_t limit, bool hooked) {
        if (limit == std::numeric_limits<std::size_t>::max()) return readError();
        struct stat info {};
        if (::fstat(fd, &info) != 0 || info.st_size < 0) return readError();
        if (static_cast<std::uint64_t>(info.st_size) > limit) return Error(ErrorCode::config_too_large);
        if (::lseek(fd, 0, SEEK_SET) != 0) return readError();
        std::string bytes;
        bytes.reserve(static_cast<std::size_t>(info.st_size));
        std::array<char, kBlock> block{};
        // Reads one detection byte beyond the limit, so growth after the size
        // check is reported as too large instead of being truncated.
        while (bytes.size() <= limit) {
            const auto want = std::min(block.size(), limit + 1 - bytes.size());
            const ssize_t count = ::read(fd, block.data(), want);
            if (count < 0 && errno == EINTR) continue;
            if (count < 0) return readError();
            if (count == 0) break;
            bytes.append(block.data(), static_cast<std::size_t>(count));
            if (bytes.size() > limit) return Error(ErrorCode::config_too_large);
            if (hooked) {
                switch (at(PosixConfigFileStage::readChunk)) {
                case PosixConfigFileAction::crash: return crashed();
                case PosixConfigFileAction::fail: return readError();
                case PosixConfigFileAction::proceed: break;
                }
            }
        }
        return bytes;
    }
    Result<std::string> hash(std::string_view bytes) {
        auto result = crypto_->sha256({reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size()});
        if (!result) return result.error();
        return toLowercaseHex(result.value());
    }
    ConfigFilePublication unknown(const Journal& record) {
        return {ConfigPublishState::unknown, record.commit, {}, {}};
    }
    // Removes the name only while it still refers to the expected inode with a
    // safe owner and mode. A missing name counts as removed.
    bool removeKnown(const char* name, ConfigFileIdentity expected) {
        struct stat info {};
        if (::fstatat(directory_.get(), name, &info, AT_SYMLINK_NOFOLLOW) != 0) return errno == ENOENT;
        if (!S_ISREG(info.st_mode) || info.st_uid != policy_.expectedOwner ||
            (info.st_mode & (S_IWGRP | S_IWOTH)) != 0 || identityOf(info) != expected)
            return false;
        return ::unlinkat(directory_.get(), name, 0) == 0 || errno == ENOENT;
    }
    Result<ConfigFilePublication> failBeforePublish(const Journal& record, const std::string& stage,
                                                    ConfigFileIdentity journal) {
        // This branch is reached only when our rename did not happen.
        if (record.original && !removeKnown(kBackup, record.original.value())) return unknown(record);
        if (!removeKnown(stage.c_str(), record.staged) || !removeKnown(kJournal, journal)) return unknown(record);
        fullSync(directory_.get());
        return writeError();
    }
    Result<ConfigFileRecovery> recoverLocked() {
        PHPVPN_TRY(checkDirectorySecurity());
        UniqueFd journal = openEntry(kJournal);
        if (!journal) {
            if (errno == ENOENT) return ConfigFileRecovery{};
            if (errno == ELOOP) return Error(ErrorCode::journal_ambiguous);
            return readError();
        }
        struct stat journalInfo {};
        if (!inspect(journal.get(), journalInfo)) return Error(ErrorCode::journal_ambiguous);
        auto bytes = readHandle(journal.get(), kJournalLimit, false);
        if (!bytes) return bytes.error();
        auto parsed = parseJournal(bytes.value());
        if (!parsed) return parsed.error();
        const auto& record = parsed.value();
        const std::string stage = stageName(record.temporary);
        ConfigFileRecovery result{ConfigRecoveryState::unknown, record.commit, {}, {}};
        UniqueFd target;
        struct stat targetInfo {};
        if (!record.confirmed) {
            target = openEntry(kTarget);
            if (!target && errno != ENOENT) return result;
            if (target && !inspect(target.get(), targetInfo)) return result;
        }
        if (record.confirmed) {
            result = {ConfigRecoveryState::confirmed, record.commit, record.staged, record.hash};
        } else if (target && identityOf(targetInfo) == record.staged) {
            auto targetBytes = readHandle(target.get(), kMaxConfigFileBytes, false);
            if (!targetBytes) return result;
            auto digest = hash(targetBytes.value());
            if (!digest || digest.value() != record.hash) return result;
            result = {ConfigRecoveryState::confirmed, record.commit, record.staged, record.hash};
        } else {
            UniqueFd staged = openEntry(stage.c_str());
            struct stat stageInfo {};
            if (!staged || !inspect(staged.get(), stageInfo) || identityOf(stageInfo) != record.staged)
                return result;
            // Staged inode still has its original name: our atomic rename never
            // consumed it, regardless of a concurrent external target change.
            result.state = ConfigRecoveryState::notPublished;
        }
        switch (at(PosixConfigFileStage::beforeRecoveryCleanup)) {
        case PosixConfigFileAction::crash: return crashed();
        case PosixConfigFileAction::fail: return result;
        case PosixConfigFileAction::proceed: break;
        }
        // Exchange strategy, crash between the swap and the move of the
        // original: config.json is the staged inode (so the result is
        // confirmed) and the stage name still holds the original inode. Finish
        // the move, so the cleanup below sees the completed publication of
        // either strategy. Repeating it is harmless: once moved, the stage name
        // no longer holds the original. A name in the way leaves the journal for
        // the next recovery, as any failed cleanup does.
        if (record.original && result.state == ConfigRecoveryState::confirmed &&
            entryIs(stage.c_str(), record.original.value())) {
            if (renameNoReplace(directory_.get(), stage.c_str(), kBackup) != 0) return result;
            fullSync(directory_.get());
        }
        if (record.original && !removeKnown(kBackup, record.original.value())) {
            if (result.state != ConfigRecoveryState::confirmed) result.state = ConfigRecoveryState::unknown;
            return result;
        }
        if (!removeKnown(stage.c_str(), record.staged) || !removeKnown(kJournal, identityOf(journalInfo))) {
            if (result.state != ConfigRecoveryState::confirmed) result.state = ConfigRecoveryState::unknown;
            return result;
        }
        // A cleanup lost to a power failure is repeated by the next recovery.
        fullSync(directory_.get());
        return result;
    }
    std::filesystem::path path_;
    PosixConfigFilePolicy policy_;
    std::unique_ptr<CryptoProvider> crypto_;
    UniqueFd directory_;
    std::mutex mutex_;
};
}  // namespace

#if defined(__linux__)
bool linuxConfigFilesystemSupported(std::uint32_t magic) noexcept {
    // Values of <linux/magic.h> and fs/smb, spelled out so the policy does not
    // depend on the kernel header version of the build host.
    constexpr std::uint32_t refused[] = {
        0x6969u,      // NFS
        0x517Bu,      // SMB (smbfs)
        0xFF534D42u,  // CIFS
        0xFE534D42u,  // SMB2
        0x65735546u,  // FUSE
        0x01021997u,  // 9P
        0x00C36400u,  // Ceph
        0x5346414Fu,  // AFS
        0x6B414653u,  // kAFS
        0x73757245u,  // Coda
    };
    return std::find(std::begin(refused), std::end(refused), magic) == std::end(refused);
}
#endif

#if defined(__APPLE__)
bool macosConfigFilesystemSupported(std::string_view fstypename) noexcept {
    // Network filesystems and the FUSE implementations, by their statfs
    // f_fstypename. ASCII case is ignored.
    constexpr std::string_view refused[] = {
        "nfs", "smbfs", "afpfs", "webdav", "cifs", "osxfuse", "macfuse", "fuse", "ftp",
    };
    const auto lower = [](char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; };
    for (const std::string_view name : refused) {
        if (name.size() == fstypename.size() &&
            std::equal(name.begin(), name.end(), fstypename.begin(),
                       [&](char expected, char actual) { return expected == lower(actual); }))
            return false;
    }
    return true;
}
#endif

Result<std::unique_ptr<ConfigFiles>> createPosixConfigFiles(
    const std::filesystem::path& fixedPath, PosixConfigFilePolicy policy) {
    if (!fixedPath.is_absolute() || fixedPath.filename() != kTarget ||
        fixedPath != fixedPath.lexically_normal() || fixedPath.native().find('\0') != std::string::npos)
        return Error(ErrorCode::invalid_argument);
#if defined(__APPLE__)
    if (policy.backupStrategy == PosixConfigBackupStrategy::exchange)
        return Error(ErrorCode::not_supported, "the exchange backup strategy needs RENAME_EXCHANGE, macOS has none");
#endif
    auto directory = openTrustedDirectory(fixedPath.parent_path(), policy.expectedOwner, policy.trustRoot);
    if (!directory) return directory.error();
    bool supported = false;
    if (!readFilesystem(directory->get(), supported)) return readError();
    if (!supported) return remoteFilesystem();
    PHPVPN_TRY(PosixConfigFiles::checkDirectory(directory->get(), fixedPath, policy));
    auto crypto = createPlatformCryptoProvider();
    if (!crypto) return crypto.error();
    return std::unique_ptr<ConfigFiles>(new PosixConfigFiles(fixedPath,
        std::move(policy), std::move(crypto).value(), std::move(directory).value()));
}
}  // namespace phpvpn
