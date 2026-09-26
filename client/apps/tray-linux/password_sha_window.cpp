#include "password_sha_window.h"
#include "gtk_util.h"

#include <cstring>

#include "core/base/secure_buffer.h"
#include "core/base/utf8.h"
#include "editor_window.h"
#include "style.h"
#include "ui_style.h"

namespace phpvpn::tray {
namespace {
struct EditPreflight {
    std::size_t bound;
    ContractsEditCheck check;
    const bool* bypass;
};
void wipe(std::u16string& text) { secureWipe(text.data(), text.size() * sizeof(char16_t)); }

GtkWidget* label(std::string_view text, const char* cssClass, bool wrap = false) {
    GtkWidget* widget = gtk_label_new(std::string(text).c_str());
    gtk_label_set_xalign(GTK_LABEL(widget), 0.0f);
    if (wrap) {
        gtk_label_set_line_wrap(GTK_LABEL(widget), TRUE);
        gtk_label_set_line_wrap_mode(GTK_LABEL(widget), PANGO_WRAP_WORD_CHAR);
    }
    if (cssClass) style::addClass(widget, cssClass);
    return widget;
}
std::string entryText(GtkWidget* entry) { return gtk_entry_get_text(GTK_ENTRY(entry)); }
}  // namespace

void attachContractsEdit(GtkWidget* entry, std::size_t bound, ContractsEditCheck check, const bool* bypass) {
    auto* data = new EditPreflight{bound, std::move(check), bypass};
    g_object_set_data_full(G_OBJECT(entry), "phpvpn-contracts-edit", data,
                           [](gpointer raw) { delete static_cast<EditPreflight*>(raw); });
    g_signal_connect(entry, "insert-text", gtkCallback([](GtkEditable* editable, gchar* text, gint length, gint* position,
                                                          gpointer raw) {
        auto* self = static_cast<EditPreflight*>(raw);
        if (self->bypass && *self->bypass) return;
        if (!gtk_editable_get_editable(editable)) {
            g_signal_stop_emission_by_name(editable, "insert-text");
            return;
        }
        const std::string_view inserted(text, length < 0 ? std::strlen(text) : static_cast<std::size_t>(length));
        const std::string_view current(gtk_entry_get_text(GTK_ENTRY(editable)));
        auto before = contractsUtf16(current), insertion = contractsUtf16(inserted);
        const auto at = contractsUtf16Offset(current, position ? static_cast<std::size_t>(*position) : current.size());
        const bool accepted = (inserted.empty() || !insertion.empty()) &&
                              contractsEditAccept(before, at, at, insertion, false, self->bound, self->check);
        wipe(before);
        wipe(insertion);
        if (!accepted) g_signal_stop_emission_by_name(editable, "insert-text");
    }), data);
    // A paste replaces the selection only when the whole result is accepted.
    g_signal_connect(entry, "paste-clipboard", gtkCallback([](GtkEntry* field, gpointer raw) {
        auto* self = static_cast<EditPreflight*>(raw);
        if (self->bypass && *self->bypass) return;
        g_signal_stop_emission_by_name(field, "paste-clipboard");
        auto* editable = GTK_EDITABLE(field);
        if (!gtk_editable_get_editable(editable)) return;
        gchar* pasted = gtk_clipboard_wait_for_text(gtk_widget_get_clipboard(GTK_WIDGET(field), GDK_SELECTION_CLIPBOARD));
        if (!pasted) return;
        const std::string_view inserted(pasted);
        const std::string_view current(gtk_entry_get_text(field));
        gint first = 0, last = 0;
        if (!gtk_editable_get_selection_bounds(editable, &first, &last)) first = last = gtk_editable_get_position(editable);
        auto before = contractsUtf16(current), insertion = contractsUtf16(inserted);
        const bool accepted = (inserted.empty() || !insertion.empty()) &&
            contractsEditAccept(before, contractsUtf16Offset(current, static_cast<std::size_t>(first)),
                                contractsUtf16Offset(current, static_cast<std::size_t>(last)), insertion, false,
                                self->bound, self->check);
        wipe(before);
        wipe(insertion);
        if (accepted) {
            gtk_editable_delete_selection(editable);
            gint position = gtk_editable_get_position(editable);
            gtk_editable_insert_text(editable, pasted, -1, &position);
            gtk_editable_set_position(editable, position);
        }
        secureWipe(pasted, std::strlen(pasted));
        g_free(pasted);
    }), data);
}

PasswordShaWindow::~PasswordShaWindow() { dismiss(); }

bool PasswordShaWindow::open(GtkWindow* parent) {
    if (window_) {
        gtk_window_present(GTK_WINDOW(window_));
        return true;
    }
    config_style::install();
    window_ = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    const auto title = target_.passwordTitle();
    style::prepareWindow(GTK_WINDOW(window_), title.c_str(), ui::passwordClient);
    gtk_window_set_role(GTK_WINDOW(window_), "phpvpn-password-sha");
    gtk_widget_set_size_request(window_, 480, 240);
    // Above the available parent, but never modal: the parent stays usable (UX11).
    if (parent && gtk_widget_get_visible(GTK_WIDGET(parent))) gtk_window_set_transient_for(GTK_WINDOW(window_), parent);
    gtk_window_set_modal(GTK_WINDOW(window_), FALSE);
    gtk_window_set_skip_taskbar_hint(GTK_WINDOW(window_), FALSE);
    g_signal_connect(window_, "delete-event", gtkCallback([](GtkWidget*, GdkEvent*, gpointer data) -> gboolean {
        static_cast<PasswordShaWindow*>(data)->close();
        return TRUE;
    }), this);
    g_signal_connect(window_, "key-press-event", gtkCallback([](GtkWidget*, GdkEventKey* event, gpointer data) -> gboolean {
        if (event->keyval != GDK_KEY_Escape) return FALSE;
        static_cast<PasswordShaWindow*>(data)->close();
        return TRUE;
    }), this);
    g_signal_connect(window_, "destroy", gtkCallback([](GtkWidget*, gpointer data) {
        static_cast<PasswordShaWindow*>(data)->onDestroyed();
    }), this);

    GtkWidget* root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(GTK_CONTAINER(window_), root);
    GtkWidget* scroller = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroller), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_vexpand(scroller, TRUE);
    gtk_box_pack_start(GTK_BOX(root), scroller, TRUE, TRUE, 0);
    GtkWidget* body = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_margin_top(body, ui::cardPadding);
    gtk_widget_set_margin_bottom(body, ui::fieldGap);
    gtk_widget_set_margin_start(body, ui::cardPadding);
    gtk_widget_set_margin_end(body, ui::cardPadding);
    gtk_container_add(GTK_CONTAINER(scroller), body);

    GtkWidget* heading = label(kPasswordHeading, style::kCardTitle);
    gtk_widget_set_margin_bottom(heading, ui::fieldGap);
    gtk_box_pack_start(GTK_BOX(body), heading, FALSE, FALSE, 0);
    GtkWidget* description = label(kPasswordDescription, style::kLabel, true);
    gtk_widget_set_margin_bottom(description, ui::relatedGap);
    gtk_box_pack_start(GTK_BOX(body), description, FALSE, FALSE, 0);
    GtkWidget* passwordLabel = label(kPasswordLabel, style::kLabel);
    gtk_widget_set_margin_bottom(passwordLabel, ui::fieldGap);
    gtk_box_pack_start(GTK_BOX(body), passwordLabel, FALSE, FALSE, 0);
    input_ = gtk_entry_new();
    // The password is visible as in the mockup; the cue text is never hashed.
    gtk_entry_set_placeholder_text(GTK_ENTRY(input_), std::string(kPasswordCue).c_str());
    style::addClass(input_, style::kValue);
    gtk_label_set_mnemonic_widget(GTK_LABEL(passwordLabel), input_);
    atk_object_set_name(gtk_widget_get_accessible(input_), std::string(kPasswordLabel).c_str());
    gtk_box_pack_start(GTK_BOX(body), input_, FALSE, FALSE, 0);
    GtkWidget* hint = label(kPasswordHint, style::kHint, true);
    gtk_widget_set_margin_top(hint, ui::fieldGap);
    gtk_widget_set_margin_bottom(hint, ui::relatedGap);
    gtk_box_pack_start(GTK_BOX(body), hint, FALSE, FALSE, 0);
    GtkWidget* keyLabel = label(kPasswordKeyLabel, style::kLabel);
    gtk_widget_set_margin_bottom(keyLabel, ui::fieldGap);
    gtk_box_pack_start(GTK_BOX(body), keyLabel, FALSE, FALSE, 0);
    hash_ = gtk_entry_new();
    // Read-only but focusable and selectable; the long hash scrolls horizontally.
    gtk_editable_set_editable(GTK_EDITABLE(hash_), FALSE);
    style::addClass(hash_, style::kMono);
    style::addClass(hash_, style::kReadOnly);
    gtk_label_set_mnemonic_widget(GTK_LABEL(keyLabel), hash_);
    atk_object_set_name(gtk_widget_get_accessible(hash_), std::string(kPasswordKeyLabel).c_str());
    gtk_box_pack_start(GTK_BOX(body), hash_, FALSE, FALSE, 0);
    notice_ = label("", style::kLabel, true);
    gtk_widget_set_margin_top(notice_, ui::spacing);
    gtk_box_pack_start(GTK_BOX(body), notice_, FALSE, FALSE, 0);

    GtkWidget* separator = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    style::addClass(separator, style::kSeparator);
    gtk_box_pack_start(GTK_BOX(root), separator, FALSE, FALSE, 0);
    GtkWidget* footer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, ui::fieldGap);
    gtk_widget_set_margin_top(footer, ui::compactMargin);
    gtk_widget_set_margin_bottom(footer, ui::compactMargin);
    gtk_widget_set_margin_start(footer, ui::compactMargin);
    gtk_widget_set_margin_end(footer, ui::compactMargin);
    GtkWidget* useDefault = gtk_button_new_with_label("Default");
    GtkWidget* random = gtk_button_new_with_label("Random");
    ok_ = gtk_button_new_with_label("OK");
    style::addClass(useDefault, config_style::kResetButton);
    style::addClass(random, config_style::kResetButton);
    style::addClass(ok_, style::kPrimaryButton);
    gtk_box_pack_start(GTK_BOX(footer), useDefault, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(footer), random, FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX(footer), ok_, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(root), footer, FALSE, FALSE, 0);

    attachContractsEdit(input_, kPasswordBound, [this](std::u16string_view text) { return update(text); }, &bypass_);
    g_signal_connect(input_, "changed", gtkCallback([](GtkEditable*, gpointer data) {
        auto* self = static_cast<PasswordShaWindow*>(data);
        if (self->bypass_) return;
        auto text = contractsUtf16(entryText(self->input_));
        self->update(text);
        wipe(text);
    }), this);
    // Enter confirms the current valid hash unless a footer button has the focus.
    for (auto* field : {input_, hash_})
        g_signal_connect(field, "activate", gtkCallback([](GtkEntry*, gpointer data) {
            static_cast<PasswordShaWindow*>(data)->confirm();
        }), this);
    g_signal_connect(useDefault, "clicked", gtkCallback([](GtkButton*, gpointer data) {
        static_cast<PasswordShaWindow*>(data)->usePairDefault();
    }), this);
    g_signal_connect(random, "clicked", gtkCallback([](GtkButton*, gpointer data) {
        static_cast<PasswordShaWindow*>(data)->randomPassword();
    }), this);
    g_signal_connect(ok_, "clicked", gtkCallback([](GtkButton*, gpointer data) {
        static_cast<PasswordShaWindow*>(data)->confirm();
    }), this);

    update(u"");
    gtk_widget_show_all(window_);
    gtk_window_present(GTK_WINDOW(window_));
    gtk_widget_grab_focus(input_);
    return true;
}

bool PasswordShaWindow::update(std::u16string_view text) {
    const auto error = model_.update(crypto_, text);
    if (hash_) {
        bypass_ = true;
        const std::string key(model_.key());
        gtk_entry_set_text(GTK_ENTRY(hash_), key.c_str());
        bypass_ = false;
    }
    refreshStatus();
    return error != PasswordKeyError::tooLong && error != PasswordKeyError::unsupportedControl;
}

void PasswordShaWindow::setTargetEnabled(bool enabled) {
    if (targetEnabled_ == enabled) return;
    targetEnabled_ = enabled;
    if (window_) refreshStatus();
}

void PasswordShaWindow::refreshStatus() {
    if (!window_) return;
    gtk_label_set_text(GTK_LABEL(notice_), std::string(passwordShaNotice(model_.error(), targetEnabled_)).c_str());
    gtk_widget_set_sensitive(ok_, targetEnabled_ && model_.valid());
}

void PasswordShaWindow::setInput(std::u16string_view text) {
    auto converted = utf16ToUtf8(text);
    if (!converted) return;
    bypass_ = true;
    gtk_entry_set_text(GTK_ENTRY(input_), converted.value().c_str());
    bypass_ = false;
    secureWipe(converted.value().data(), converted.value().size());
    update(text);
}

void PasswordShaWindow::randomPassword() {
    std::u16string password;
    if (!contractsRandomPassword(crypto_, password)) {
        gtk_label_set_text(GTK_LABEL(notice_), std::string(kPasswordRandomFailed).c_str());
        return;
    }
    setInput(password);
    wipe(password);
}

void PasswordShaWindow::usePairDefault() {
    // The captured numeric pair, with no Wizard candidate-range restriction.
    auto password = contractsUtf16(target_.contractId());
    setInput(password);
    wipe(password);
}

void PasswordShaWindow::confirm() {
    if (!window_ || !targetEnabled_ || !model_.valid()) return;
    if (apply_ && apply_(target_, model_.key())) close();
}

void PasswordShaWindow::close() {
    if (!window_) return;
    gtk_widget_destroy(window_);
    if (closed_) closed_(target_);
}

void PasswordShaWindow::dismiss() {
    closed_ = {};
    if (window_) gtk_widget_destroy(window_);
}

void PasswordShaWindow::onDestroyed() {
    bypass_ = true;
    if (input_) gtk_entry_set_text(GTK_ENTRY(input_), "");
    if (hash_) gtk_entry_set_text(GTK_ENTRY(hash_), "");
    bypass_ = false;
    model_.clear();
    window_ = input_ = hash_ = notice_ = ok_ = nullptr;
}
}  // namespace phpvpn::tray
