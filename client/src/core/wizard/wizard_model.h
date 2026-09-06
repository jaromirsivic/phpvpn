#pragma once
#include <array>
#include "core/wizard/wizard_types.h"

namespace phpvpn {
enum class WizardStep { loading, disconnectGate, serverUrl, serverSecret, identity, contracts, loadError, sessionExpired, commitUnknown, finished };
struct WizardAttempt {
    std::string clientAttemptId, operationId;
    std::uint64_t sequence = 0;
    WizardOperationKind kind = WizardOperationKind::url;
    std::int64_t deadlineMs = 0;
    bool closed = false;
};
// Pure presentation state. No Win32, IPC transport, file access or HTTP.
class WizardModel {
public:
    void load(WizardView view, bool preserveFields = false);
    const WizardView& view() const { return view_; }
    const WizardFields& fields() const { return fields_; }
    WizardStep step() const { return step_; }
    bool busy() const { return busy_; }
    bool committing() const { return committing_; }
    bool useDefaultAccessKey() const { return defaultKey_; }
    bool useDefaultSecret() const { return defaultSecret_; }
    bool secretLiteralMode() const { return literalSecret_; }
    const std::string& secretEditorText() const { return secretText_; }
    bool createContracts() const { return createContracts_; }
    bool contractsAllowed() const;
    std::size_t contractCount() const;
    const std::optional<WizardError>& error() const { return error_; }
    const std::optional<WizardAttempt>& attempt() const { return attempt_; }
    const std::array<CheckpointResult, 3>& checks() const { return checks_; }
    bool setUrl(std::string text);
    bool setSecretEditor(std::string text);
    bool setDefaultSecret(bool checked);
    bool setIp(std::string text);
    bool setAccessKey(std::string text);
    bool setDefaultAccessKey(bool checked);
    void setCreateContracts(bool checked);
    bool back();
    Status validateCurrent() const;
    Result<WizardAttempt> beginCheck(std::string attemptId, std::int64_t nowMs);
    bool acceptStart(std::string operationId, std::string_view attemptId, std::uint64_t sequence);
    bool applyOperation(const WizardOperationResult& operation, std::int64_t nowMs);
    bool expireAttempt(std::int64_t nowMs);
    void fail(WizardError error);
    void interrupt();
    void sessionExpired();
    void setCommitting(bool value);
    void commitUnknown();
    void commitFailed(WizardError error);
    void confirmed();
    void cancellationFinished();
private:
    void invalidate(std::size_t from);
    void updateDefaultKey();
    void updateSecretText();
    WizardView view_;
    WizardFields fields_;
    WizardStep step_ = WizardStep::loading;
    bool busy_ = false, committing_ = false;
    bool defaultKey_ = true, defaultSecret_ = true, literalSecret_ = false, secretValid_ = true, createContracts_ = true;
    std::string secretText_;
    std::uint64_t sequence_ = 0;
    std::optional<WizardAttempt> attempt_;
    std::optional<WizardError> error_;
    std::array<CheckpointResult, 3> checks_{CheckpointResult::pending, CheckpointResult::pending, CheckpointResult::pending};
};
} // namespace phpvpn
