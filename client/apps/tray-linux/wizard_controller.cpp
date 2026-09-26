#include "wizard_controller.h"

#include <atomic>

#include "core/base/secure_buffer.h"
#include "core/config/config.h"
#include "core/crypto/crypto_provider.h"
#include "core/ipc/wizard_ipc.h"
#include "status_model.h"
#include "tray_text.h"
#include "wizard_text.h"

namespace phpvpn::tray {
namespace {
constexpr std::int64_t kOperationPollMs = 500;
constexpr std::int64_t kCommitBudgetMs = 15000;
constexpr std::string_view kTitle = "Configuration Wizard";

std::uint64_t nextWindowGeneration() {
    static std::atomic<std::uint64_t> value{0};
    return ++value;
}
bool terminal(WizardOperationState state) {
    return state != WizardOperationState::pending && state != WizardOperationState::running &&
           state != WizardOperationState::cancelling;
}
std::string text(const wchar_t* value) { return utf8(value); }
std::string stringMember(const JsonValue& value, const char* name) {
    const auto* member = value.find(name);
    return member && member->isString() ? member->stringValue() : std::string{};
}
std::uint64_t mix(std::uint64_t hash, std::uint64_t value) {
    // FNV-1a over the eight bytes of each identity.
    for (int i = 0; i < 8; ++i) {
        hash ^= (value >> (8 * i)) & 0xFF;
        hash *= 0x100000001b3ULL;
    }
    return hash;
}
}  // namespace

LifecycleSnapshot wizardExitSnapshot(const WizardLifecycleSnapshot& s) {
    std::uint64_t hash = 0xcbf29ce484222325ULL;
    for (std::uint64_t value : {std::uint64_t(s.open), std::uint64_t(s.dirty), std::uint64_t(s.committing),
                                std::uint64_t(s.commitUnknown), s.windowGeneration, s.sessionGeneration,
                                s.draftGeneration, s.attemptGeneration, s.operationGeneration, s.attemptSequence,
                                s.connectionEpoch, std::uint64_t(s.commitDeadlineMs.has_value()),
                                static_cast<std::uint64_t>(s.commitDeadlineMs.value_or(0))})
        hash = mix(hash, value);
    return {s.open, s.dirty, s.commitUnknown, s.committing, hash};
}

WizardController::WizardController(IpcEndpoint endpoint, Dispatcher dispatcher, WizardHost& host, Clock clock,
                                   IdSource ids)
    : endpoint_(endpoint), host_(host), clock_(std::move(clock)), ids_(std::move(ids)),
      primary_(endpoint, dispatcher), control_(endpoint, dispatcher), heartbeat_(endpoint, dispatcher),
      disconnect_(endpoint, dispatcher), status_(endpoint, dispatcher) {}

WizardController::~WizardController() { abandonChannels(); }

std::string WizardController::newId() const {
    if (ids_) return ids_();
    auto crypto = createPlatformCryptoProvider();
    if (!crypto) return {};
    auto id = crypto.value()->randomUuidV4();
    return id ? std::move(id).value() : std::string{};
}

void WizardController::closeOnExit() {
    // Best effort per-session close. Process exit cannot claim to cancel a
    // publication whose receipt is still unknown. The service lease is final.
    abandonChannels();
    if (open_ && !model_.view().sessionId.empty() && !endingSession_) {
        try {
            auto close = request(WizardCommand::close);
            close.requestId = newId();
            auto encoded = WizardIpc::buildRequest(close);
            if (encoded && !close.requestId.empty()) {
                auto reply = exchangeOnce(endpoint_, encoded.value(), kWizardCloseTimeoutMs);
                secureWipe(encoded.value().data(), encoded.value().size());
                secureWipe(reply.response.data(), reply.response.size());
            }
            for (auto* value : {&close.fields.serverUrl, &close.fields.serverSecret, &close.fields.virtualIp,
                                &close.fields.accessKey})
                secureWipe(value->data(), value->size());
        } catch (...) {}
    }
    if (open_) destroy();
}

WizardLifecycleSnapshot WizardController::wizardSnapshot() const {
    return {open_, open_ && draftDirty_, open_ && model_.committing(),
            open_ && model_.step() == WizardStep::commitUnknown,
            windowGeneration_, generation_, draftGeneration_, attemptGeneration_, operationGeneration_,
            model_.attempt() ? model_.attempt()->sequence : 0, model_.view().connectionEpoch,
            commitDeadline_ ? std::optional<std::int64_t>{commitDeadline_} : std::nullopt};
}

void WizardController::abandonChannels() {
    primary_.abandon(); control_.abandon(); heartbeat_.abandon(); disconnect_.abandon(); status_.abandon();
}

bool WizardController::approvedDiscard(const WizardLifecycleSnapshot& approved) {
    if (!open_ || endingSession_ || model_.committing() || confirmationShown_ || wizardSnapshot() != approved)
        return false;
    abandonChannels();
    destroy();
    return true;
}

bool WizardController::approvedDiscard(const LifecycleSnapshot& approved) {
    const auto current = wizardSnapshot();
    if (wizardExitSnapshot(current) != approved) return false;
    return approvedDiscard(current);
}

void WizardController::endSession() {
    if (endingSession_) return;
    endingSession_ = true;
    abandonChannels();
    destroy();
}

void WizardController::windowLost() {
    if (!open_) return;
    abandonChannels();
    destroy();
}

void WizardController::draftChanged() { draftDirty_ = true; ++draftGeneration_; }

void WizardController::render() {
    if (!open_) return;
    syncHttpDiagnostic();
    host_.render();
}

void WizardController::syncHttpDiagnostic() {
    if (model_.httpDiagnostic()) {
        if (!diagnosticRequest_) {
            diagnosticRequest_ = request(WizardCommand::cancel_attempt);
            diagnosticRequest_->fields = {};
        }
    } else {
        host_.closeDetail();
        if (diagnosticRequest_) {
            // Retires the service-held diagnostic of the closed attempt.
            if (!control_.busy() && !endingSession_ && sendRequest(*diagnosticRequest_, control_))
                diagnosticCleanup_ = std::pair{diagnosticRequest_->clientAttemptId, diagnosticRequest_->attemptSequence};
            diagnosticRequest_.reset();
        }
    }
}

bool WizardController::open(std::function<void()> onConfirmed) {
    if (endingSession_) return false;
    if (open_) { host_.presentWindow(); return true; }
    onConfirmed_ = std::move(onConfirmed);
    ++generation_;
    windowGeneration_ = nextWindowGeneration(); ++draftGeneration_; ++attemptGeneration_; ++operationGeneration_;
    draftDirty_ = false; commitDeadline_ = 0;
    model_ = WizardModel{};
    closing_ = resetting_ = disconnecting_ = confirmationShown_ = offlineKnown_ = false;
    commitId_.clear(); commitOperation_.clear();
    if (!host_.createWindow()) { onConfirmed_ = {}; return false; }
    open_ = true;
    lastContact_ = lastHeartbeat_ = clock_(); lastPoll_ = 0;
    render();
    host_.presentWindow();
    if (!send(WizardCommand::open, primary_)) fail(WizardIpc::safeError(Error(ErrorCode::ipc_unavailable)));
    return true;
}

WizardRequest WizardController::request(WizardCommand command) const {
    WizardRequest value;
    value.command = command;
    value.serviceInstanceId = model_.view().serviceInstanceId;
    value.sessionId = model_.view().sessionId;
    value.connectionEpoch = model_.view().connectionEpoch;
    value.fields = model_.fields();
    value.commitId = commitId_;
    value.createDefaultContracts = model_.shouldAddDefaultContracts();
    if (model_.attempt()) {
        value.clientAttemptId = model_.attempt()->clientAttemptId;
        value.attemptSequence = model_.attempt()->sequence;
        value.operationId = model_.attempt()->operationId;
    }
    if (model_.committing()) value.operationId = commitOperation_;
    return value;
}

bool WizardController::sendRequest(WizardRequest request, IpcChannel& channel) {
    if (!open_ || endingSession_) return false;
    const auto generation = generation_;
    return channel.requestWizard(std::move(request), [this, generation](WizardResult& result) {
        if (generation != generation_) return;
        receive(result);
    });
}

bool WizardController::send(WizardCommand command, IpcChannel& channel) {
    return sendRequest(request(command), channel);
}

void WizardController::fail(WizardError value) {
    if (model_.committing() || model_.step() == WizardStep::commitUnknown) {
        model_.commitUnknown(); model_.fail(std::move(value)); render(); return;
    }
    if (value.code == "wizard_session_expired") model_.sessionExpired();
    else if (value.code == "wizard_connection_interrupted") { model_.interrupt(); offlineKnown_ = false; }
    else model_.fail(std::move(value));
    render();
}

bool WizardController::locked() const {
    return model_.busy() || primary_.busy() || control_.busy() || closing_ || resetting_ || disconnecting_;
}

void WizardController::next() {
    if (!open_ || endingSession_) return;
    if (primary_.busy() || control_.busy() || closing_ || resetting_ || disconnecting_ || model_.busy() ||
        confirmationShown_)
        return;
    if (model_.step() == WizardStep::disconnectGate) {
        if (offlineKnown_) {
            if (!send(WizardCommand::resume, primary_)) fail(WizardIpc::safeError(Error(ErrorCode::ipc_unavailable)));
        } else {
            // "Disconnect & continue" is the single consent to one global Disconnect.
            const auto generation = generation_;
            if (!disconnect_.request(IpcCommand::disconnect, {}, [this, generation](std::string& response) {
                    if (generation == generation_) receiveDisconnect(response);
                }))
                fail(WizardIpc::safeError(Error(ErrorCode::ipc_unavailable)));
            else disconnecting_ = true;
        }
        render(); return;
    }
    if (model_.step() == WizardStep::sessionExpired || model_.step() == WizardStep::loading) {
        if (!send(WizardCommand::open, primary_)) fail(WizardIpc::safeError(Error(ErrorCode::ipc_unavailable)));
        render(); return;
    }
    if (model_.step() == WizardStep::commitUnknown) {
        if (!send(WizardCommand::get_commit, primary_)) fail(WizardIpc::safeError(Error(ErrorCode::ipc_unavailable)));
        render(); return;
    }
    if (model_.step() == WizardStep::contracts) { finish(); return; }
    auto attempt = model_.beginCheck(newId(), clock_());
    if (!attempt) {
        fail(WizardIpc::safeError(attempt.error()));
        if (!open_) return;
        const auto step = model_.step();
        const auto field = step == WizardStep::serverUrl ? WizardField::url
                         : step == WizardStep::serverSecret ? WizardField::secret
                         : attempt.error().code == ErrorCode::config_invalid_address ? WizardField::ip : WizardField::key;
        const auto fallback = field == WizardField::ip ? WizardField::automaticIp
                            : field == WizardField::key ? WizardField::defaultKey
                            : field == WizardField::secret ? WizardField::defaultSecret : WizardField::none;
        host_.focusField(field, fallback);
        return;
    }
    const auto command = attempt->kind == WizardOperationKind::url ? WizardCommand::check_url
                       : attempt->kind == WizardOperationKind::secret ? WizardCommand::check_secret
                                                                      : WizardCommand::check_access;
    ++attemptGeneration_; ++operationGeneration_;
    if (!send(command, primary_)) {
        model_.fail(WizardIpc::safeError(Error(ErrorCode::ipc_unavailable)));
        send(WizardCommand::cancel_attempt, control_);
    }
    render();
}

void WizardController::back() {
    if (!open_ || endingSession_ || locked()) return;
    const auto step = model_.step();
    if (step != WizardStep::serverSecret && step != WizardStep::identity && step != WizardStep::contracts) return;
    model_.back();
    render();
}

void WizardController::finish() {
    commitId_ = newId();
    if (commitId_.empty()) { fail(WizardIpc::safeError(Error(ErrorCode::internal))); return; }
    commitOperation_.clear(); model_.setCommitting(true); commitDeadline_ = clock_() + kCommitBudgetMs;
    ++operationGeneration_;
    if (!send(WizardCommand::finish, primary_)) model_.commitUnknown();
    render();
}

void WizardController::cancel(bool reset) {
    if (!open_ || endingSession_) return;
    if (model_.committing()) {
        host_.notify("The service is saving the configuration. Wait for its receipt before closing or resetting.",
                     kTitle, WizardNotice::information);
        return;
    }
    if (control_.busy() || closing_ || resetting_) return;
    const auto approved = wizardSnapshot();
    if (reset && !host_.confirm("Reset this entire wizard to the default settings? Your file will change only after Finish.",
                                kTitle))
        return;
    if (endingSession_ || !open_ || wizardSnapshot() != approved) return;
    if (model_.step() == WizardStep::commitUnknown && reset) {
        host_.notify("First verify the previous save. Its result is unknown and the file may already have changed.",
                     kTitle, WizardNotice::information);
        return;
    }
    ++operationGeneration_;
    if (model_.view().sessionId.empty()) {
        if (primary_.busy()) { closing_ = !reset; resetting_ = reset; render(); return; }
        destroy(); return;
    }
    closing_ = !reset; resetting_ = reset;
    if (model_.attempt() && !model_.attempt()->closed) {
        model_.fail(WizardIpc::safeError(Error(ErrorCode::cancelled)));
        if (!send(WizardCommand::cancel_attempt, control_)) {
            closing_ = resetting_ = false;
            fail(WizardIpc::safeError(Error(ErrorCode::ipc_unavailable)));
        }
    } else if (!send(reset ? WizardCommand::reset : WizardCommand::close, control_)) {
        closing_ = resetting_ = false;
        fail(WizardIpc::safeError(Error(ErrorCode::ipc_unavailable)));
    }
    render();
}

void WizardController::confirm(const WizardReceipt& receipt) {
    if (receipt.commitId != commitId_ || confirmationShown_) return;
    confirmationShown_ = true; model_.confirmed();
    draftDirty_ = false;
    closing_ = true;
    render();
    const auto confirmedGeneration = windowGeneration_;
    host_.notify(text(wizardText::saved), text(wizardText::savedHeading), WizardNotice::information);
    if (endingSession_ || !open_ || windowGeneration_ != confirmedGeneration) return;
    if (!send(WizardCommand::close, control_)) {
        const auto callback = onConfirmed_;
        destroy();
        if (callback && !endingSession_) callback();
        return;
    }
    // Drain the bounded close request before destroying the window. A lost
    // close reply cannot undo an already confirmed publication.
    render();
}

void WizardController::receive(WizardResult& result) {
    if (!open_ || endingSession_) return;
    const auto command = result.request.command;
    if (command == WizardCommand::cancel_attempt && diagnosticCleanup_ &&
        diagnosticCleanup_->first == result.request.clientAttemptId &&
        diagnosticCleanup_->second == result.request.attemptSequence) {
        diagnosticCleanup_.reset();
        if (closing_ || resetting_) send(resetting_ ? WizardCommand::reset : WizardCommand::close, control_);
        render(); return;
    }
    if (command == WizardCommand::close && confirmationShown_) {
        const auto callback = onConfirmed_;
        destroy();
        if (callback && !endingSession_) callback();
        return;
    }
    if (!result.reply || result.reply.value().error) {
        auto failure = result.reply ? *result.reply.value().error : WizardIpc::safeError(result.reply.error());
        if (command == WizardCommand::finish && result.reply && result.reply.value().error &&
            failure.code != "wizard_commit_unknown") {
            model_.commitFailed(failure); fail(std::move(failure)); return;
        }
        if (command == WizardCommand::finish || (command == WizardCommand::get_operation && model_.committing())) {
            model_.commitUnknown(); model_.fail(std::move(failure)); render(); return;
        }
        if (command == WizardCommand::get_commit && failure.code == "wizard_session_expired") {
            // Explicit receipt verification discovered a restarted service.
            // One fresh state read informs the user, never repeats publication.
            send(WizardCommand::get_config_state, primary_); render(); return;
        }
        if (command == WizardCommand::keepalive && failure.code != "wizard_session_expired" &&
            failure.code != "wizard_connection_interrupted")
            return;
        if ((closing_ || resetting_) && failure.code == "busy") { render(); return; }
        if (closing_ && failure.code == "wizard_session_expired") { destroy(); return; }
        closing_ = resetting_ = false;
        if (command == WizardCommand::check_url || command == WizardCommand::check_secret ||
            command == WizardCommand::check_access || command == WizardCommand::get_operation)
            if (!control_.busy() && model_.attempt()) send(WizardCommand::cancel_attempt, control_);
        fail(std::move(failure)); return;
    }
    lastContact_ = clock_();
    if (command == WizardCommand::keepalive) return;
    const auto& value = result.reply.value().result;
    if (command == WizardCommand::open || command == WizardCommand::reset || command == WizardCommand::resume) {
        auto view = WizardIpc::parseView(value);
        if (!view) { fail(WizardIpc::safeError(view.error())); return; }
        model_.load(std::move(view).value(), command == WizardCommand::resume);
        ++draftGeneration_; ++attemptGeneration_; ++operationGeneration_;
        if (command == WizardCommand::open) { draftDirty_ = false; commitDeadline_ = 0; }
        if (command == WizardCommand::reset) { draftDirty_ = true; commitDeadline_ = 0; resetting_ = false; }
        offlineKnown_ = model_.view().vpnState == ConnectionState::disconnected &&
                        model_.view().vpnPhase == ConnectionPhase::idle;
        // A new session identity: deliveries of the old one are revoked without waiting.
        ++generation_; primary_.abandon(); heartbeat_.abandon(); lastHeartbeat_ = clock_();
        if (closing_ || resetting_) send(resetting_ ? WizardCommand::reset : WizardCommand::close, control_);
    } else if (command == WizardCommand::check_url || command == WizardCommand::check_secret ||
               command == WizardCommand::check_access) {
        model_.acceptStart(stringMember(value, "operationId"), result.request.clientAttemptId,
                           result.request.attemptSequence);
    } else if (command == WizardCommand::get_operation) {
        auto operation = WizardIpc::parseOperation(value);
        if (!operation) { fail(WizardIpc::safeError(operation.error())); return; }
        if (operation->kind == WizardOperationKind::finish) {
            if (operation->operationId != commitOperation_) { fail(WizardIpc::safeError(Error(ErrorCode::ipc_malformed))); return; }
            if (operation->state == WizardOperationState::succeeded && operation->receipt) { confirm(*operation->receipt); return; }
            if (terminal(operation->state)) {
                model_.setCommitting(false);
                if (operation->error && operation->error->code == "wizard_commit_unknown") model_.commitUnknown();
                fail(operation->error.value_or(WizardIpc::safeError(Error(ErrorCode::config_write_failed))));
                return;
            }
        } else {
            const auto previousStep = model_.step();
            const bool changesIp = model_.autoDetectIpAddress() && operation->suggestedIp &&
                                   model_.fields().virtualIp != *operation->suggestedIp;
            const bool applied = model_.applyOperation(*operation, clock_());
            if (applied && changesIp && previousStep == WizardStep::serverSecret && model_.step() == WizardStep::identity)
                draftChanged();
        }
    } else if (command == WizardCommand::finish) {
        commitOperation_ = stringMember(value, "operationId");
    } else if (command == WizardCommand::close) {
        destroy(); return;
    } else if (command == WizardCommand::cancel_attempt) {
        // Reset/close are short idempotent control actions. A cancelling probe
        // is drained by the service before Reset can be accepted.
        if (closing_ || resetting_) send(resetting_ ? WizardCommand::reset : WizardCommand::close, control_);
    } else if (command == WizardCommand::get_commit) {
        const auto state = stringMember(value, "commitState");
        if (state == "confirmed") {
            const auto* receipt = value.find("receipt");
            confirm({stringMember(*receipt, "commitId"), stringMember(*receipt, "revisionToken")});
            return;
        }
        if (state == "failed") {
            auto failure = WizardIpc::parseError(*value.find("error"));
            // An authoritative no-publication result. Start again only through the
            // user's explicit reopening, never replay Finish.
            if (failure) model_.commitFailed(*failure);
        } else {
            model_.commitUnknown();
        }
    } else if (command == WizardCommand::get_config_state) {
        model_.commitUnknown();
        host_.notify("The service restarted. The previous save cannot be confirmed here. Current configuration: " +
                         stringMember(value, "availability") + "\n" + stringMember(value, "configPath") +
                         "\nUse Edit Config to inspect the file before starting a new wizard. No save was repeated.",
                     "Save result unknown", WizardNotice::warning);
        if (!open_ || endingSession_) return;
    }
    render();
}

void WizardController::receiveDisconnect(const std::string& response) {
    if (!open_ || endingSession_) return;
    const auto snapshot = parseStatusResponse(response);
    if (!snapshot.valid) {
        disconnecting_ = false; fail(WizardIpc::safeError(Error(ErrorCode::ipc_unavailable))); return;
    }
    if (snapshot.state == "disconnected" && snapshot.phase == "idle") {
        disconnecting_ = false; offlineKnown_ = true; send(WizardCommand::resume, primary_);
    }
    render();
}

void WizardController::receiveStatus(const std::string& response) {
    if (!open_ || endingSession_) return;
    const auto snapshot = parseStatusResponse(response);
    if (!snapshot.valid) return;
    const bool offline = snapshot.state == "disconnected" && snapshot.phase == "idle";
    const bool changed = offlineKnown_ != offline;
    offlineKnown_ = offline;
    if (offline && disconnecting_ && !primary_.busy()) {
        disconnecting_ = false; send(WizardCommand::resume, primary_); render();
    } else if (changed) {
        render();
    }
}

void WizardController::tick() {
    if (!open_ || endingSession_) return;
    // The confirmed publication has one notification and one bounded close.
    // No polling or lease expiry may re-enter that sequence from the modal loop.
    if (confirmationShown_) return;
    const auto now = clock_();
    if (model_.committing() && now >= commitDeadline_) { model_.commitUnknown(); render(); }
    const bool sessionLive = !model_.view().sessionId.empty() && model_.step() != WizardStep::sessionExpired &&
                             model_.step() != WizardStep::commitUnknown;
    if (sessionLive && now - lastContact_ >= kWizardLeaseMs) {
        if (model_.committing()) model_.commitUnknown(); else model_.sessionExpired();
        render();
    }
    if (!model_.view().sessionId.empty() && model_.step() != WizardStep::sessionExpired &&
        model_.step() != WizardStep::commitUnknown && !heartbeat_.busy() && now - lastHeartbeat_ >= kWizardKeepaliveMs) {
        lastHeartbeat_ = now;
        send(WizardCommand::keepalive, heartbeat_);
    }
    if (model_.expireAttempt(now)) {
        if (!control_.busy()) send(WizardCommand::cancel_attempt, control_);
        render();
    }
    if (now - lastPoll_ < kOperationPollMs) return;
    lastPoll_ = now;
    if (closing_ || resetting_) {
        if (!control_.busy() && !model_.view().sessionId.empty())
            send(resetting_ ? WizardCommand::reset : WizardCommand::close, control_);
    } else if (disconnecting_ || model_.step() == WizardStep::disconnectGate) {
        // The gate's read-only 400 ms status read, at most one outstanding.
        const auto generation = generation_;
        if (!status_.busy())
            status_.request(IpcCommand::get_status, {}, [this, generation](std::string& response) {
                if (generation == generation_) receiveStatus(response);
            }, kStatusRequestTimeoutMs);
    } else if (!primary_.busy() && ((model_.committing() && !commitOperation_.empty()) ||
                                    (model_.busy() && model_.attempt() && !model_.attempt()->operationId.empty()))) {
        send(WizardCommand::get_operation, primary_);
    }
}

void WizardController::destroy() {
    diagnosticRequest_.reset(); diagnosticCleanup_.reset();
    ++generation_;
    windowGeneration_ = nextWindowGeneration(); ++draftGeneration_; ++attemptGeneration_; ++operationGeneration_;
    abandonChannels();
    const bool wasOpen = open_;
    open_ = false;
    if (wasOpen) host_.destroyWindow();
    model_ = WizardModel{}; commitId_.clear(); commitOperation_.clear();
    draftDirty_ = false; commitDeadline_ = 0; onConfirmed_ = {};
}

// --- edits ---------------------------------------------------------------------
void WizardController::editUrl(std::string value) {
    if (!acceptsInput() || model_.busy() || model_.committing() || model_.step() == WizardStep::commitUnknown) return;
    draftChanged();
    model_.setUrl(std::move(value));
    syncHttpDiagnostic(); host_.refreshDiagnostic();
}

void WizardController::editSecret(std::string value) {
    if (!acceptsInput() || model_.busy() || model_.committing() || model_.step() == WizardStep::commitUnknown) return;
    draftChanged();
    const bool wasDefault = model_.fields().serverSecret == kDefaultServerSecret;
    model_.setSecretEditor(std::move(value));
    const bool isDefault = model_.fields().serverSecret == kDefaultServerSecret;
    if (wasDefault != isDefault) render();
    syncHttpDiagnostic(); host_.refreshDiagnostic();
}

void WizardController::editIp(std::string value) {
    if (!acceptsInput() || model_.busy() || model_.committing() || model_.step() == WizardStep::commitUnknown) return;
    draftChanged();
    model_.setIp(std::move(value));
    if (model_.useDefaultAccessKey()) host_.setFieldText(WizardField::key, model_.fields().accessKey);
    syncHttpDiagnostic(); host_.refreshDiagnostic();
}

void WizardController::editKey(std::string value) {
    if (!acceptsInput() || model_.busy() || model_.committing() || model_.step() == WizardStep::commitUnknown) return;
    draftChanged();
    model_.setAccessKey(std::move(value));
    syncHttpDiagnostic(); host_.refreshDiagnostic();
}

void WizardController::setDefaultSecret(bool checked) {
    if (!acceptsInput() || locked()) return;
    draftChanged(); model_.setDefaultSecret(checked); render();
}
void WizardController::setAutoDetectIp(bool checked) {
    if (!acceptsInput() || locked()) return;
    draftChanged(); model_.setAutoDetectIpAddress(checked); render();
}
void WizardController::setDefaultKey(bool checked) {
    if (!acceptsInput() || locked()) return;
    draftChanged(); model_.setDefaultAccessKey(checked); render();
}
void WizardController::setCreateContracts(bool checked) {
    if (!acceptsInput() || locked()) return;
    draftChanged(); model_.setCreateContracts(checked); render();
}

Status WizardController::acceptHttpHeaders(std::string value) {
    if (!acceptsInput()) return Error(ErrorCode::busy);
    const auto before = model_.fields().httpHeaders;
    auto result = model_.setHttpHeaders(std::move(value));
    if (result && before != model_.fields().httpHeaders) { draftChanged(); render(); }
    return result;
}

// --- presentation ----------------------------------------------------------------
WizardPresentation WizardController::presentation() const {
    WizardPresentation p;
    const auto step = model_.step();
    p.step = step;
    p.locked = locked();
    const auto& checks = model_.checks();
    // Sidebar (Windows drawSteps, non-compact texts; the window adapts the compact form).
    const int active = step == WizardStep::serverUrl ? 0 : step == WizardStep::serverSecret ? 1
                     : step == WizardStep::identity ? 2 : step == WizardStep::contracts ? 3 : -1;
    const char* names[]{"Server URL", "Server secret", "IP and access key", "Contracts"};
    for (int i = 0; i < 4; ++i) {
        const auto state = i < 3 ? checks[static_cast<std::size_t>(i)] : phpvpn::CheckpointResult::pending;
        auto& item = p.sidebar[static_cast<std::size_t>(i)];
        item.name = names[i];
        item.active = i == active;
        item.color = state == phpvpn::CheckpointResult::success ? ui::Color::success
                   : state == phpvpn::CheckpointResult::failed ? ui::Color::error : ui::Color::secondaryText;
        if (item.active && state != phpvpn::CheckpointResult::failed) item.color = ui::Color::primary;
        item.symbol = state == phpvpn::CheckpointResult::success ? "✓" : state == phpvpn::CheckpointResult::failed ? "×"
                                                                                                        : std::to_string(i + 1);
        item.status = state == phpvpn::CheckpointResult::success ? "Completed" : state == phpvpn::CheckpointResult::failed ? "Failed"
                    : item.active ? "" : "Upcoming";
    }
    p.sidebarText = "CONFIGURE THIS COMPUTER\n\n";
    if (step == WizardStep::disconnectGate) p.sidebarText += "Disconnect VPN\n\n";
    const char* numbered[]{"1  Server URL", "2  Server secret", "3  IP and access key"};
    for (std::size_t i = 0; i < 3; ++i) {
        p.sidebarText += numbered[i];
        p.sidebarText += checks[i] == phpvpn::CheckpointResult::success ? "\n    Completed\n\n"
                       : checks[i] == phpvpn::CheckpointResult::failed ? "\n    Failed\n\n" : "\n    Upcoming\n\n";
    }
    p.sidebarText += "4  Communication contracts";

    // Message pages.
    switch (step) {
        case WizardStep::loading:
            p.heading = text(wizardText::configure); p.text = text(wizardText::loading); break;
        case WizardStep::disconnectGate:
            p.eyebrow = text(wizardText::beforeSetup);
            p.heading = text(wizardText::gateHeading);
            p.text = text(wizardText::gateDescription);
            p.gateCard = true;
            p.offline = offlineKnown_;
            break;
        case WizardStep::loadError:
            p.heading = "This configuration needs attention";
            p.text = "The service classified the file as: " +
                     std::string(configAvailabilityName(model_.view().availability)) +
                     ".\nExpected file: " + expectedConfigurationPath();
            p.secondText = "Choose Reset to start with the default settings. Nothing will be written until Finish. "
                           "You can also close this wizard and copy a configuration supplied by your administrator to "
                           "the path above, or repair valid UTF-8 text using Edit Config.";
            break;
        case WizardStep::sessionExpired:
            p.heading = "Open a new wizard session"; p.text = text(wizardText::expired); break;
        case WizardStep::commitUnknown:
            p.heading = "The save result is unknown"; p.text = text(wizardText::unknown);
            p.secondText = "Choose Verify save to read the receipt. If the service restarted, inspect the actual file "
                           "using Edit Config before starting again.";
            break;
        case WizardStep::finished:
            p.heading = text(wizardText::savedHeading); p.text = text(wizardText::saved); break;
        case WizardStep::serverUrl: case WizardStep::serverSecret: case WizardStep::identity:
        case WizardStep::contracts: break;
    }

    // Error panel (Windows renderErrorPanel).
    if (model_.error()) {
        const auto& failure = *model_.error();
        std::string panel = failure.description + "\n" + failure.suggestedAction;
        if (failure.code == "timeout" || failure.code == "transport_timeout") panel = text(wizardText::timeout);
        if (failure.code == "wizard_connection_interrupted") panel = text(wizardText::interrupted);
        if (step == WizardStep::serverUrl && model_.fields().serverUrl.find("MODIFY-THIS-URL") != std::string::npos)
            panel = text(wizardText::urlPlaceholder);
        if (step == WizardStep::serverSecret && failure.code == "crypto_tag_mismatch" && failure.field == "serverSecret")
            panel = text(wizardText::secretMismatchPrefix) + model_.secretEditorText() + text(wizardText::secretMismatchSuffix);
        if (step == WizardStep::identity && failure.code == "wizard_contract_self_conflict")
            panel = text(wizardText::contractConflict);
        if (failure.httpStatus) panel += "\nHTTP status: " + std::to_string(*failure.httpStatus);
        if (failure.serverUrl) panel += "\nServer URL: " + *failure.serverUrl;
        if (failure.configPath) panel += "\nConfiguration: " + *failure.configPath;
        p.errorText = std::move(panel);
        // Presentation follows only errors already evaluated by the workflow.
        // Transport failures (including timeout) do not declare the input invalid.
        if (failure.code == "config_invalid_url" && step == WizardStep::serverUrl) p.invalidField = WizardField::url;
        else if (failure.code == "config_invalid_value" && step == WizardStep::serverSecret) p.invalidField = WizardField::secret;
        else if ((failure.code == "config_invalid_address" || failure.code == "wizard_contract_self_conflict") &&
                 step == WizardStep::identity) p.invalidField = WizardField::ip;
        else if (failure.code == "config_invalid_key" && step == WizardStep::identity) p.invalidField = WizardField::key;
        else if (failure.code == "crypto_tag_mismatch" && failure.field == "serverSecret") p.invalidField = WizardField::secret;
        else if (failure.code == "server_unauthorized" && failure.field) {
            if (*failure.field == "serverSecret") p.invalidField = WizardField::secret;
            else if (*failure.field == "accessKey") p.invalidField = WizardField::key;
        }
    }

    // Step contents.
    const auto& fields = model_.fields();
    p.url = fields.serverUrl;
    p.secret = model_.secretEditorText();
    p.ip = fields.virtualIp;
    p.key = fields.accessKey;
    p.useDefaultSecret = model_.useDefaultSecret();
    p.autoDetectIp = model_.autoDetectIpAddress();
    p.useDefaultKey = model_.useDefaultAccessKey();
    p.createContracts = model_.createContracts();
    p.secretLabel = text(model_.secretLiteralMode() ? wizardText::secretLiteral : wizardText::secretLabel);
    p.secretHint = text(model_.secretLiteralMode() ? wizardText::secretLiteralHint : wizardText::secretHint);
    p.defaultSecretWarning = step == WizardStep::serverSecret && !model_.error() &&
                             (model_.useDefaultSecret() || fields.serverSecret == kDefaultServerSecret);
    if (model_.view().context)
        p.identityDescription = text(wizardText::identityPrefix) + model_.view().context->vpnCidr.toString() +
                                text(wizardText::identityDescription);
    p.noSuggestion = model_.autoDetectIpAddress() && !model_.hasSuggestedIp();
    p.defaultKeyWarning = step == WizardStep::identity && !model_.error() && model_.useDefaultAccessKey();
    p.contractCounts = {model_.view().contractCount, model_.missingContractCount(),
                        model_.shouldAddDefaultContracts() ? model_.missingContractCount() : 0};
    p.contractsUnsupported = !model_.contractsAllowed();
    p.contractsHint = model_.shouldAddDefaultContracts() && model_.missingContractCount() > 0;
    p.secretEnabled = !p.locked && !model_.useDefaultSecret();
    p.ipEnabled = !p.locked && !model_.autoDetectIpAddress();
    p.keyEnabled = !p.locked && !model_.useDefaultAccessKey();
    p.contractsEnabled = !p.locked && model_.contractsAllowed();

    if (p.locked)
        p.statusText = model_.committing() ? text(wizardText::saving)
                     : closing_ ? "Closing this session…"
                     : resetting_ ? "Stopping the current check and resetting…"
                     : disconnecting_ ? text(wizardText::waitingDisconnect)
                     : step == WizardStep::serverUrl ? text(wizardText::checkingUrl)
                     : step == WizardStep::serverSecret ? text(wizardText::checkingSecret)
                     : step == WizardStep::identity ? text(wizardText::checkingIdentity)
                                                    : "Waiting for the service…";

    // Footer.
    p.detailVisible = model_.httpDiagnostic().has_value();
    p.detailEnabled = p.detailVisible && !p.locked;
    p.backVisible = step == WizardStep::serverSecret || step == WizardStep::identity || step == WizardStep::contracts;
    p.backEnabled = p.backVisible && !p.locked;
    p.primaryLabel = step == WizardStep::contracts ? "Finish"
                   : step == WizardStep::commitUnknown ? "Verify save"
                   : step == WizardStep::sessionExpired ? "Open again"
                   : step == WizardStep::disconnectGate ? (offlineKnown_ ? std::string("Continue") : text(wizardText::disconnect))
                                                        : text(wizardText::verify);
    p.primaryEnabled = !p.locked && step != WizardStep::loadError && (step != WizardStep::loading || model_.error().has_value());
    p.resetEnabled = !model_.committing() && !closing_ && !resetting_ && step != WizardStep::commitUnknown &&
                     step != WizardStep::sessionExpired && !model_.view().sessionId.empty();
    return p;
}
}  // namespace phpvpn::tray
