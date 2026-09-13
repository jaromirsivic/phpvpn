#pragma once
#include "core/config/config_document.h"
#include "core/runtime/status_snapshot.h"
#include "platform/iface/transport.h"

namespace phpvpn {
enum class WizardCommand {
    get_config_state, get_config_document, get_default_config, create_default_config,
    open, reset, keepalive, check_url, check_secret, check_access, get_operation,
    cancel_operation, cancel_attempt, resume, finish, get_commit, close
};
enum class WizardOperationKind { url, secret, access, finish };
enum class WizardOperationState { pending, running, cancelling, succeeded, failed, cancelled, interrupted };
enum class WizardCommitState { pending, confirmed, failed, unknown };
struct WizardRequest {
    WizardCommand command = WizardCommand::open;
    std::string requestId, serviceInstanceId, sessionId;
    std::string clientAttemptId, operationId, commitId;
    std::uint64_t attemptSequence = 0, connectionEpoch = 0;
    WizardFields fields;
    bool createDefaultContracts = false;
};
struct WizardError {
    std::string code, description;
    bool recoverable = false;
    std::optional<std::string> field;
    std::optional<int> httpStatus;
    std::string suggestedAction;
    std::optional<std::string> serverUrl, configPath;
};
struct WizardReceipt { std::string commitId, revisionToken; };
struct WizardOperationResult {
    std::string operationId;
    WizardOperationKind kind = WizardOperationKind::url;
    WizardOperationState state = WizardOperationState::pending;
    std::uint64_t connectionEpoch = 0;
    std::optional<std::string> clientAttemptId;
    std::optional<std::uint64_t> attemptSequence;
    std::optional<WizardError> error;
    std::optional<std::string> suggestedIp;
    std::size_t discardedRecordCount = 0;
    std::optional<WizardReceipt> receipt;
    std::optional<HttpDiagnostic> httpDiagnostic;
};
struct WizardView {
    std::string sessionId, serviceInstanceId, revisionToken;
    ConfigAvailability availability = ConfigAvailability::missing;
    WizardMode mode = WizardMode::create;
    std::optional<WizardFields> fields;
    std::optional<WizardContext> context;
    std::size_t contractCount = 0;
    std::vector<Ipv4Address> existingDefaultContractPeers;
    std::uint64_t connectionEpoch = 0;
    ConnectionState vpnState = ConnectionState::disconnected;
    ConnectionPhase vpnPhase = ConnectionPhase::idle;
};
inline constexpr std::int64_t kWizardLeaseMs = 60000;
inline constexpr std::int64_t kWizardKeepaliveMs = 15000;
inline constexpr std::int64_t kWizardReceiptRetentionMs = 600000;
inline constexpr std::size_t kWizardMaxSessions = 8;
inline constexpr std::size_t kWizardMaxReceipts = 128;
} // namespace phpvpn
