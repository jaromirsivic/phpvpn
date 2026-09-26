#include "editor_window.h"
#include "gtk_util.h"

#include <cstring>

#include "core/base/secure_buffer.h"
#include "dialogs.h"
#include "style.h"
#include "tray_app.h"
#include "ui_style.h"

namespace phpvpn::tray {
namespace config_style {
void install() {
    static bool installed = false;
    if (installed) return;
    style::install();
    GdkScreen* screen = gdk_screen_get_default();
    if (!screen) return;
    using style::hex;
    using ui::Color;
    std::string css;
    auto rule = [&css](const std::string& text) { css += text; css += '\n'; };
    // Reset role of the Win32 modern button: red fill, white text (ui_controls.cpp).
    rule(std::string("button.") + kResetButton + " { min-height: " + std::to_string(ui::buttonHeight) +
         "px; min-width: " + std::to_string(ui::buttonMinWidth) + "px; padding: 0 " + std::to_string(ui::buttonPadding) +
         "px; border-radius: 4px; background-image: none; box-shadow: none; text-shadow: none; color: " +
         hex(Color::primaryText) + "; background-color: " + hex(Color::buttonReset) + "; border: 1px solid " +
         hex(Color::buttonReset) + "; }");
    rule(std::string("button.") + kResetButton + " label { color: " + hex(Color::primaryText) + "; }");
    rule(std::string("button.") + kResetButton + ":hover { background-color: " + hex(Color::buttonResetHover) + "; }");
    rule(std::string("button.") + kResetButton + ":active { background-color: " + hex(Color::buttonResetPressed) + "; }");
    rule(std::string("button.") + kResetButton + ":disabled { background-color: " + hex(Color::buttonResetDisabled) +
         "; border-color: " + hex(Color::buttonResetDisabled) + "; }");
    rule(std::string("button.") + kResetButton + ":disabled label { color: " + hex(Color::disabledText) + "; }");
    rule(std::string(".") + kEditorFrame + " { background-color: " + hex(Color::surface) + "; border: 1px solid " +
         hex(Color::border) + "; border-radius: 12px; padding: 6px; }");
    rule(std::string(".") + kEditorFrame + " textview text { background-color: " + hex(Color::surface) + "; color: " +
         hex(Color::text) + "; }");
    rule(std::string(".") + kEditorFrame + ":disabled textview text { background-color: " + hex(Color::disabledBackground) + "; }");
    rule(std::string("entry.") + kInvalid + " { border: 1px solid " + hex(Color::error) + "; background-color: " +
         hex(Color::errorBackground) + "; }");
    rule(std::string(".") + kTable + " { background-color: " + hex(Color::surface) + "; border: 1px solid " +
         hex(Color::border) + "; border-radius: 8px; }");
    rule(std::string(".") + kTableHeader + " { background-color: " + hex(Color::contractsHeader) + "; min-height: " +
         std::to_string(ui::headerHeight) + "px; }");
    rule(std::string(".") + kTableHeader + " label { color: " + hex(Color::primaryText) + "; font-size: 16px; font-weight: 700; }");
    rule(std::string(".") + kTableRow + " { background-color: " + hex(Color::surface) + "; min-height: " +
         std::to_string(ui::rowHeight) + "px; border-bottom: 1px solid " + hex(Color::separator) + "; }");
    rule(std::string(".") + kTableRow + "." + kChanged + ", entry." + kChanged + " { background-color: " +
         hex(Color::contractsChanged) + "; }");
    rule(std::string(".") + style::kWindow + " label." + kValidation + " { color: " + hex(Color::error) +
         "; font-weight: 600; }");
    GtkCssProvider* provider = gtk_css_provider_new();
    gtk_css_provider_load_from_data(provider, css.c_str(), static_cast<gssize>(css.size()), nullptr);
    gtk_style_context_add_provider_for_screen(screen, GTK_STYLE_PROVIDER(provider),
                                              GTK_STYLE_PROVIDER_PRIORITY_APPLICATION + 1);
    g_object_unref(provider);
    installed = true;
}
}  // namespace config_style

namespace {
GtkWidget* footerButton(const char* text, const char* cssClass) {
    GtkWidget* button = gtk_button_new_with_label(text);
    style::addClass(button, cssClass);
    return button;
}
}  // namespace

EditorWindow::EditorWindow(TrayApp& app) : app_(app), resetChannel_(app.endpoint(), app.dispatcher()) {}

EditorWindow::~EditorWindow() {
    // Owned by the TrayApp hooks: the application has already ended the session.
    resetChannel_.abandon();
    if (window_) gtk_widget_destroy(window_);
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
    gtk_window_present(GTK_WINDOW(window_));
    return true;
}

bool EditorWindow::open(const std::string& document, const std::string& path) {
    if (state_.ending()) return false;
    if (window_) gtk_widget_destroy(window_);
    state_.open();
    build(path);
    setContent(document);
    updateEnabled();
    gtk_widget_show_all(window_);
    gtk_window_present(GTK_WINDOW(window_));
    // Focus starts in the editor (UX06).
    gtk_widget_grab_focus(view_);
    return true;
}

void EditorWindow::build(const std::string& path) {
    config_style::install();
    window_ = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    style::prepareWindow(GTK_WINDOW(window_), std::string(kEditorTitle).c_str(), ui::editorClient);
    gtk_window_set_role(GTK_WINDOW(window_), "phpvpn-edit-config");
    gtk_widget_set_size_request(window_, 320, 360);
    g_signal_connect(window_, "delete-event", gtkCallback([](GtkWidget*, GdkEvent*, gpointer data) -> gboolean {
        auto* self = static_cast<EditorWindow*>(data);
        self->deferred_.post([self] { self->requestClose(); });
        return TRUE;
    }), this);
    g_signal_connect(window_, "key-press-event", gtkCallback([](GtkWidget*, GdkEventKey* event, gpointer data) -> gboolean {
        if (event->keyval != GDK_KEY_Escape) return FALSE;
        auto* self = static_cast<EditorWindow*>(data);
        self->deferred_.post([self] { self->requestClose(); });
        return TRUE;
    }), this);
    g_signal_connect(window_, "destroy", gtkCallback([](GtkWidget*, gpointer data) {
        static_cast<EditorWindow*>(data)->onDestroyed();
    }), this);

    GtkWidget* root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(GTK_CONTAINER(window_), root);

    // Header: 16 DIP from the top, heading and the service's configPath (UX06).
    GtkWidget* header = gtk_box_new(GTK_ORIENTATION_VERTICAL, ui::spacing);
    gtk_widget_set_margin_top(header, ui::compactMargin);
    gtk_widget_set_margin_start(header, ui::contentMargin);
    gtk_widget_set_margin_end(header, ui::contentMargin);
    GtkWidget* heading = gtk_label_new(std::string(kEditorHeading).c_str());
    gtk_label_set_xalign(GTK_LABEL(heading), 0.0f);
    style::addClass(heading, style::kHeading);
    gtk_box_pack_start(GTK_BOX(header), heading, FALSE, FALSE, 0);
    GtkWidget* pathLabel = gtk_label_new(path.c_str());
    gtk_label_set_xalign(GTK_LABEL(pathLabel), 0.0f);
    gtk_label_set_selectable(GTK_LABEL(pathLabel), TRUE);
    gtk_label_set_ellipsize(GTK_LABEL(pathLabel), PANGO_ELLIPSIZE_MIDDLE);
    gtk_widget_set_can_focus(pathLabel, FALSE);
    gtk_widget_set_tooltip_text(pathLabel, path.c_str());
    style::addClass(pathLabel, style::kEyebrow);
    gtk_box_pack_start(GTK_BOX(header), pathLabel, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(root), header, FALSE, FALSE, 0);

    // One large multiline editor with both scrollbars and no word wrap.
    GtkWidget* frame = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    style::addClass(frame, config_style::kEditorFrame);
    gtk_widget_set_margin_top(frame, ui::relatedGap);
    gtk_widget_set_margin_bottom(frame, ui::compactMargin);
    gtk_widget_set_margin_start(frame, ui::contentMargin);
    gtk_widget_set_margin_end(frame, ui::contentMargin);
    GtkWidget* scroller = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroller), GTK_POLICY_ALWAYS, GTK_POLICY_ALWAYS);
    gtk_scrolled_window_set_overlay_scrolling(GTK_SCROLLED_WINDOW(scroller), FALSE);
    gtk_widget_set_vexpand(scroller, TRUE);
    view_ = gtk_text_view_new();
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(view_), GTK_WRAP_NONE);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(view_), TRUE);
    // Tab moves to Reset, Cancel and Save; Enter inserts a new line (UX06).
    gtk_text_view_set_accepts_tab(GTK_TEXT_VIEW(view_), FALSE);
    gtk_text_view_set_left_margin(GTK_TEXT_VIEW(view_), 4);
    style::addClass(view_, style::kMono);
    atk_object_set_name(gtk_widget_get_accessible(view_), "Configuration JSON");
    buffer_ = gtk_text_view_get_buffer(GTK_TEXT_VIEW(view_));
    g_signal_connect(buffer_, "changed", gtkCallback([](GtkTextBuffer*, gpointer data) {
        auto* self = static_cast<EditorWindow*>(data);
        if (!self->changingContent_) self->state_.draftChanged();
    }), this);
    gtk_container_add(GTK_CONTAINER(scroller), view_);
    gtk_box_pack_start(GTK_BOX(frame), scroller, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(root), frame, TRUE, TRUE, 0);

    GtkWidget* separator = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    style::addClass(separator, style::kSeparator);
    gtk_box_pack_start(GTK_BOX(root), separator, FALSE, FALSE, 0);

    // Footer geometry of the Wizard: 16 DIP margin, 8 DIP gaps, 36 DIP buttons.
    // Reset leading; Cancel and the blue Save trailing.
    GtkWidget* footer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, ui::fieldGap);
    gtk_widget_set_margin_top(footer, ui::compactMargin);
    gtk_widget_set_margin_bottom(footer, ui::compactMargin);
    gtk_widget_set_margin_start(footer, ui::compactMargin);
    gtk_widget_set_margin_end(footer, ui::compactMargin);
    reset_ = footerButton("Reset", config_style::kResetButton);
    cancel_ = footerButton("Cancel", style::kSecondaryButton);
    save_ = footerButton("Save", style::kPrimaryButton);
    gtk_box_pack_start(GTK_BOX(footer), reset_, FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX(footer), save_, FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX(footer), cancel_, FALSE, FALSE, 0);
    // Tab follows the visual order: editor -> Reset -> Cancel -> Save.
    gtk_box_pack_start(GTK_BOX(root), footer, FALSE, FALSE, 0);
    g_signal_connect(reset_, "clicked", gtkCallback([](GtkButton*, gpointer data) {
        auto* self = static_cast<EditorWindow*>(data);
        self->deferred_.post([self] { self->reset(); });
    }), this);
    g_signal_connect(cancel_, "clicked", gtkCallback([](GtkButton*, gpointer data) {
        auto* self = static_cast<EditorWindow*>(data);
        self->deferred_.post([self] { self->requestClose(); });
    }), this);
    g_signal_connect(save_, "clicked", gtkCallback([](GtkButton*, gpointer data) {
        auto* self = static_cast<EditorWindow*>(data);
        self->deferred_.post([self] { self->save(); });
    }), this);
}

void EditorWindow::setContent(const std::string& content) {
    changingContent_ = true;
    state_.draftChanged();
    document_.load(content);
    auto presentation = presentEditorDocument(document_, content);
    gtk_text_buffer_set_text(buffer_, presentation.text.c_str(), static_cast<gint>(presentation.text.size()));
    secureWipe(presentation.text.data(), presentation.text.size());
    gtk_text_buffer_set_modified(buffer_, FALSE);
    GtkTextIter start;
    gtk_text_buffer_get_start_iter(buffer_, &start);
    gtk_text_buffer_place_cursor(buffer_, &start);
    gtk_window_set_title(GTK_WINDOW(window_), presentation.title.c_str());
    changingContent_ = false;
}

bool EditorWindow::modified() const { return buffer_ && gtk_text_buffer_get_modified(buffer_); }

std::string EditorWindow::bufferText() const {
    GtkTextIter start, end;
    gtk_text_buffer_get_bounds(buffer_, &start, &end);
    gchar* raw = gtk_text_buffer_get_text(buffer_, &start, &end, TRUE);
    std::string text = raw ? raw : "";
    if (raw) {
        secureWipe(raw, std::strlen(raw));
        g_free(raw);
    }
    return text;
}

void EditorWindow::updateEnabled() {
    if (!window_) return;
    gtk_widget_set_sensitive(view_, state_.fieldSensitive());
    gtk_text_view_set_editable(GTK_TEXT_VIEW(view_), state_.fieldEditable());
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(view_), state_.fieldEditable());
    gtk_widget_set_sensitive(save_, state_.saveEnabled());
    gtk_widget_set_sensitive(reset_, state_.resetEnabled());
    gtk_widget_set_sensitive(cancel_, state_.closeEnabled());
    // X and Escape follow Cancel: no close while the save is running (UX06).
    gtk_window_set_deletable(GTK_WINDOW(window_), state_.closeEnabled());
}

LifecycleSnapshot EditorWindow::lifecycleSnapshot() const {
    return state_.snapshot(window_ && modified());
}

bool EditorWindow::approvedDiscard(const LifecycleSnapshot& approved) {
    if (!window_ || state_.saving() || state_.ending() || lifecycleSnapshot() != approved) return false;
    gtk_widget_destroy(window_);
    return true;
}

void EditorWindow::requestClose() {
    if (!window_ || !state_.closeEnabled() || state_.ending()) return;
    const auto approved = lifecycleSnapshot();
    if ((approved.dirty || approved.unknown) &&
        !confirm(GTK_WINDOW(window_), approved.unknown ? kUnknownQuestion : kDiscardQuestion, kEditorDialogTitle))
        return;
    approvedDiscard(approved);
}

void EditorWindow::save() {
    if (!window_ || !state_.saveEnabled()) return;
    auto prepared = editorSaveText(document_, bufferText(), modified());
    if (!prepared.document) {
        showError(GTK_WINDOW(window_), prepared.error, kEditorDialogTitle);
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
                                [this](const SaveResult& result) { finishSave(result); }))
        finishSave({*operation, SaveOutcome::notSent, ErrorCode::ipc_unavailable, {}});
}

void EditorWindow::finishSave(const SaveResult& result) {
    const auto finish = state_.finishSave(result);
    if (finish == EditorState::Finish::ignored) return;
    updateEnabled();
    if (finish == EditorState::Finish::confirmed) {
        gtk_text_buffer_set_modified(buffer_, FALSE);
        updateEnabled();
        // No application-level Save dialog: the tray checks the connection and asks
        // the shared reconnect question, then discards exactly this snapshot.
        app_.requestReconnectAfterSave(this, lifecycleSnapshot());
        return;
    }
    const auto generation = state_.windowGeneration();
    const auto notice = editorSaveNotice(result);
    showMessage(GTK_WINDOW(window_), notice.text, kEditorDialogTitle,
                notice.warning ? DialogIcon::warning : DialogIcon::error);
    if (window_ && generation == state_.windowGeneration() && !state_.ending()) gtk_widget_grab_focus(view_);
}

void EditorWindow::reset() {
    if (!window_ || !state_.resetEnabled()) return;
    const auto expected = lifecycleSnapshot();
    if (!confirm(GTK_WINDOW(window_), kEditorResetQuestion, kEditorDialogTitle)) return;
    if (!window_ || state_.ending() || lifecycleSnapshot() != expected) return;
    WizardRequest request;
    request.command = WizardCommand::get_default_config;
    const auto generation = state_.windowGeneration();
    if (!resetChannel_.requestWizard(request, [this, generation](WizardResult& result) { finishReset(generation, result); },
                                     kEditorResetTimeoutMs)) {
        showError(GTK_WINDOW(window_), kEditorResetFailed, kEditorDialogTitle);
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
        showError(GTK_WINDOW(window_), loaded.error + std::string(kEditorPreserved), kEditorDialogTitle);
        return;
    }
    setContent(loaded.document);
    secureWipe(loaded.document.data(), loaded.document.size());
    gtk_text_buffer_set_modified(buffer_, TRUE);
    gtk_widget_grab_focus(view_);
}

void EditorWindow::endSession() {
    if (state_.ending()) return;
    // Terminal and non-blocking: revoke the save and the reset, wipe, destroy.
    if (state_.saving()) app_.abandonConfigSave();
    resetChannel_.abandon();
    state_.endSession();
    if (window_) gtk_widget_destroy(window_);
}

void EditorWindow::onDestroyed() {
    resetChannel_.abandon();
    if (buffer_) {
        changingContent_ = true;
        gtk_text_buffer_set_text(buffer_, "", 0);
        changingContent_ = false;
    }
    document_.load({});
    if (state_.isOpen()) state_.close();
    window_ = view_ = reset_ = cancel_ = save_ = nullptr;
    buffer_ = nullptr;
}
}  // namespace phpvpn::tray
