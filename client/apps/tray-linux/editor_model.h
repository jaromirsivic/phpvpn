// GTK-free part of the Linux Edit Config window (tray_UX.md UX06), the portable
// counterpart of the state kept by the Win32 EditorWindow (apps/tray/editor_window.cpp):
// window/draft/operation generations, the single save with its original deadline,
// the confirmed/notSent/rejected/unknown classification texts, the Reset reply
// validation and the presentation of the received document. The GTK adapter
// (editor_window.cpp) owns the widgets, the text buffer and its modified flag.
#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "core/base/result.h"
#include "core/ipc/wizard_ipc.h"
#include "editor_document.h"
#include "lifecycle.h"
#include "save_response.h"

namespace phpvpn::tray {
inline constexpr std::string_view kEditorTitle = "PHPVPN configuration";
inline constexpr std::string_view kEditorHeading = "Edit configuration";
inline constexpr std::string_view kEditorDialogTitle = "Edit Config";
inline constexpr std::string_view kEditorLiteralSuffix = " - JSON string literal (raw NUL shown as \\u0000)";
inline constexpr std::string_view kEditorResetQuestion =
    "Restore the default configuration in this editor? The file will change only after you choose Save.";
inline constexpr std::string_view kEditorResetFailed =
    "The default configuration could not be loaded. Your text has been preserved.";
inline constexpr std::string_view kEditorPreserved = "\n\nYour text has been preserved.";
inline constexpr std::string_view kEditorInvalidUnicode =
    "The edited text contains an invalid Unicode sequence. Correct it before saving.";
inline constexpr std::string_view kEditorLiteralRequired =
    "Enter a complete JSON string literal, including its surrounding quotes. The file has not been changed.";
inline constexpr std::string_view kEditorUnknownText =
    "The file may already have changed. The save result is unknown. Close this editor and open Edit Config to "
    "inspect the current configuration. The save will not be retried automatically.";
inline constexpr std::string_view kEditorNotSentText = "The save was not sent. Your text has been preserved.";
inline constexpr std::string_view kEditorRejectedText =
    "The service rejected the configuration before publication. Your text has been preserved.";

// Whitespace-only reflow of valid JSON (exact tokens, escapes and member order are
// kept); invalid JSON is returned unchanged. Same algorithm as the Win32 editor.
std::string prettyEditorJson(const std::string& text);

// What the text view shows for a received document and the resulting window title.
struct EditorPresentation {
    std::string text;
    std::string title;
    bool literal = false;
};
EditorPresentation presentEditorDocument(const EditorDocument& document, const std::string& original);

// The document a Save sends. Unmodified text uses the original bytes (EditorDocument).
// On failure the text is the Edit Config error message and nothing is sent.
struct EditorSaveText {
    std::optional<std::string> document;
    std::string_view error;
};
EditorSaveText editorSaveText(const EditorDocument& document, std::string edited, bool modified);

// Message and severity of a save that was not confirmed.
struct EditorSaveNotice {
    std::string text;
    bool warning = false;  // unknown is a warning, notSent/rejected an error
};
EditorSaveNotice editorSaveNotice(const SaveResult& result);

// Validated get-default-config reply: exactly one of document or error is set. The
// shared template is normalised and fully validated before it may replace a draft.
struct EditorDefault {
    std::string document;
    std::string error;
};
EditorDefault editorDefaultFromReply(const Result<WizardReply>& reply);

// Lifecycle of one editor instance. Single owner, UI thread only. The adapter reports
// every draft change (text buffer "changed") and replaced content; the buffer's
// modified flag is the dirty state and is passed in where needed.
class EditorState {
public:
    // A new window identity; clears saving, unknown and saved.
    void open();
    // The window was destroyed (Cancel, discard, confirmed save, session end).
    void close();
    void endSession();
    bool isOpen() const { return open_; }
    bool ending() const { return ending_; }
    void draftChanged();
    void resetStarted();
    void resetFinished();
    bool resetting() const { return resetting_; }
    bool saving() const { return saving_; }
    bool unknown() const { return unknown_; }
    bool savedPending() const { return savedPending_; }

    // Deadline on the monotonic clock of ipc_async (monotonicMs).
    std::optional<SaveOperation> beginSave(std::uint64_t nowMs, std::uint64_t deadlineMs);
    enum class Finish { ignored, confirmed, failed };
    Finish finishSave(const SaveResult& result);

    // Enabled states of the Win32 updateEnabled().
    bool fieldSensitive() const { return !saving_ && !savedPending_ && !resetting_; }
    bool fieldEditable() const { return !unknown_; }
    bool saveEnabled() const { return !saving_ && !savedPending_ && !unknown_ && !resetting_ && !ending_; }
    bool resetEnabled() const { return !saving_ && !savedPending_ && !unknown_ && !resetting_ && !ending_; }
    bool closeEnabled() const { return !saving_; }

    LifecycleSnapshot snapshot(bool modified) const;
    const SaveOperation& operation() const { return operation_; }
    std::uint64_t windowGeneration() const { return windowGeneration_; }

private:
    void bump() { ++revision_; }
    bool open_ = false, ending_ = false;
    bool saving_ = false, savedPending_ = false, unknown_ = false, resetting_ = false;
    std::uint64_t windowGeneration_ = 0, draftGeneration_ = 0, operationGeneration_ = 0;
    // Changes with every generation, flag or deadline change: the lifecycle generation.
    std::uint64_t revision_ = 0;
    SaveOperation operation_;
};
}  // namespace phpvpn::tray
