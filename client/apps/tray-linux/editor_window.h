// Edit Config window of the GTK tray (tray_UX.md UX06), the counterpart of the
// save-mode Win32 EditorWindow (apps/tray/editor_window.cpp) and of its
// TrayApplication wiring (openEditor, startCommand set-config, onSaved).
//
// The tray's one-shot get-config-document check (including the missing ->
// create-default-config question) opens it through TrayWindowHooks::openEditor
// with the service's configPath; a repeated Edit Config only activates it.
// Save publishes through TrayApp::requestConfigSave with one original 30 s
// deadline; a confirmed save hands the exact snapshot to
// TrayApp::requestReconnectAfterSave (the shared "disconnect and reconnect now?"
// flow), which finally discards the editor. Reset asks get-default-config (5 s)
// on its own channel. The GTK-free rules live in editor_model.h.
#pragma once
#include <gtk/gtk.h>

#include <functional>
#include <string>

#include "editor_model.h"
#include "gtk_util.h"  // DeferredCalls
#include "ipc_async.h"
#include "lifecycle.h"

namespace phpvpn::tray {
class TrayApp;

// Window-local CSS shared by Edit Config, Communication contracts and Password SHA:
// the red Reset role, the editor frame, the invalid Key field, changed contract rows
// and the table header. Generated from ui_style.h; safe to call repeatedly.
namespace config_style {
inline constexpr const char kResetButton[] = "phpvpn-reset";
inline constexpr const char kEditorFrame[] = "phpvpn-editor-frame";
inline constexpr const char kInvalid[] = "phpvpn-invalid";
inline constexpr const char kChanged[] = "phpvpn-changed";
inline constexpr const char kTable[] = "phpvpn-table";
inline constexpr const char kTableHeader[] = "phpvpn-table-header";
inline constexpr const char kTableRow[] = "phpvpn-table-row";
inline constexpr const char kValidation[] = "phpvpn-validation";
void install();
}  // namespace config_style

class EditorWindow final : public LifecycleParticipant {
public:
    // Created by installEditorWindow, which owns it through the TrayApp hooks.
    explicit EditorWindow(TrayApp& app);
    ~EditorWindow() override;
    EditorWindow(const EditorWindow&) = delete;
    EditorWindow& operator=(const EditorWindow&) = delete;

    bool activate();
    bool open(const std::string& document, const std::string& path);
    GtkWindow* window() const { return window_ ? GTK_WINDOW(window_) : nullptr; }

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
    void onDestroyed();
    bool modified() const;
    std::string bufferText() const;

    TrayApp& app_;
    EditorState state_;
    EditorDocument document_;
    IpcChannel resetChannel_;
    GtkWidget* window_ = nullptr;
    GtkWidget* view_ = nullptr;
    GtkWidget* reset_ = nullptr;
    GtkWidget* cancel_ = nullptr;
    GtkWidget* save_ = nullptr;
    GtkTextBuffer* buffer_ = nullptr;
    bool changingContent_ = false;
    DeferredCalls deferred_;
};

// Registers Edit Config (hooks activateEditor/openEditor, lifecycle participant).
// Called from the TrayApp constructor after the Wizard, before Communication contracts.
void installEditorWindow(TrayApp& app);
}  // namespace phpvpn::tray
