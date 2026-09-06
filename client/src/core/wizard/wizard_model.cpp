#include "core/wizard/wizard_model.h"
#include "core/wizard/wizard_defaults.h"
#include "core/ipc/wizard_ipc.h"
#include "core/protocol/uuid.h"
#include "core/base/hex.h"
#include <algorithm>
#include <limits>

namespace phpvpn {
void WizardModel::load(WizardView view, bool preserveFields) {
    const bool newSession = view.sessionId != view_.sessionId;
    view_ = std::move(view);
    if (newSession) sequence_ = 0;
    busy_ = committing_ = false; attempt_.reset(); error_.reset();
    invalidate(0);
    if (!view_.fields || !view_.context) { step_ = WizardStep::loadError; return; }
    if (!preserveFields) {
        fields_ = *view_.fields;
        defaultKey_ = fields_.accessKey.empty();
        defaultSecret_ = fields_.serverSecret == kDefaultServerSecret;
        updateDefaultKey(); updateSecretText();
        createContracts_ = contractsAllowed();
    }
    step_ = view_.vpnState == ConnectionState::disconnected && view_.vpnPhase == ConnectionPhase::idle ? WizardStep::serverUrl : WizardStep::disconnectGate;
}
void WizardModel::invalidate(std::size_t from) {
    for (auto i = from; i < checks_.size(); ++i) checks_[i] = CheckpointResult::pending;
}
void WizardModel::updateDefaultKey() { if (defaultKey_) fields_.accessKey = defaultAccessKey(fields_.virtualIp); }
void WizardModel::updateSecretText() {
    literalSecret_ = std::any_of(fields_.serverSecret.begin(), fields_.serverSecret.end(), [](unsigned char c) { return c < 0x20 || c == 0x7f; });
    secretText_ = literalSecret_ ? Json::escapeString(fields_.serverSecret) : fields_.serverSecret;
    secretValid_ = true;
}
bool WizardModel::setUrl(std::string text) {
    if (busy_ || committing_) return false;
    if (fields_.serverUrl != text) { fields_.serverUrl = std::move(text); invalidate(0); }
    return true;
}
bool WizardModel::setSecretEditor(std::string text) {
    if (busy_ || committing_ || defaultSecret_) return false;
    secretText_ = std::move(text);
    if (literalSecret_) {
        auto parsed = Json::parse(secretText_);
        secretValid_ = parsed && parsed->isString();
        if (secretValid_) fields_.serverSecret = parsed->stringValue();
    } else {
        secretValid_ = isStrictUtf8(secretText_);
        if (secretValid_) fields_.serverSecret = secretText_;
    }
    invalidate(1);
    return true;
}
bool WizardModel::setDefaultSecret(bool checked) {
    if (busy_ || committing_) return false;
    defaultSecret_ = checked;
    if (checked) fields_.serverSecret = kDefaultServerSecret;
    updateSecretText(); invalidate(1); return true;
}
bool WizardModel::setIp(std::string text) {
    if (busy_ || committing_) return false;
    if (fields_.virtualIp != text) { fields_.virtualIp = std::move(text); updateDefaultKey(); invalidate(2); }
    return true;
}
bool WizardModel::setAccessKey(std::string text) {
    if (busy_ || committing_ || defaultKey_) return false;
    fields_.accessKey = std::move(text); invalidate(2); return true;
}
bool WizardModel::setDefaultAccessKey(bool checked) {
    if (busy_ || committing_) return false;
    defaultKey_ = checked; updateDefaultKey(); invalidate(2); return true;
}
bool WizardModel::contractsAllowed() const {
    return view_.context && (view_.mode == WizardMode::create || view_.mode == WizardMode::reset) && supportsDefaultContracts(view_.context->vpnCidr);
}
std::size_t WizardModel::contractCount() const {
    if (view_.mode == WizardMode::edit || view_.mode == WizardMode::repair) return view_.contractCount;
    if (!createContracts_ || !contractsAllowed()) return 0;
    const auto own = Ipv4Address::parse(fields_.virtualIp);
    if (!own) return 0;
    const auto last = own->value() & 255;
    return (own->value() & 0xffffff00) == view_.context->vpnCidr.network().value() && last >= 2 && last <= 15 ? 13 : 14;
}
void WizardModel::setCreateContracts(bool checked) { if (!busy_ && !committing_) createContracts_ = checked && contractsAllowed(); }
bool WizardModel::back() {
    if (busy_ || committing_) return false;
    if (step_ == WizardStep::contracts) step_ = WizardStep::identity;
    else if (step_ == WizardStep::identity) step_ = WizardStep::serverSecret;
    else if (step_ == WizardStep::serverSecret) step_ = WizardStep::serverUrl;
    else return false;
    error_.reset(); return true;
}
Status WizardModel::validateCurrent() const {
    if (!view_.context) return Error(ErrorCode::wizard_prerequisite_failed);
    if (step_ == WizardStep::serverUrl) {
        auto url = parseServerUrl(fields_.serverUrl);
        if (!url) return url.error();
        auto host = url->host;
        std::transform(host.begin(), host.end(), host.begin(), [](unsigned char c) { return static_cast<char>(c >= 'A' && c <= 'Z' ? c + 32 : c); });
        if (host == "modify-this-url") return Error(ErrorCode::config_invalid_url);
    } else if (step_ == WizardStep::serverSecret) {
        if (!secretValid_) return Error(ErrorCode::config_invalid_value, "enter a complete JSON string literal");
    } else if (step_ == WizardStep::identity) {
        const auto ip = Ipv4Address::parse(fields_.virtualIp);
        if (!ip || !view_.context->vpnCidr.isValidClientAddress(*ip)) return Error(ErrorCode::config_invalid_address);
        if (fields_.accessKey.size() != 64 || !isLowercaseHex(fields_.accessKey)) return Error(ErrorCode::config_invalid_key);
    } else return Error(ErrorCode::wizard_prerequisite_failed);
    return ok();
}
Result<WizardAttempt> WizardModel::beginCheck(std::string attemptId, std::int64_t nowMs) {
    if (busy_ || committing_) return Error(ErrorCode::busy);
    PHPVPN_TRY(validateCurrent());
    if (!Uuid::isValidV4(attemptId) || sequence_ == (std::numeric_limits<std::uint64_t>::max)()) return Error(ErrorCode::invalid_argument);
    const auto kind = step_ == WizardStep::serverUrl ? WizardOperationKind::url :
        step_ == WizardStep::serverSecret ? WizardOperationKind::secret : WizardOperationKind::access;
    const auto count = static_cast<std::int64_t>(kind) + 1;
    const auto seconds = view_.context->runtime.responseTimeoutSeconds;
    if (seconds < 1 || seconds > kMaxHttpTimeoutSeconds) return Error(ErrorCode::config_invalid_limit);
    const auto budget = count * seconds * 1000 + 15000;
    if (nowMs < 0 || nowMs > (std::numeric_limits<std::int64_t>::max)() - budget) return Error(ErrorCode::timeout);
    invalidate(static_cast<std::size_t>(kind));
    attempt_ = WizardAttempt{std::move(attemptId), {}, ++sequence_, kind, nowMs + budget, false};
    busy_ = true; error_.reset();
    return *attempt_;
}
bool WizardModel::acceptStart(std::string operationId, std::string_view attemptId, std::uint64_t sequence) {
    if (!attempt_ || attempt_->closed || attempt_->clientAttemptId != attemptId || attempt_->sequence != sequence || !Uuid::isValidV4(operationId)) return false;
    attempt_->operationId = std::move(operationId); return true;
}
bool WizardModel::applyOperation(const WizardOperationResult& operation, std::int64_t nowMs) {
    if (!attempt_ || attempt_->closed || operation.operationId != attempt_->operationId || operation.kind != attempt_->kind ||
        operation.clientAttemptId != attempt_->clientAttemptId || operation.attemptSequence != attempt_->sequence) return false;
    if (expireAttempt(nowMs)) return false;
    if (operation.connectionEpoch != view_.connectionEpoch || operation.state == WizardOperationState::interrupted) { interrupt(); return true; }
    if (operation.state == WizardOperationState::pending || operation.state == WizardOperationState::running || operation.state == WizardOperationState::cancelling) return true;
    if (operation.state != WizardOperationState::succeeded) {
        fail(operation.error.value_or(WizardIpc::safeError(Error(ErrorCode::cancelled)))); return true;
    }
    busy_ = false; attempt_->closed = true;
    checks_[static_cast<std::size_t>(operation.kind)] = CheckpointResult::success;
    if (step_ == WizardStep::serverUrl) step_ = WizardStep::serverSecret;
    else if (step_ == WizardStep::serverSecret) {
        if (fields_.virtualIp.empty() && operation.suggestedIp) { fields_.virtualIp = *operation.suggestedIp; updateDefaultKey(); }
        step_ = WizardStep::identity;
    } else step_ = WizardStep::contracts;
    return true;
}
bool WizardModel::expireAttempt(std::int64_t nowMs) {
    if (!attempt_ || attempt_->closed || nowMs < attempt_->deadlineMs) return false;
    fail(WizardIpc::safeError(Error(ErrorCode::timeout))); return true;
}
void WizardModel::fail(WizardError error) {
    error_ = std::move(error); busy_ = false;
    if (attempt_ && !attempt_->closed) { checks_[static_cast<std::size_t>(attempt_->kind)] = CheckpointResult::failed; attempt_->closed = true; }
}
void WizardModel::interrupt() {
    if (committing_) return;
    if (attempt_) attempt_->closed = true;
    busy_ = false; invalidate(0); step_ = WizardStep::disconnectGate;
    error_ = WizardIpc::safeError(Error(ErrorCode::wizard_connection_interrupted));
}
void WizardModel::sessionExpired() {
    busy_ = false; if (attempt_) attempt_->closed = true;
    if (!committing_) step_ = WizardStep::sessionExpired;
    error_ = WizardIpc::safeError(Error(ErrorCode::wizard_session_expired));
}
void WizardModel::setCommitting(bool value) { committing_ = value; busy_ = value; }
void WizardModel::commitUnknown() { committing_ = busy_ = false; step_ = WizardStep::commitUnknown; }
void WizardModel::commitFailed(WizardError error) {
    committing_ = busy_ = false; step_ = WizardStep::contracts; fail(std::move(error));
}
void WizardModel::confirmed() { committing_ = busy_ = false; step_ = WizardStep::finished; }
void WizardModel::cancellationFinished() { busy_ = false; if (attempt_) attempt_->closed = true; }
} // namespace phpvpn
