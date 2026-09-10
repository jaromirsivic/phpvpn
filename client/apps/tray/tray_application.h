#pragma once
#include <memory>
#include <optional>
#include <string>
#include "editor_window.h"
#include "progress_window.h"
#include "statistics_window.h"
#include "wizard_window.h"
#include "contracts_window.h"
#include "menu_model.h"
#include "window_utils.h"
#include "ui_resources.h"
#include "shell_adapter.h"
#include "system_dialog.h"

namespace phpvpn::tray {
// Explicit, owned runtime dependencies. No production default exists here.
// Construct/destroy on the UI thread. The shell outlives ShellIcon and all HWNDs.
struct TrayRuntime {
    std::wstring pipeName;
    std::wstring mutexName;
    std::unique_ptr<ShellAdapter> shell;
    std::wstring tooltipOverride;
};
struct CloseKernelHandle { void operator()(HANDLE handle) const noexcept { CloseHandle(handle); } };
class TrayApplication {
public:
    explicit TrayApplication(TrayRuntime runtime);
    ~TrayApplication();
    TrayApplication(const TrayApplication&) = delete;
    TrayApplication& operator=(const TrayApplication&) = delete;
    HWND window() const noexcept { return window_; }
    bool initialise(HINSTANCE instance);
    int run();
    EditorLifecycleSnapshot editorLifecycleSnapshot() const { return editor_.lifecycleSnapshot(); }
    bool approvedDiscardEditor(const EditorLifecycleSnapshot& approved) { return editor_.approvedDiscard(approved); }
    // Editor/Save participant only. The common session host and Wizard
    // participant are integrated by their owners after this seam is ready.
    void endEditorSession();
    // Wizard participant only. No common Exit dialog or session host here.
    WizardLifecycleSnapshot wizardLifecycleSnapshot() const { return wizard_.lifecycleSnapshot(); }
    bool approvedDiscardWizard(const WizardLifecycleSnapshot& approved) { return wizard_.approvedDiscard(approved); }
    void endWizardSession() { wizard_.endSession(); }
    ContractsLifecycleSnapshot contractsLifecycleSnapshot() const { return contracts_.lifecycleSnapshot(); }
    void requestExit();
    void endSession();
private:
    static LRESULT CALLBACK procedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    void showMenu(bool keyboard);
    void updateIcon();
    void onSnapshot(const TrayStatus& snapshot);
    void startCommand(IpcCommand command, const std::string& payload = {});
    void finishCommand();
    void startConfigAction(MenuAction action);
    void finishConfigAction();
    void finishDisconnect();
    void finishInfo();
    void showConfigWarning(std::string_view availability, std::string_view path, std::string_view reason = {});
    void openEditor(const std::string& text, const std::string& path);
    void advanceExit();
    void showError(const wchar_t* message) { showSystemMessage(window_, message, L"PHPVPN", SystemDialogIcon::error); }
    bool connectedOrConnecting() const {
        return snapshot_.state == "connected" || snapshot_.state == "connecting" || snapshot_.state == "recovering";
    }
    TrayRuntime runtime_;
    UniqueUiHandle<HANDLE, CloseKernelHandle> mutex_;
    UiResources resources_;
    ShellIcon shellIcon_;
    WindowPlacementContext menuPlacement_, commandPlacement_, configPlacement_, infoPlacement_;
    bool alreadyRunning_ = false;
    bool initialisationAttempted_ = false, initialised_ = false;
    bool exitRequested_ = false, exitConfirming_ = false, endingSession_ = false;
    struct ExitApproval {
        EditorLifecycleSnapshot editor;
        WizardLifecycleSnapshot wizard;
        ContractsLifecycleSnapshot contracts;
    };
    std::optional<ExitApproval> exitApproval_;
    HINSTANCE instance_ = nullptr;
    HWND window_ = nullptr;
    TrayStatus snapshot_;
    bool statusUnavailable_ = true;
    PollSchedule polling_{kStatusIntervalMs};
    StatusReader statusReader_;
    CommandReader commands_;
    CommandReader disconnect_, info_;
    WizardReader config_;
    std::optional<MenuAction> configAction_;
    std::optional<IpcCommand> pendingCommand_;
    std::optional<SaveOperation> saveOperation_;
    EditorWindow editor_;
    EditorWindow infoWindow_;
    WizardWindow wizard_;
    ContractsWindow contracts_;
    ProgressWindow progress_;
    ProgressWindow disconnectProgress_;
    bool connectSuperseded_ = false;
    bool disconnectPending_ = false;
    StatisticsWindow statistics_;
};

} // namespace phpvpn::tray
