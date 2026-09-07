#include "core/wizard/wizard_service.h"
#include "core/config/config_defaults.h"
#include "core/base/hex.h"
#include "core/crypto/canonical.h"
#include "core/wizard/wizard_defaults.h"
#include <chrono>
#include <limits>
#include <algorithm>

namespace phpvpn {
namespace {
using J = JsonValue;
J str(std::string_view text) { return J::makeString(std::string(text)); }
J nullable(std::string_view text) { return text.empty() ? J::makeNull() : str(text); }
std::string utf8Path(const std::filesystem::path& path) {
    const auto text = path.u8string();
    return {reinterpret_cast<const char*>(text.data()), text.size()};
}
bool offline(const ConnectionIdentity& identity) {
    return identity.state == ConnectionState::disconnected && identity.phase == ConnectionPhase::idle && !identity.connectPending;
}
std::string commitKey(std::string_view session, std::string_view commit) { return std::string(session) + "/" + std::string(commit); }
}
WizardService::WizardService(CryptoProvider& crypto, Clock& clock, ConfigStore& store, VpnEngine& engine,
    WizardScheduler scheduler, std::string instanceId)
    : crypto_(crypto), clock_(clock), store_(store), engine_(engine), scheduler_(std::move(scheduler)),
      instanceId_(std::move(instanceId)), configPath_(utf8Path(store.path())) {
    engine_.setDiagnosticHooks([this] { return interruptForConnect(); }, [this] { cancelActiveProbe(); });
}
WizardService::~WizardService() { engine_.setDiagnosticHooks({}, {}); }
bool WizardService::terminal(const Operation& operation) {
    return !operation.executing && operation.result.state != WizardOperationState::pending &&
        operation.result.state != WizardOperationState::running && operation.result.state != WizardOperationState::cancelling;
}
J WizardService::acknowledgement(const Operation& operation) {
    // Admission always acknowledges pending, even if an executor finished before
    // IPC delivery. The caller obtains the actual terminal state separately.
    return J::makeObject({{"operationId", str(operation.result.operationId)}, {"operationState", str("pending")}});
}
void WizardService::clearEvidenceLocked(Session& session) {
    session.urlEvidence.reset(); session.secretEvidence.reset(); session.accessEvidence.reset();
    session.suggestedIpEvidence.reset();
}
void WizardService::clearOperationEvidenceLocked(Session& session, WizardOperationKind kind) {
    // Cancelling a later checkpoint does not revoke an earlier successful
    // checkpoint. Its exact-field proof remains necessary for an explicit retry.
    if (kind == WizardOperationKind::url) session.urlEvidence.reset();
    if (kind == WizardOperationKind::url || kind == WizardOperationKind::secret) {
        session.secretEvidence.reset(); session.suggestedIpEvidence.reset();
    }
    session.accessEvidence.reset();
}
void WizardService::cancelLocked(const std::shared_ptr<Operation>& operation) {
    if (!operation || terminal(*operation) || operation->committing) return;
    operation->cancelled = true;
    operation->result.state = WizardOperationState::cancelling;
    if (operation->probe) operation->probe->cancel();
}
void WizardService::closeLocked(const std::shared_ptr<Session>& session, std::int64_t now) {
    if (!session->alive) return;
    session->alive = false;
    clearEvidenceLocked(*session);
    cancelLocked(session->operation);
    for (auto& [key, record] : commits_) {
        (void)key;
        if (record->sessionId == session->view.sessionId && !record->closedAt) record->closedAt = now;
    }
}
void WizardService::expireLocked(std::int64_t now) {
    for (const auto& [id, session] : sessions_) {
        (void)id;
        if (now < session->lastContact || now - session->lastContact >= kWizardLeaseMs) closeLocked(session, now);
    }
    std::erase_if(sessions_, [](const auto& entry) {
        return !entry.second->alive && (!entry.second->operation || terminal(*entry.second->operation));
    });
    std::erase_if(commits_, [now](const auto& entry) {
        const auto& record = *entry.second;
        return record.closedAt && record.state != WizardCommitState::pending && terminal(*record.operation) &&
            now >= *record.closedAt && now - *record.closedAt >= kWizardReceiptRetentionMs;
    });
}
void WizardService::tick() { std::lock_guard lock(mutex_); expireLocked(clock_.leaseMonotonicMs()); }
void WizardService::shutdown() {
    std::lock_guard lock(mutex_);
    stopping_ = true;
    for (const auto& [id, session] : sessions_) { (void)id; closeLocked(session, clock_.leaseMonotonicMs()); }
}
void WizardService::cancelActiveProbe() {
    std::lock_guard lock(mutex_);
    for (const auto& [id, session] : sessions_) {
        (void)id;
        if (session->operation && session->operation->result.kind != WizardOperationKind::finish) cancelLocked(session->operation);
    }
}
Status WizardService::interruptForConnect() {
    std::unique_lock lock(mutex_);
    for (const auto& [id, session] : sessions_) {
        (void)id;
        session->interrupted = true;
        clearEvidenceLocked(*session);
        if (session->operation && session->operation->result.kind != WizardOperationKind::finish) cancelLocked(session->operation);
    }
    if (!changed_.wait_for(lock, std::chrono::milliseconds(2000), [this] {
        for (const auto& [id, session] : sessions_) {
            (void)id;
            if (session->operation && session->operation->result.kind != WizardOperationKind::finish && !terminal(*session->operation)) return false;
        }
        return true;
    })) return Error(ErrorCode::busy, "the interrupted diagnostic has not terminated yet");
    return ok();
}
Result<std::shared_ptr<WizardService::Session>> WizardService::sessionLocked(const WizardRequest& request, bool renew) {
    if (request.serviceInstanceId != instanceId_) return Error(ErrorCode::wizard_session_expired);
    expireLocked(clock_.leaseMonotonicMs());
    const auto found = sessions_.find(request.sessionId);
    if (found == sessions_.end() || !found->second->alive || stopping_) return Error(ErrorCode::wizard_session_expired);
    if (renew) found->second->lastContact = clock_.leaseMonotonicMs();
    return found->second;
}
Status WizardService::readyLocked(const Session& session) {
    const auto identity = engine_.connectionIdentity();
    if (session.interrupted || session.view.connectionEpoch != identity.epoch || !offline(identity))
        return Error(ErrorCode::wizard_connection_interrupted);
    if (!session.original) return Error(ErrorCode::wizard_prerequisite_failed);
    return ok();
}
WizardView WizardService::viewLocked(const Session& session) {
    auto view = session.view;
    const auto identity = engine_.connectionIdentity();
    view.vpnState = identity.state; view.vpnPhase = identity.phase;
    return view;
}
Result<J> WizardService::open() {
    // Expensive file work occurs before taking the session registry mutex.
    auto snapshot = store_.read();
    if (!snapshot) return snapshot.error();
    std::string id;
    PHPVPN_TRY_ASSIGN(id, crypto_.randomUuidV4());
    auto session = std::make_shared<Session>();
    session->view.sessionId = id; session->view.serviceInstanceId = instanceId_;
    session->view.revisionToken = snapshot->revisionToken; session->view.availability = snapshot->availability;
    if (snapshot->availability == ConfigAvailability::missing) {
        session->original = makeNewWizardDocument(); session->view.mode = WizardMode::create;
    } else if (snapshot->wizard) {
        session->original = *snapshot->wizard;
        session->view.mode = snapshot->config ? WizardMode::edit : WizardMode::repair;
    } else session->view.mode = WizardMode::reset;
    if (session->original) {
        session->view.fields = session->original->fields; session->view.context = session->original->context;
        session->view.contractCount = session->original->contractPeers.size();
        for (const auto peer : defaultContractCandidates(session->original->context.vpnCidr)) {
            const auto& peers = session->original->contractPeers;
            if (std::find(peers.begin(), peers.end(), peer) != peers.end()) session->view.existingDefaultContractPeers.push_back(peer);
        }
    }
    std::lock_guard lock(mutex_);
    expireLocked(clock_.leaseMonotonicMs());
    if (stopping_) return Error(ErrorCode::service_not_running);
    if (sessions_.size() >= kWizardMaxSessions) return Error(ErrorCode::busy);
    session->lastContact = clock_.leaseMonotonicMs();
    session->view.connectionEpoch = engine_.connectionIdentity().epoch;
    sessions_.emplace(id, session);
    return WizardIpc::viewValue(viewLocked(*session));
}
std::string WizardService::handle(const WizardRequest& request) {
    try {
        auto result = execute(request);
        if (!result) return WizardIpc::buildError(request.requestId, WizardIpc::safeError(result.error(), {}, {}, {}, configPath_));
        return WizardIpc::buildResult(request.requestId, std::move(result).value());
    } catch (...) {
        return WizardIpc::buildError(request.requestId, WizardIpc::safeError(Error(ErrorCode::internal)));
    }
}
Result<J> WizardService::execute(const WizardRequest& request) {
    if (request.command == WizardCommand::open) return open();
    if (request.command == WizardCommand::check_url || request.command == WizardCommand::check_secret || request.command == WizardCommand::check_access)
        return startProbe(request);
    if (request.command == WizardCommand::finish) return finish(request);
    if (request.command == WizardCommand::get_default_config)
        return J::makeObject({{"document", str(makeDefaultEditorDocument())}});
    if (request.command == WizardCommand::create_default_config) {
        auto created = store_.createDefault();
        if (!created) return created.error();
        if (!created->snapshot.document) return Error(created->snapshot.reasonCode.value_or(ErrorCode::config_unreadable));
        return J::makeObject({{"created", J::makeBool(created->created)}, {"configPath", str(configPath_)},
            {"availability", str(configAvailabilityName(created->snapshot.availability))}, {"document", str(*created->snapshot.document)},
            {"revisionToken", str(created->snapshot.revisionToken)}});
    }
    if (request.command == WizardCommand::get_config_state || request.command == WizardCommand::get_config_document) {
        auto snapshot = store_.read();
        if (!snapshot) return snapshot.error();
        if (request.command == WizardCommand::get_config_document && !snapshot->document && snapshot->availability != ConfigAvailability::missing)
            return Error(snapshot->reasonCode.value_or(ErrorCode::config_unreadable));
        JsonObject result{{"configPath", str(configPath_)}, {"availability", str(configAvailabilityName(snapshot->availability))},
            {"reasonCode", snapshot->reasonCode ? str(errorCodeName(*snapshot->reasonCode)) : J::makeNull()},
            {"revisionToken", nullable(snapshot->revisionToken)}};
        if (request.command == WizardCommand::get_config_document)
            result.emplace_back("document", snapshot->document ? str(*snapshot->document) : J::makeNull());
        else {
            const auto identity = engine_.connectionIdentity();
            result.emplace_back("serviceInstanceId", str(instanceId_));
            result.emplace_back("connectionEpoch", J::makeUnsignedInteger(identity.epoch));
            result.emplace_back("vpnState", str(connectionStateName(identity.state)));
            result.emplace_back("vpnPhase", str(connectionPhaseName(identity.phase)));
        }
        return J::makeObject(std::move(result));
    }
    std::lock_guard lock(mutex_);
    if (request.command == WizardCommand::get_commit) {
        if (request.serviceInstanceId != instanceId_) return Error(ErrorCode::wizard_session_expired);
        expireLocked(clock_.leaseMonotonicMs());
        const auto found = commits_.find(commitKey(request.sessionId, request.commitId));
        if (found == commits_.end()) return J::makeObject({{"commitState", str("unknown")}, {"receipt", J::makeNull()}, {"error", J::makeNull()}});
        const auto& record = *found->second;
        constexpr const char* names[] = {"pending", "confirmed", "failed", "unknown"};
        return J::makeObject({{"commitState", str(names[static_cast<int>(record.state)])},
            {"receipt", record.receipt ? WizardIpc::receiptValue(*record.receipt) : J::makeNull()},
            {"error", record.error ? WizardIpc::errorValue(*record.error) : J::makeNull()}});
    }
    std::shared_ptr<Session> session;
    PHPVPN_TRY_ASSIGN(session, sessionLocked(request));
    const auto currentIdentity = engine_.connectionIdentity();
    if (currentIdentity.epoch != session->view.connectionEpoch) {
        session->interrupted = true;
        clearEvidenceLocked(*session);
        cancelLocked(session->operation);
    }
    switch (request.command) {
        case WizardCommand::keepalive:
            if (session->interrupted) return Error(ErrorCode::wizard_connection_interrupted);
            return J::makeObject({{"alive", J::makeBool(true)}, {"leaseDurationMs", J::makeInteger(kWizardLeaseMs)}});
        case WizardCommand::close:
            if (session->operation && session->operation->result.kind == WizardOperationKind::finish && !terminal(*session->operation))
                return Error(ErrorCode::busy);
            closeLocked(session, clock_.leaseMonotonicMs());
            return J::makeObject({{"closed", J::makeBool(true)}});
        case WizardCommand::reset: case WizardCommand::resume: {
            if (session->operation && !terminal(*session->operation)) { cancelLocked(session->operation); return Error(ErrorCode::busy); }
            if (session->generation == (std::numeric_limits<std::uint64_t>::max)()) return Error(ErrorCode::state_conflict);
            const auto identity = engine_.connectionIdentity();
            // An idle wizard may only know its previously acknowledged epoch:
            // keepalive reports interruption as an error without an epoch field.
            // Resume explicitly adopts the current offline service epoch, while
            // rejecting a value that belongs to neither this view nor the service.
            if (request.command == WizardCommand::resume && (!offline(identity) ||
                (request.connectionEpoch != session->view.connectionEpoch && request.connectionEpoch != identity.epoch)))
                return Error(ErrorCode::wizard_connection_interrupted);
            ++session->generation;
            clearEvidenceLocked(*session);
            session->operation.reset();
            if (request.command == WizardCommand::reset) {
                session->original = makeNewWizardDocument();
                session->view.mode = WizardMode::reset;
                session->view.fields = session->original->fields;
                session->view.context = session->original->context;
                session->view.contractCount = 0;
                session->view.existingDefaultContractPeers.clear();
            } else {
                session->interrupted = false;
                session->view.connectionEpoch = identity.epoch;
            }
            return WizardIpc::viewValue(viewLocked(*session));
        }
        case WizardCommand::get_operation: case WizardCommand::cancel_operation: {
            auto operation = session->operation;
            if (!operation || operation->result.operationId != request.operationId) {
                operation.reset();
                for (const auto& [key, record] : commits_) {
                    (void)key;
                    if (record->sessionId == request.sessionId && record->operation->result.operationId == request.operationId) operation = record->operation;
                }
            }
            if (!operation) return Error(ErrorCode::wizard_attempt_closed);
            if (session->interrupted && operation->result.kind != WizardOperationKind::finish && terminal(*operation)) {
                operation->result.state = WizardOperationState::interrupted;
                operation->result.error = WizardIpc::safeError(Error(ErrorCode::wizard_connection_interrupted));
            }
            if (request.command == WizardCommand::cancel_operation) {
                if (operation->result.kind == WizardOperationKind::finish && !terminal(*operation)) return Error(ErrorCode::busy);
                if (operation->result.kind != WizardOperationKind::finish) clearOperationEvidenceLocked(*session, operation->result.kind);
                cancelLocked(operation);
                auto value = WizardIpc::operationValue(operation->result);
                return J::makeObject({{"operationId", str(request.operationId)}, {"operationState", *value.find("operationState")}});
            }
            {
                auto result = operation->result;
                if (result.kind != WizardOperationKind::finish) result.connectionEpoch = currentIdentity.epoch;
                return WizardIpc::operationValue(result);
            }
        }
        case WizardCommand::cancel_attempt: {
            if (request.attemptSequence == session->highestSequence && request.clientAttemptId != session->highestAttemptId)
                return Error(ErrorCode::wizard_attempt_closed);
            if (request.attemptSequence > session->highestSequence) {
                session->highestSequence = request.attemptSequence; session->highestAttemptId = request.clientAttemptId;
            }
            const char* state = "closed";
            if (session->operation && session->operation->result.attemptSequence == request.attemptSequence) {
                if (session->operation->result.clientAttemptId != request.clientAttemptId) return Error(ErrorCode::wizard_attempt_closed);
                clearOperationEvidenceLocked(*session, session->operation->result.kind);
                cancelLocked(session->operation);
                state = terminal(*session->operation) ? "cancelled" : "cancelling";
            }
            return J::makeObject({{"attemptSequence", J::makeUnsignedInteger(request.attemptSequence)}, {"attemptState", str(state)}});
        }
        default: return Error(ErrorCode::ipc_unknown_command);
    }
}
Result<J> WizardService::startProbe(const WizardRequest& request) {
    std::string id;
    PHPVPN_TRY_ASSIGN(id, crypto_.randomUuidV4());
    std::lock_guard lock(mutex_);
    std::shared_ptr<Session> session;
    PHPVPN_TRY_ASSIGN(session, sessionLocked(request));
    if (request.attemptSequence <= session->highestSequence) return Error(ErrorCode::wizard_attempt_closed);
    session->highestSequence = request.attemptSequence; session->highestAttemptId = request.clientAttemptId;
    if (session->operation && !terminal(*session->operation)) return Error(ErrorCode::busy);
    PHPVPN_TRY(readyLocked(*session));
    auto fields = session->view.fields.value();
    fields.serverUrl = request.fields.serverUrl;
    if (request.command == WizardCommand::check_secret) fields.serverSecret = request.fields.serverSecret;
    if (request.command == WizardCommand::check_access) fields = request.fields;
    const auto kind = request.command == WizardCommand::check_url ? WizardOperationKind::url :
        request.command == WizardCommand::check_secret ? WizardOperationKind::secret : WizardOperationKind::access;
    if (kind != WizardOperationKind::url && (!session->urlEvidence || *session->urlEvidence != fields.serverUrl))
        return Error(ErrorCode::wizard_prerequisite_failed);
    if (kind == WizardOperationKind::access) {
        if (!session->secretEvidence || session->secretEvidence->serverUrl != fields.serverUrl || session->secretEvidence->serverSecret != fields.serverSecret)
            return Error(ErrorCode::wizard_prerequisite_failed);
        PHPVPN_TRY(validateWizardIdentity(*session->original, fields));
        if ((fields.useDefaultServerSecretInWizard && fields.serverSecret != kDefaultServerSecret) ||
            (fields.useDefaultAccessKeyInWizard && fields.accessKey != defaultAccessKey(fields.virtualIp)) ||
            (fields.autoDetectIpAddressInWizard && (!session->suggestedIpEvidence || *session->suggestedIpEvidence != fields.virtualIp)))
            return Error(ErrorCode::wizard_prerequisite_failed);
    }
    if (kind == WizardOperationKind::url) clearEvidenceLocked(*session);
    else if (kind == WizardOperationKind::secret) { session->secretEvidence.reset(); session->suggestedIpEvidence.reset(); session->accessEvidence.reset(); }
    else session->accessEvidence.reset();
    auto operation = std::make_shared<Operation>();
    operation->result.operationId = id; operation->result.kind = kind;
    operation->result.connectionEpoch = session->view.connectionEpoch;
    operation->result.clientAttemptId = request.clientAttemptId; operation->result.attemptSequence = request.attemptSequence;
    operation->generation = session->generation; operation->fields = fields;
    // A successfully admitted task cannot enter its body until this lock is released.
    if (!scheduler_.diagnostic || !scheduler_.diagnostic([this, session, operation] { probeTask(session, operation); }))
        return Error(ErrorCode::busy);
    session->operation = operation;
    session->view.fields = std::move(fields);
    return acknowledgement(*operation);
}
void WizardService::probeTask(std::shared_ptr<Session> session, std::shared_ptr<Operation> operation) {
    Result<std::size_t> outcome = Error(ErrorCode::cancelled);
    std::optional<std::string> suggestion;
    std::optional<int> httpStatus;
    std::shared_ptr<ProbeClient> probe;
    std::unique_ptr<Transport> transport;
    try {
        {
            std::lock_guard lock(mutex_);
            operation->executing = true;
            if (!operation->cancelled && session->alive && !stopping_)
                operation->result.state = WizardOperationState::running;
        }
        auto created = scheduler_.transport();
        if (!created) outcome = created.error();
        else {
            transport = std::move(created).value();
            probe = std::make_shared<ProbeClient>(crypto_, *transport, clock_);
            {
                std::lock_guard lock(mutex_);
                operation->probe = probe;
                if (operation->cancelled || !session->alive || stopping_) probe->cancel();
            }
            if (operation->result.kind == WizardOperationKind::url) {
                auto checked = probe->checkUrl(session->original->context, operation->fields);
                outcome = checked ? Result<std::size_t>(0) : Result<std::size_t>(checked.error());
            } else if (operation->result.kind == WizardOperationKind::secret) {
                auto checked = probe->checkSecret(session->original->context, operation->fields);
                if (checked) {
                    const auto ip = suggestWizardIp(*session->original, checked->clients);
                    if (ip) suggestion = ip->toString();
                    outcome = std::size_t{0};
                } else outcome = checked.error();
            } else outcome = probe->checkAccess(*session->original, operation->fields);
            httpStatus = probe->lastHttpStatus();
        }
    } catch (...) { outcome = Error(ErrorCode::internal); }
    {
        std::lock_guard lock(mutex_);
        operation->probe.reset();
        operation->executing = false;
        const auto identity = engine_.connectionIdentity();
        if (session->interrupted || identity.epoch != operation->result.connectionEpoch) {
            operation->result.state = WizardOperationState::interrupted;
            operation->result.error = WizardIpc::safeError(Error(ErrorCode::wizard_connection_interrupted));
        } else if (operation->cancelled || !session->alive || stopping_ || session->generation != operation->generation) {
            operation->result.state = WizardOperationState::cancelled;
        } else if (!outcome) {
            operation->result.state = WizardOperationState::failed;
            const auto field = operation->result.kind == WizardOperationKind::url ? "serverUrl" :
                operation->result.kind == WizardOperationKind::secret ? "serverSecret" : "accessKey";
            operation->result.error = WizardIpc::safeError(outcome.error(), httpStatus, field, operation->fields.serverUrl);
        } else {
            operation->result.state = WizardOperationState::succeeded;
            operation->result.suggestedIp = suggestion; operation->result.discardedRecordCount = outcome.value();
            if (operation->result.kind == WizardOperationKind::url) session->urlEvidence = operation->fields.serverUrl;
            else if (operation->result.kind == WizardOperationKind::secret) {
                session->secretEvidence = operation->fields;
                session->suggestedIpEvidence = suggestion;
            }
            else session->accessEvidence = operation->fields;
        }
    }
    changed_.notify_all();
}
Result<J> WizardService::finish(const WizardRequest& request) {
    const auto input = Json::serialize(J::makeObject({{"fields", WizardIpc::fieldsValue(request.fields)},
        {"createDefaultContracts", J::makeBool(request.createDefaultContracts)}}));
    Sha256Digest digest;
    PHPVPN_TRY_ASSIGN(digest, crypto_.sha256(asBytes(input)));
    const auto fingerprint = toLowercaseHex(digest);
    std::string id;
    PHPVPN_TRY_ASSIGN(id, crypto_.randomUuidV4());
    std::lock_guard lock(mutex_);
    std::shared_ptr<Session> session;
    PHPVPN_TRY_ASSIGN(session, sessionLocked(request));
    const auto key = commitKey(request.sessionId, request.commitId);
    if (const auto found = commits_.find(key); found != commits_.end()) {
        if (found->second->fingerprint != fingerprint || found->second->generation != session->generation)
            return Error(ErrorCode::invalid_argument, "this commit identifier belongs to another complete input");
        return acknowledgement(*found->second->operation);
    }
    if (commits_.size() >= kWizardMaxReceipts || (session->operation && !terminal(*session->operation))) return Error(ErrorCode::busy);
    PHPVPN_TRY(readyLocked(*session));
    if (!session->urlEvidence || *session->urlEvidence != request.fields.serverUrl || !session->secretEvidence ||
        session->secretEvidence->serverUrl != request.fields.serverUrl || session->secretEvidence->serverSecret != request.fields.serverSecret ||
        !session->accessEvidence || *session->accessEvidence != request.fields) return Error(ErrorCode::wizard_prerequisite_failed);
    if (request.createDefaultContracts && !supportsDefaultContracts(session->original->context.vpnCidr)) return Error(ErrorCode::invalid_argument);
    auto operation = std::make_shared<Operation>();
    operation->result.operationId = id; operation->result.kind = WizardOperationKind::finish;
    operation->result.connectionEpoch = session->view.connectionEpoch; operation->generation = session->generation;
    auto record = std::make_shared<CommitRecord>();
    record->sessionId = request.sessionId; record->commitId = request.commitId; record->fingerprint = fingerprint;
    record->generation = session->generation; record->operation = operation;
    // Allocate both immutable receipt representations and the fallback error
    // before publication. Completing a successful commit only moves strings.
    record->preparedReceipt = {request.commitId, std::string(36, '0')};
    record->preparedOperationReceipt = record->preparedReceipt;
    record->preparedUnknownError = WizardIpc::safeError(Error(ErrorCode::wizard_commit_unknown), {}, "configuration", {}, configPath_);
    // Reserve the durable-in-memory receipt entry before admitting a file task.
    commits_.emplace(key, record);
    if (!scheduler_.file || !scheduler_.file([this, session, record, fields = request.fields, contracts = request.createDefaultContracts] {
        finishTask(session, record, fields, contracts);
    })) {
        commits_.erase(key);
        return Error(ErrorCode::busy);
    }
    session->operation = operation;
    return acknowledgement(*operation);
}
void WizardService::finishTask(std::shared_ptr<Session> session, std::shared_ptr<CommitRecord> record,
    WizardFields fields, bool contracts) {
    auto operation = record->operation;
    Result<ConfigCommit> outcome = Error(ErrorCode::cancelled);
    bool publicationEntered = false;
    bool uncertain = false;
    try {
        // Same guard as Connect: Connect before this section invalidates it,
        // Connect after this section performs its fresh read after publication.
        std::lock_guard transition(engine_.transitionMutex());
        bool allowed = false;
        {
            std::lock_guard lock(mutex_);
            operation->executing = true;
            expireLocked(clock_.leaseMonotonicMs());
            auto ready = readyLocked(*session);
            if (session->alive && !operation->cancelled && !stopping_ && ready && session->generation == operation->generation) {
                operation->committing = true;
                operation->result.state = WizardOperationState::running;
                allowed = true;
            } else if (!ready) outcome = ready.error();
        }
        if (allowed) {
            auto document = buildWizardDocument(crypto_, *session->original, session->view.mode, fields, contracts);
            if (!document) outcome = document.error();
            else {
                publicationEntered = true;
                outcome = store_.save(*document, record->commitId);
            }
        }
    } catch (...) {
        // An exception after entering publication cannot prove that no write
        // occurred. Preserve unknown instead of inviting an unsafe retry.
        uncertain = publicationEntered;
        outcome = Error(ErrorCode::internal);
    }
    {
        std::lock_guard lock(mutex_);
        operation->committing = false; operation->executing = false;
        try {
            if (!uncertain && outcome && outcome->state == ConfigPublishState::confirmed && outcome->revisionToken.size() == 36) {
                std::copy(outcome->revisionToken.begin(), outcome->revisionToken.end(), record->preparedReceipt.revisionToken.begin());
                std::copy(outcome->revisionToken.begin(), outcome->revisionToken.end(), record->preparedOperationReceipt.revisionToken.begin());
                record->receipt.emplace(std::move(record->preparedReceipt));
                operation->result.receipt.emplace(std::move(record->preparedOperationReceipt));
                record->state = WizardCommitState::confirmed;
                operation->result.state = WizardOperationState::succeeded;
            } else if (uncertain || outcome) {
                record->state = WizardCommitState::unknown;
                operation->result.state = WizardOperationState::failed;
                operation->result.error.emplace(std::move(record->preparedUnknownError));
            } else {
                auto failure = WizardIpc::safeError(outcome.error(), {}, "configuration", {}, configPath_);
                auto operationFailure = failure;
                record->error.emplace(std::move(failure));
                operation->result.error.emplace(std::move(operationFailure));
                record->state = WizardCommitState::failed;
                operation->result.state = WizardOperationState::failed;
            }
        } catch (...) {
            record->state = WizardCommitState::unknown;
            record->receipt.reset(); record->error.reset();
            operation->result.receipt.reset();
            operation->result.state = WizardOperationState::failed;
            operation->result.error.emplace(std::move(record->preparedUnknownError));
        }
        if (!session->alive) record->closedAt = clock_.leaseMonotonicMs();
    }
    changed_.notify_all();
}
} // namespace phpvpn
