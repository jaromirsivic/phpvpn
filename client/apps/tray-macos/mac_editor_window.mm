#include "mac_editor_window.h"

#include <memory>

#include "core/base/secure_buffer.h"
#include "mac_tray_app.h"

namespace phpvpn::tray {
EditorWindow::EditorWindow(TrayApp& app) : app_(app), resetChannel_(app.endpoint(), app.dispatcher()) {}

EditorWindow::~EditorWindow() {
    // Owned by the TrayApp hooks: the application has already ended the session.
    resetChannel_.abandon();
    destroy();
}

void installEditorWindow(TrayApp& app) {
    auto editor = std::make_shared<EditorWindow>(app);
    EditorWindow* raw = editor.get();
    app.registerParticipant({raw, [raw] { return raw->window(); }});
    app.hooks().activateEditor = [raw] { return raw->activate(); };
    app.hooks().openEditor = [editor](const std::string& document, const std::string& path) {
        return editor->open(document, path);
    };
}

bool EditorWindow::activate() {
    if (!window_) return false;
    // Reactivation neither reads nor resets the open draft (tray_UX.md UX03).
    mac::presentWindow(window_);
    return true;
}

bool EditorWindow::open(const std::string& document, const std::string& path) {
    if (state_.ending()) return false;
    if (window_) destroy();
    state_.open();
    build(path);
    setContent(document);
    updateEnabled();
    mac::presentWindow(window_);
    // Focus starts in the editor (UX06).
    focusEditor();
    return true;
}

void EditorWindow::focusEditor() {
    if (window_ && view_) [window_ makeFirstResponder:view_];
}

void EditorWindow::build(const std::string& path) {
    window_ = mac::makeWindow(kEditorTitle, ui::editorClient, Size{320, 360});
    root_ = mac::makeRoot(window_);

    // Header: 16 DIP from the top, heading and the service's configPath (UX06).
    PVStackView* header = mac::makeStack(true, ui::spacing,
                                         NSEdgeInsetsMake(ui::compactMargin, ui::contentMargin, 0, ui::contentMargin));
    [header addView:mac::makeLabel(kEditorHeading, ui::Font::heading)];
    NSTextField* pathLabel = mac::makeLabel(path, ui::Font::subtitle, ui::Color::secondaryText);
    pathLabel.selectable = YES;
    pathLabel.lineBreakMode = NSLineBreakByTruncatingMiddle;
    pathLabel.toolTip = mac::ns(path);
    pathLabel.refusesFirstResponder = YES;
    [header addView:pathLabel];
    [root_ addView:header];

    // One large multiline editor with both scrollbars and no word wrap.
    PVStackView* frame = mac::makeStack(true, 0,
                                        NSEdgeInsetsMake(ui::relatedGap, ui::contentMargin, ui::compactMargin,
                                                         ui::contentMargin));
    const auto editor = mac::makeTextScroll(true, true, "Configuration JSON");
    view_ = editor.text;
    mac::setTextChanged(view_, [this] {
        if (changingContent_) return;
        modified_ = true;
        state_.draftChanged();
    });
    [frame addView:editor.scroll item:mac::withMinHeight(mac::flexibleItem(), 80)];
    [root_ addView:frame item:mac::flexibleItem()];
    [root_ addView:mac::makeSeparator() item:mac::fixedHeight(1)];

    // Footer geometry of the Wizard: 16 DIP margin, 8 DIP gaps, 36 DIP buttons.
    // Reset leading; Cancel and the blue Save trailing. Tab: editor, Reset, Cancel, Save.
    auto footer = mac::makeFooter();
    reset_ = mac::makeButton("Reset", mac::ButtonRole::destructive, [this] { deferred_.post([this] { reset(); }); });
    cancel_ = mac::makeButton("Cancel", mac::ButtonRole::secondary,
                              [this] { deferred_.post([this] { requestClose(); }); });
    save_ = mac::makeButton("Save", mac::ButtonRole::primary, [this] { deferred_.post([this] { save(); }); });
    [footer.leading addView:reset_ item:mac::withMinWidth(mac::natural(), ui::buttonMinWidth)];
    [footer.trailing addView:cancel_ item:mac::withMinWidth(mac::natural(), ui::buttonMinWidth)];
    [footer.trailing addView:save_ item:mac::withMinWidth(mac::natural(), ui::buttonMinWidth)];
    [root_ addView:footer.bar];

    // X, Escape and Cancel ask before discarding a dirty or unknown draft.
    [window_ setCloseHandler:[this] { requestClose(); }];
    // Enter in the text inserts a new line; Escape follows Cancel.
    [window_ setKeyHandler:[this](NSEvent* event) {
        if (!mac::isEscape(event)) return false;
        deferred_.post([this] { requestClose(); });
        return true;
    }];
    [window_ setResizeHandler:[this] { [root_ relayout]; }];
    [root_ relayout];
}

void EditorWindow::setContent(const std::string& content) {
    changingContent_ = true;
    state_.draftChanged();
    document_.load(content);
    auto presentation = presentEditorDocument(document_, content);
    mac::setTextViewString(view_, presentation.text);
    secureWipe(presentation.text.data(), presentation.text.size());
    modified_ = false;
    window_.title = mac::ns(presentation.title);
    changingContent_ = false;
}

void EditorWindow::updateEnabled() {
    if (!window_) return;
    const bool editable = state_.fieldSensitive() && state_.fieldEditable();
    view_.editable = editable ? YES : NO;
    view_.backgroundColor = mac::color(editable ? ui::Color::surface : ui::Color::disabledBackground);
    save_.enabled = state_.saveEnabled() ? YES : NO;
    reset_.enabled = state_.resetEnabled() ? YES : NO;
    cancel_.enabled = state_.closeEnabled() ? YES : NO;
    // X and Escape follow Cancel: no close while the save is running (UX06).
    mac::setCloseEnabled(window_, state_.closeEnabled());
}

LifecycleSnapshot EditorWindow::lifecycleSnapshot() const { return state_.snapshot(modified()); }

bool EditorWindow::approvedDiscard(const LifecycleSnapshot& approved) {
    if (!window_ || state_.saving() || state_.ending() || lifecycleSnapshot() != approved) return false;
    destroy();
    return true;
}

void EditorWindow::requestClose() {
    if (!window_ || !state_.closeEnabled() || state_.ending()) return;
    const auto approved = lifecycleSnapshot();
    if ((approved.dirty || approved.unknown) &&
        !mac::confirm(approved.unknown ? kUnknownQuestion : kDiscardQuestion, kEditorDialogTitle))
        return;
    approvedDiscard(approved);
}

void EditorWindow::save() {
    if (!window_ || !state_.saveEnabled()) return;
    // An unpaired surrogate cannot become UTF-8: the shared invalid Unicode message.
    auto text = mac::strictUtf8(view_.string);
    if (!text) {
        mac::showError(kEditorInvalidUnicode, kEditorDialogTitle);
        return;
    }
    auto prepared = editorSaveText(document_, std::move(*text), modified());
    if (!prepared.document) {
        mac::showError(prepared.error, kEditorDialogTitle);
        return;
    }
    const auto now = static_cast<std::uint64_t>(monotonicMs());
    const auto operation = state_.beginSave(now, now + kEditorSaveDeadlineMs);
    if (!operation) {
        secureWipe(prepared.document->data(), prepared.document->size());
        return;
    }
    updateEnabled();
    // The shared command channel owns the request (Windows set-config path); a
    // result of any other operation or window generation is ignored.
    if (!app_.requestConfigSave(*operation, std::move(*prepared.document),
                                [this](const SaveResult& result) { finishSave(result); })) {
        // Refused before any request: the tray has shown the only message (or the
        // session is ending). End the save state without a second message.
        state_.finishSave({*operation, SaveOutcome::notSent, ErrorCode::ipc_unavailable, {}});
        if (!window_) return;
        updateEnabled();
        if (!state_.ending()) focusEditor();
    }
}

void EditorWindow::finishSave(const SaveResult& result) {
    const auto finish = state_.finishSave(result);
    if (finish == EditorState::Finish::ignored) return;
    updateEnabled();
    if (finish == EditorState::Finish::confirmed) {
        modified_ = false;
        updateEnabled();
        // No application-level Save dialog: the tray checks the connection and asks
        // the shared reconnect question, then discards exactly this snapshot.
        app_.requestReconnectAfterSave(this, lifecycleSnapshot());
        return;
    }
    const auto generation = state_.windowGeneration();
    const auto notice = editorSaveNotice(result);
    mac::showMessage(notice.text, kEditorDialogTitle, notice.warning ? mac::DialogIcon::warning : mac::DialogIcon::error);
    if (window_ && generation == state_.windowGeneration() && !state_.ending()) focusEditor();
}

void EditorWindow::reset() {
    if (!window_ || !state_.resetEnabled()) return;
    const auto expected = lifecycleSnapshot();
    if (!mac::confirm(kEditorResetQuestion, kEditorDialogTitle)) return;
    if (!window_ || state_.ending() || lifecycleSnapshot() != expected) return;
    WizardRequest request;
    request.command = WizardCommand::get_default_config;
    const auto generation = state_.windowGeneration();
    if (!resetChannel_.requestWizard(request,
                                     [this, generation](WizardResult& result) { finishReset(generation, result); },
                                     kEditorResetTimeoutMs)) {
        mac::showError(kEditorResetFailed, kEditorDialogTitle);
        return;
    }
    state_.resetStarted();
    updateEnabled();
}

void EditorWindow::finishReset(std::uint64_t windowGeneration, WizardResult& result) {
    struct WipeReply {
        WizardResult& result;
        ~WipeReply() {
            if (!result.reply) return;
            const auto* document = result.reply.value().result.find("document");
            if (document && document->isString()) {
                auto& text = const_cast<std::string&>(document->stringValue());
                secureWipe(text.data(), text.size());
            }
        }
    } wipe{result};
    if (!window_ || windowGeneration != state_.windowGeneration() || !state_.resetting()) return;
    state_.resetFinished();
    updateEnabled();
    auto loaded = editorDefaultFromReply(result.reply);
    if (!loaded.error.empty()) {
        mac::showError(loaded.error + std::string(kEditorPreserved), kEditorDialogTitle);
        return;
    }
    setContent(loaded.document);
    secureWipe(loaded.document.data(), loaded.document.size());
    // The restored default is a change of the draft until Save.
    modified_ = true;
    focusEditor();
}

void EditorWindow::endSession() {
    if (state_.ending()) return;
    // Terminal and non-blocking: revoke the save and the reset, wipe, destroy.
    if (state_.saving()) app_.abandonConfigSave();
    resetChannel_.abandon();
    state_.endSession();
    destroy();
}

void EditorWindow::destroy() {
    resetChannel_.abandon();
    if (!window_) return;
    PVWindow* window = window_;
    window_ = nil;
    if (view_) {
        // The draft (with possible secrets) leaves the native text and its undo first.
        changingContent_ = true;
        mac::setTextViewString(view_, "");
        changingContent_ = false;
    }
    document_.load({});
    modified_ = false;
    if (state_.isOpen()) state_.close();
    mac::closeWindow(window);
    root_ = nil;
    view_ = nil;
    reset_ = nil;
    cancel_ = nil;
    save_ = nil;
}
}  // namespace phpvpn::tray
