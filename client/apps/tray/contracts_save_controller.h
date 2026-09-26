#pragma once
#include "contracts_save_machine.h"
#include "wizard_reader.h"

namespace phpvpn::tray {
// Win32 owner of ContractsSaveMachine: runs the save and check IPC channels and
// posts their completion to the window. All state decisions stay in the machine.
class ContractsSaveController {
public:
    explicit ContractsSaveController(std::wstring pipeName) : writer_(pipeName), checker_(std::move(pipeName)) {}
    ~ContractsSaveController() { close(); }
    Status load(CryptoProvider& crypto, std::string_view document, std::string path, std::string revision) {
        return machine_.load(crypto, document, std::move(path), std::move(revision));
    }
    ContractsModel* model() const { return machine_.model(); }
    ContractsStage stage() const { return machine_.stage(); }
    ContractsLifecycleSnapshot snapshot() const { return machine_.snapshot(checker_.busy()); }
    void changed() { machine_.changed(); }
    // prepare retires child targets. The view closes children before dispatch.
    Status prepare(bool confirmedRepeat = false) { return machine_.prepare(confirmedRepeat, checker_.busy()); }
    bool dispatch(HWND target, UINT message);
    ContractsEvent poll();
    bool check(HWND target, UINT message);
    bool canRepeat() const { return machine_.canRepeat(checker_.busy()); }
    const std::wstring& notice() const;
    void close();
private:
    ContractsSaveMachine machine_;
    CommandReader writer_;
    WizardReader checker_;
    mutable std::wstring notice_;
};
} // namespace phpvpn::tray
