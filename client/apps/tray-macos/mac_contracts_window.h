// Communication contracts window of the macOS tray (tray_UX.md UX10-UX12), the
// counterpart of the Win32 ContractsWindow + ContractsSaveController and of
// apps/tray-linux/contracts_window.
//
// Opened by the tray's one-shot get-config-document check through
// TrayWindowHooks::openContracts (valid availability only); a repeated choice only
// activates it. The portable ContractsModel keeps every draft by remote IP, only the
// current page of 20 rows owns controls. ContractsSaveMachine decides every stage:
// Save sends one set-config with a 30 s deadline on its own channel; a confirmed save
// goes to TrayApp::requestReconnectAfterSave, an unknown result offers "Check current
// configuration" (get-config-document) and, after a differing result, "Save draft
// again". Password SHA children are PasswordShaWindow.
#pragma once
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "contracts_save_machine.h"
#include "contracts_view.h"
#include "ipc_async.h"
#include "lifecycle.h"
#include "mac_password_sha_window.h"
#include "mac_ui.h"

namespace phpvpn::tray {
class TrayApp;

class ContractsWindow final : public LifecycleParticipant {
public:
    using CryptoFactory = std::function<Result<std::unique_ptr<CryptoProvider>>()>;
    explicit ContractsWindow(TrayApp& app, CryptoFactory cryptoFactory = createPlatformCryptoProvider);
    ~ContractsWindow() override;
    ContractsWindow(const ContractsWindow&) = delete;
    ContractsWindow& operator=(const ContractsWindow&) = delete;

    bool activate();
    bool open(std::string_view document, std::string path, std::string revision);
    NSWindow* window() const { return window_; }
    void setConnectionActive(bool active);
    ContractsLifecycleSnapshot contractsSnapshot() const;

    LifecycleSnapshot lifecycleSnapshot() const override;
    bool approvedDiscard(const LifecycleSnapshot& approved) override;
    void endSession() override;

private:
    struct Row {
        Ipv4Address remote;
        std::string identity;
        PVStackView* box = nil;
        NSButton* toggle = nil;
        NSTextField* key = nil;
        NSButton* change = nil;
        bool lastValid = true;
        bool lastDirty = false;
    };
    void build();
    bool createRows();
    void destroyRows();
    void createPager();
    void changePage(int page);
    void focusContract(Ipv4Address remote, int column);
    int pageCount() const;
    bool acceptKey(Ipv4Address remote, std::u16string_view text);
    Row* findRow(Ipv4Address remote);
    void syncRow(Row& row);
    void rowEvent(Ipv4Address remote, int column);
    void openPasswordSha(Ipv4Address remote);
    void save(bool repeat);
    void check();
    void onSaveResult(const SaveResult& result);
    void onCheckResult(std::uint64_t checkGeneration, WizardResult& result);
    void handle(ContractsEvent event);
    void refresh();
    void layoutTable();
    void close();
    void closeWindow();
    void closeChildren();

    TrayApp& app_;
    CryptoFactory cryptoFactory_;
    ContractsSaveMachine machine_;
    IpcChannel writer_, checker_;
    std::optional<ContractsLifecycleSnapshot> savedSnapshot_;
    std::unique_ptr<CryptoProvider> crypto_;
    std::vector<Row> rows_;
    std::vector<std::unique_ptr<PasswordShaWindow>> children_;
    int page_ = 0;
    std::string localError_;
    bool ending_ = false, closing_ = false, changing_ = false, connectionActive_ = false;
    bool tableSensitive_ = true;
    mutable ContractsLifecycleSnapshot lastSnapshot_;
    mutable bool lastSaved_ = false;
    mutable std::uint64_t revision_ = 0;

    PVWindow* window_ = nil;
    PVStackView* root_ = nil;
    NSTextField* notice_ = nil;
    NSScrollView* headerScroll_ = nil;
    PVStackView* header_ = nil;
    NSScrollView* rowsScroll_ = nil;
    PVStackView* rowsBox_ = nil;
    PVStackView* pager_ = nil;
    NSTextField* error_ = nil;
    NSButton* check_ = nil;
    NSButton* repeat_ = nil;
    NSButton* cancel_ = nil;
    NSButton* save_ = nil;
    id boundsObserver_ = nil;
    mac::Deferred deferred_;
};
}  // namespace phpvpn::tray
