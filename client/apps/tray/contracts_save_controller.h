#pragma once
#include "core/presentation/contracts_model.h"
#include "wizard_reader.h"

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
class ContractsSaveController {
public:
    explicit ContractsSaveController(std::wstring pipeName) : writer_(pipeName), checker_(std::move(pipeName)) {}
    ~ContractsSaveController() { close(); }
    Status load(CryptoProvider& crypto, std::string_view document, std::string path, std::string revision);
    ContractsModel* model() const { return model_.get(); }
    ContractsStage stage() const;
    ContractsLifecycleSnapshot snapshot() const;
    void changed();
    // prepare retires child targets. The view closes children before dispatch.
    Status prepare(bool confirmedRepeat = false);
    bool dispatch(HWND target, UINT message);
    ContractsEvent poll();
    bool check(HWND target, UINT message);
    bool canRepeat() const { return stage_ == ContractsStage::saveUnknown && repeatAllowed_ && !checker_.busy(); }
    const std::wstring& notice() const { return notice_; }
    void close();
private:
    std::unique_ptr<ContractsModel> model_;
    CommandReader writer_;
    WizardReader checker_;
    ContractsStage stage_ = ContractsStage::closed;
    std::uint64_t windowGeneration_ = 0, draftGeneration_ = 0, operationGeneration_ = 0, checkGeneration_ = 0;
    std::optional<SaveOperation> operation_;
    bool repeatAllowed_ = false, prepared_ = false;
    std::wstring notice_;
};
} // namespace phpvpn::tray
