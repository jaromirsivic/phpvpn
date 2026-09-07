#include "core/config/config_store.h"
#include "core/config/config_defaults.h"
#include "core/protocol/uuid.h"

namespace phpvpn {
namespace {
ConfigAvailability classify(ErrorCode code) {
    if (code == ErrorCode::config_too_large) return ConfigAvailability::too_large;
    if (code == ErrorCode::config_encoding_error) return ConfigAvailability::encoding_error;
    if (code == ErrorCode::config_not_found) return ConfigAvailability::missing;
    return ConfigAvailability::unreadable;
}
}
ConfigStore::ConfigStore(CryptoProvider& crypto, std::unique_ptr<ConfigFiles> files)
    : crypto_(crypto), files_(std::move(files)), path_(files_->path()) {}
ConfigStore::ConfigStore(CryptoProvider& crypto, std::filesystem::path fixedPath, Error unavailable)
    : crypto_(crypto), path_(std::move(fixedPath)), initializationError_(std::move(unavailable)) {}
const std::filesystem::path& ConfigStore::path() const noexcept { return path_; }
Status ConfigStore::operationalStatus() const {
    std::lock_guard lock(mutex_);
    return initializationError_ ? Status(*initializationError_) : ok();
}
Result<ConfigFileRecovery> ConfigStore::recover() {
    std::lock_guard lock(mutex_);
    latest_ = {};
    if (!files_) return *initializationError_;
    auto result = files_->recover();
    if (!result) initializationError_ = result.error();
    else if (result->state == ConfigRecoveryState::unknown) initializationError_ = Error(ErrorCode::journal_ambiguous);
    else initializationError_.reset();
    return result;
}
Result<StartupConfigState> ConfigStore::ensureDefaultAtStartup() {
    PHPVPN_TRY(operationalStatus());
    auto snapshot = read();
    if (!snapshot) return snapshot.error();
    if (snapshot->availability != ConfigAvailability::missing) return StartupConfigState::existing;
    if (snapshot->reasonCode != ErrorCode::config_not_found) return Error(ErrorCode::config_unreadable);
    auto created = createDefault();
    if (!created) return created.error();
    return created->created ? StartupConfigState::created : StartupConfigState::alreadyExists;
}
Result<StoredConfigSnapshot> ConfigStore::analyze(ConfigFileRead input) {
    StoredConfigSnapshot result;
    if (!input.found) {
        result.reasonCode = ErrorCode::config_not_found;
        return result;
    }
    auto normalized = normalizeConfigText(input.bytes);
    if (!normalized) {
        result.availability = classify(normalized.code());
        result.reasonCode = normalized.code();
        return result;
    }
    result.identity = input.identity;
    result.sha256 = std::move(input.sha256);
    // Reuse a token only for the same fully read file identity and exact bytes.
    if (latest_.document && latest_.identity == result.identity &&
        latest_.sha256 == result.sha256 && !result.sha256.empty())
        result.revisionToken = latest_.revisionToken;
    else PHPVPN_TRY_ASSIGN(result.revisionToken, crypto_.randomUuidV4());
    result.document = std::move(normalized).value();
    // Validators each receive original bytes and remove at most their single
    // leading BOM. Feeding already-normalized text would strip a second BOM.
    auto config = parseAndValidateConfig(input.bytes, result.warnings);
    if (config) {
        result.config = std::move(config).value();
        result.availability = ConfigAvailability::valid;
    } else {
        result.reasonCode = config.code();
        result.availability = ConfigAvailability::invalid;
    }
    auto wizard = analyzeWizardDocument(input.bytes);
    if (wizard) {
        result.wizard = std::make_shared<const WizardDocument>(std::move(wizard).value());
        if (!result.config) result.availability = ConfigAvailability::repairable;
    }
    return result;
}
Result<StoredConfigSnapshot> ConfigStore::readLocked() {
    if (!files_) {
        latest_ = {};
        latest_.availability = ConfigAvailability::unreadable;
        latest_.reasonCode = initializationError_->code;
        return latest_;
    }
    auto input = files_->readBounded(kMaxConfigFileBytes);
    if (!input) {
        // Never retain a previously valid document after a failed fresh read.
        latest_ = {};
        latest_.availability = classify(input.code());
        latest_.reasonCode = input.code();
        return latest_;
    }
    auto snapshot = analyze(std::move(input).value());
    if (!snapshot) { latest_ = {}; return snapshot.error(); }
    latest_ = snapshot.value();
    return snapshot;
}
Result<StoredConfigSnapshot> ConfigStore::read() {
    std::lock_guard lock(mutex_);
    return readLocked();
}
Result<ClientConfigPtr> ConfigStore::readValid() {
    PHPVPN_TRY(operationalStatus());
    auto snapshot = read();
    if (!snapshot) return snapshot.error();
    if (!snapshot->config) return Error(snapshot->reasonCode.value_or(ErrorCode::config_invalid_value));
    return snapshot->config;
}
Result<ConfigCommit> ConfigStore::save(std::string_view document, std::string_view commitId) {
    if (!Uuid::isValidV4(commitId)) return Error(ErrorCode::invalid_argument);
    std::string text;
    PHPVPN_TRY_ASSIGN(text, normalizeConfigText(document));
    ConfigWarnings warnings;
    auto config = parseAndValidateConfig(document, warnings);
    if (!config) return config.error();
    std::lock_guard lock(mutex_);
    if (initializationError_) return *initializationError_;
    // Allocate the receipt token before publication so allocation/randomness
    // failure can never turn a completed write into a reported failed write.
    std::string revision;
    PHPVPN_TRY_ASSIGN(revision, crypto_.randomUuidV4());
    StoredConfigSnapshot prepared;
    prepared.availability = ConfigAvailability::valid;
    prepared.revisionToken = revision;
    prepared.document = std::move(text);
    prepared.config = std::move(config).value();
    prepared.warnings = std::move(warnings);
    ConfigCommit receipt{ConfigPublishState::confirmed, std::string(commitId), std::move(revision)};
    auto publication = files_->publish(*prepared.document, ConfigPublishMode::replace, commitId);
    if (!publication) return publication.error();
    if (publication->state != ConfigPublishState::confirmed) {
        receipt.state = publication->state; receipt.revisionToken.clear(); return receipt;
    }
    // The commit receipt describes this publication, even if an administrator
    // immediately edits or deletes its file. A later action always reads anew.
    prepared.identity = publication->identity;
    prepared.sha256 = std::move(publication->sha256);
    latest_ = std::move(prepared);
    return receipt;
}
Result<DefaultConfigCreation> ConfigStore::createDefault() {
    const auto text = makeDefaultEditorDocument();
    ConfigWarnings warnings;
    auto valid = parseAndValidateConfig(text, warnings);
    if (!valid) return valid.error();
    std::lock_guard lock(mutex_);
    if (initializationError_) return *initializationError_;
    std::string id;
    PHPVPN_TRY_ASSIGN(id, crypto_.randomUuidV4());
    auto publication = files_->publish(text, ConfigPublishMode::createIfMissing, id);
    if (!publication) return publication.error();
    if (publication->state == ConfigPublishState::unknown)
        return Error(ErrorCode::config_write_failed, "the publication outcome is not confirmed");
    auto snapshot = readLocked();
    if (!snapshot) return snapshot.error();
    return DefaultConfigCreation{publication->state == ConfigPublishState::confirmed, std::move(snapshot).value()};
}
} // namespace phpvpn
