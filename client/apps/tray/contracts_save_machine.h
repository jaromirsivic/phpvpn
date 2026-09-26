#pragma once
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include "core/ipc/wizard_ipc.h"
#include "core/presentation/contracts_model.h"
#include "save_response.h"

namespace phpvpn::tray {
enum class ContractsStage { loading, editing, invalid, saving, saved, saveUnknown, closed };
enum class ContractsEvent { none, saved, matched, rejected, unknown, different, checkFailed };
struct ContractsLifecycleSnapshot {
    bool open = false, dirty = false, saving = false, unknown = false, checking = false;
    std::uint64_t windowGeneration = 0, draftGeneration = 0, operationGeneration = 0,
        checkGeneration = 0, deadlineMs = 0;
    bool operator==(const ContractsLifecycleSnapshot&) const = default;
};
// Measures the actual existing four-member request without sending it. UUIDv4
// identifiers have a fixed encoded length. The synthetic ID is never sent.
Result<std::size_t> contractsEnvelopeSize(std::string_view document);

// Portable Communication Contracts save state machine, shared by the Win32 and
// GTK trays. It owns the model, stages, generations and UTF-8 notices but no
// transport or window: dispatch and check hand requests to an injected sender,
// the owner feeds correlated terminal results back and reports whether its
// configuration check channel is busy. Single owner, UI thread only.
class ContractsSaveMachine {
public:
    // Returns true when the transport accepted the request. The payload is
    // wiped by the machine after the call and must not be retained.
    using SaveSender = std::function<bool(const SaveOperation& operation, const std::string& payload)>;
    using CheckSender = std::function<bool(std::uint64_t checkGeneration)>;
    ContractsSaveMachine() = default;
    ContractsSaveMachine(const ContractsSaveMachine&) = delete;
    ContractsSaveMachine& operator=(const ContractsSaveMachine&) = delete;
    ~ContractsSaveMachine() { close(); }
    Status load(CryptoProvider& crypto, std::string_view document, std::string path, std::string revision);
    ContractsModel* model() const { return model_.get(); }
    ContractsStage stage() const;
    // Raw stage checks; stage() additionally reports editing with blocking errors as invalid.
    bool saving() const { return stage_ == ContractsStage::saving; }
    ContractsLifecycleSnapshot snapshot(bool checking) const;
    void changed();
    // prepare retires child targets. The view closes children before dispatch.
    Status prepare(bool confirmedRepeat, bool checking);
    // Deadline is nowMs plus kContractsSaveDeadlineMs on the caller's monotonic clock.
    bool dispatch(std::uint64_t nowMs, const SaveSender& send);
    // Terminal save result. A foreign or stale result returns none and changes nothing.
    ContractsEvent saveFinished(const SaveResult& result);
    bool check(bool checking, const CheckSender& send);
    // Terminal configuration check reply for checkGeneration. Stale replies return none.
    ContractsEvent checkFinished(std::uint64_t checkGeneration, const Result<WizardReply>& reply);
    bool canRepeat(bool checking) const { return stage_ == ContractsStage::saveUnknown && repeatAllowed_ && !checking; }
    const std::string& notice() const { return notice_; }
    void close();
private:
    std::unique_ptr<ContractsModel> model_;
    ContractsStage stage_ = ContractsStage::closed;
    std::uint64_t windowGeneration_ = 0, draftGeneration_ = 0, operationGeneration_ = 0, checkGeneration_ = 0;
    std::optional<SaveOperation> operation_;
    bool repeatAllowed_ = false, prepared_ = false;
    std::string notice_;
};
} // namespace phpvpn::tray
