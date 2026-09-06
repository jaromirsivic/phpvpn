#pragma once
#include <mutex>
#include "core/config/config_document.h"
#include "platform/iface/config_files.h"

namespace phpvpn {
struct StoredConfigSnapshot {
    ConfigAvailability availability = ConfigAvailability::missing;
    std::optional<ErrorCode> reasonCode;
    std::string revisionToken;
    std::optional<ConfigFileIdentity> identity;
    std::string sha256; // Internal only. Never expose a secret-derived hash over IPC.
    std::optional<std::string> document;
    std::shared_ptr<const WizardDocument> wizard;
    ClientConfigPtr config;
    ConfigWarnings warnings;
};
struct ConfigCommit {
    ConfigPublishState state = ConfigPublishState::unknown;
    std::string commitId;
    std::string revisionToken;
};
struct DefaultConfigCreation {
    bool created = false;
    StoredConfigSnapshot snapshot;
};

// This is the only service owner of configuration disk operations. Its mutex
// never guards VPN state and callers must run file actions outside control I/O.
class ConfigStore {
public:
    ConfigStore(CryptoProvider& crypto, std::unique_ptr<ConfigFiles> files);
    const std::filesystem::path& path() const noexcept;
    Result<ConfigFileRecovery> recover();
    Result<StoredConfigSnapshot> read();
    Result<ClientConfigPtr> readValid();
    Result<ConfigCommit> save(std::string_view document, std::string_view commitId);
    Result<DefaultConfigCreation> createDefault();
private:
    Result<StoredConfigSnapshot> readLocked();
    Result<StoredConfigSnapshot> analyze(ConfigFileRead input);
    CryptoProvider& crypto_;
    std::unique_ptr<ConfigFiles> files_;
    std::mutex mutex_;
    StoredConfigSnapshot latest_;
};
} // namespace phpvpn
