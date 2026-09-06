#pragma once
#include <condition_variable>
#include <functional>
#include <map>
#include "core/config/config_store.h"
#include "core/ipc/wizard_ipc.h"
#include "core/runtime/vpn_engine.h"
#include "core/wizard/probe_client.h"

namespace phpvpn {
struct WizardScheduler {
    // The pipe server supplies its bounded existing executors. A false result
    // guarantees that the task was not admitted and will never run later.
    std::function<bool(std::function<void()>)> file;
    std::function<bool(std::function<void()>)> diagnostic;
    std::function<Result<std::unique_ptr<Transport>>()> transport = createPlatformTransport;
};
class WizardService {
public:
    WizardService(CryptoProvider& crypto, Clock& clock, ConfigStore& store, VpnEngine& engine,
        WizardScheduler scheduler, std::string instanceId);
    ~WizardService();
    std::string handle(const WizardRequest& request);
    void tick();
    void shutdown(); // Signal only, accepted scheduler tasks must then be drained.
    void cancelActiveProbe();
    Status interruptForConnect();
    const std::string& instanceId() const noexcept { return instanceId_; }
private:
    struct Operation {
        WizardOperationResult result;
        WizardFields fields;
        std::uint64_t generation = 0;
        bool cancelled = false;
        bool executing = false;
        bool committing = false;
        std::shared_ptr<ProbeClient> probe;
    };
    struct Session {
        WizardView view;
        std::optional<WizardDocument> original;
        std::int64_t lastContact = 0;
        std::uint64_t generation = 0, highestSequence = 0;
        std::string highestAttemptId;
        bool alive = true, interrupted = false;
        std::shared_ptr<Operation> operation;
        std::optional<std::string> urlEvidence;
        std::optional<WizardFields> secretEvidence, accessEvidence;
    };
    struct CommitRecord {
        std::string sessionId, commitId, fingerprint;
        std::uint64_t generation = 0;
        WizardCommitState state = WizardCommitState::pending;
        std::optional<WizardReceipt> receipt;
        std::optional<WizardError> error;
        std::shared_ptr<Operation> operation;
        std::optional<std::int64_t> closedAt;
        WizardReceipt preparedReceipt, preparedOperationReceipt;
        WizardError preparedUnknownError;
    };
    Result<JsonValue> execute(const WizardRequest& request);
    Result<JsonValue> open();
    Result<JsonValue> startProbe(const WizardRequest& request);
    Result<JsonValue> finish(const WizardRequest& request);
    Result<std::shared_ptr<Session>> sessionLocked(const WizardRequest& request, bool renew = true);
    Status readyLocked(const Session& session);
    WizardView viewLocked(const Session& session);
    void clearEvidenceLocked(Session& session);
    void clearOperationEvidenceLocked(Session& session, WizardOperationKind kind);
    void cancelLocked(const std::shared_ptr<Operation>& operation);
    void closeLocked(const std::shared_ptr<Session>& session, std::int64_t now);
    void expireLocked(std::int64_t now);
    void probeTask(std::shared_ptr<Session> session, std::shared_ptr<Operation> operation);
    void finishTask(std::shared_ptr<Session> session, std::shared_ptr<CommitRecord> record,
        WizardFields fields, bool contracts);
    static bool terminal(const Operation& operation);
    static JsonValue acknowledgement(const Operation& operation);
    CryptoProvider& crypto_;
    Clock& clock_;
    ConfigStore& store_;
    VpnEngine& engine_;
    WizardScheduler scheduler_;
    const std::string instanceId_, configPath_;
    std::mutex mutex_;
    std::condition_variable changed_;
    bool stopping_ = false;
    std::map<std::string, std::shared_ptr<Session>> sessions_;
    std::map<std::string, std::shared_ptr<CommitRecord>> commits_;
};
} // namespace phpvpn
