// Communication contracts window of the GTK tray (tray_UX.md UX10-UX12), the
// counterpart of the Win32 ContractsWindow + ContractsSaveController
// (apps/tray/contracts_window.cpp, contracts_save_controller.cpp).
//
// Opened by the tray's one-shot get-config-document check through
// TrayWindowHooks::openContracts (valid availability only); a repeated choice only
// activates it. The portable ContractsModel keeps every draft by remote IP, only the
// current page of 20 rows owns widgets. ContractsSaveMachine decides every stage:
// Save sends one set-config with a 30 s deadline on its own channel; a confirmed
// save goes to TrayApp::requestReconnectAfterSave, an unknown result offers
// "Check current configuration" (get-config-document) and, after a differing
// result, "Save draft again". Password SHA children are PasswordShaWindow.
#pragma once
#include <gtk/gtk.h>

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "contracts_save_machine.h"
#include "contracts_view.h"
#include "editor_window.h"
#include "ipc_async.h"
#include "lifecycle.h"
#include "password_sha_window.h"

namespace phpvpn::tray {
class TrayApp;

class ContractsWindow final : public LifecycleParticipant {
public:
    using CryptoFactory = std::function<Result<std::unique_ptr<CryptoProvider>>()>;
    // Created by installContractsWindow, which owns it through the TrayApp hooks.
    explicit ContractsWindow(TrayApp& app, CryptoFactory cryptoFactory = createPlatformCryptoProvider);
    ~ContractsWindow() override;
    ContractsWindow(const ContractsWindow&) = delete;
    ContractsWindow& operator=(const ContractsWindow&) = delete;

    bool activate();
    bool open(std::string_view document, std::string path, std::string revision);
    GtkWindow* window() const { return window_ ? GTK_WINDOW(window_) : nullptr; }
    void setConnectionActive(bool active);
    ContractsLifecycleSnapshot contractsSnapshot() const;

    LifecycleSnapshot lifecycleSnapshot() const override;
    bool approvedDiscard(const LifecycleSnapshot& approved) override;
    void endSession() override;

private:
    struct Row {
        Ipv4Address remote;
        std::string identity;
        GtkWidget* box = nullptr;
        GtkWidget* toggle = nullptr;
        GtkWidget* key = nullptr;
        GtkWidget* change = nullptr;
        bool lastValid = true;
        bool lastDirty = false;
    };
    struct Binding {
        ContractsWindow* owner;
        std::size_t index;
        int column;
    };
    void build();
    bool createRows();
    void destroyRows();
    void createPager();
    void changePage(int page);
    void focusContract(Ipv4Address remote, int column);
    int pageCount() const;
    bool acceptKey(Ipv4Address remote, std::u16string_view text);
    void syncRow(Row& row);
    void rowEvent(std::size_t index, int column);
    void openPasswordSha(std::size_t index);
    void save(bool repeat);
    void check();
    void onSaveResult(const SaveResult& result);
    void onCheckResult(std::uint64_t checkGeneration, WizardResult& result);
    void handle(ContractsEvent event);
    void refresh();
    void applyKeyWidth(int viewportWidth);
    void close();
    void closeWindow();
    void closeChildren();
    void onDestroyed();
    bool onKey(GdkEventKey* event);

    TrayApp& app_;
    CryptoFactory cryptoFactory_;
    ContractsSaveMachine machine_;
    IpcChannel writer_, checker_;
    std::optional<ContractsLifecycleSnapshot> savedSnapshot_;
    std::unique_ptr<CryptoProvider> crypto_;
    std::vector<Row> rows_;
    std::vector<std::unique_ptr<Binding>> bindings_;
    std::vector<std::unique_ptr<PasswordShaWindow>> children_;
    int page_ = 0;
    int keyWidth_ = 260;
    std::string localError_;
    bool ending_ = false, closing_ = false, changing_ = false, connectionActive_ = false;
    guint keyWidthIdle_ = 0, pageIdle_ = 0;
    int pendingPage_ = -1;
    bool pendingLastRow_ = false;
    mutable ContractsLifecycleSnapshot lastSnapshot_;
    mutable bool lastSaved_ = false;
    mutable std::uint64_t revision_ = 0;

    GtkWidget* window_ = nullptr;
    GtkWidget* local_ = nullptr;
    GtkWidget* notice_ = nullptr;
    GtkWidget* headerKey_ = nullptr;
    GtkWidget* rowsScroller_ = nullptr;
    GtkWidget* rowsBox_ = nullptr;
    GtkWidget* pager_ = nullptr;
    GtkWidget* error_ = nullptr;
    GtkWidget* check_ = nullptr;
    GtkWidget* repeat_ = nullptr;
    GtkWidget* cancel_ = nullptr;
    GtkWidget* save_ = nullptr;
    DeferredCalls deferred_;
};

// Registers Communication contracts (hooks, status listener, lifecycle participant).
// Called from the TrayApp constructor after installEditorWindow.
void installContractsWindow(TrayApp& app);
}  // namespace phpvpn::tray
