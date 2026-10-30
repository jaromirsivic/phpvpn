// Edit Config window of the macOS tray (tray_UX.md UX06), the counterpart of the
// save-mode Win32 EditorWindow and apps/tray-linux/editor_window. The GTK-free rules
// (generations, the single save with its original 30 s deadline, the
// confirmed/notSent/rejected/unknown texts, the Reset reply validation and the
// presentation of the received document) are apps/tray-linux/editor_model.
//
// The tray's one-shot get-config-document check (including the missing ->
// create-default-config question) opens it through TrayWindowHooks::openEditor with
// the service's configPath; a repeated Edit Config only activates it. Save publishes
// through TrayApp::requestConfigSave; a confirmed save hands the exact snapshot to
// TrayApp::requestReconnectAfterSave, which finally discards the editor. Reset asks
// get-default-config (5 s) on its own channel.
#pragma once
#include <string>

#include "editor_model.h"
#include "ipc_async.h"
#include "lifecycle.h"
#include "mac_ui.h"

namespace phpvpn::tray {
class TrayApp;

class EditorWindow final : public LifecycleParticipant {
public:
    explicit EditorWindow(TrayApp& app);
    ~EditorWindow() override;
    EditorWindow(const EditorWindow&) = delete;
    EditorWindow& operator=(const EditorWindow&) = delete;

    bool activate();
    bool open(const std::string& document, const std::string& path);
    NSWindow* window() const { return window_; }

    LifecycleSnapshot lifecycleSnapshot() const override;
    bool approvedDiscard(const LifecycleSnapshot& approved) override;
    void endSession() override;

private:
    void build(const std::string& path);
    void setContent(const std::string& content);
    void updateEnabled();
    void save();
    void reset();
    void finishReset(std::uint64_t windowGeneration, WizardResult& result);
    void finishSave(const SaveResult& result);
    void requestClose();
    void destroy();
    void focusEditor();
    bool modified() const { return window_ && modified_; }

    TrayApp& app_;
    EditorState state_;
    EditorDocument document_;
    IpcChannel resetChannel_;
    PVWindow* window_ = nil;
    PVStackView* root_ = nil;
    NSTextView* view_ = nil;
    NSButton* reset_ = nil;
    NSButton* cancel_ = nil;
    NSButton* save_ = nil;
    bool changingContent_ = false;
    bool modified_ = false;
    mac::Deferred deferred_;
};
}  // namespace phpvpn::tray
