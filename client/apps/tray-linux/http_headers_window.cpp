#include "http_headers_window.h"
#include "gtk_util.h"

#include "core/base/secure_buffer.h"
#include "dialogs.h"
#include "style.h"
#include "ui_style.h"
#include "wizard_dialog_text.h"
#include "wizard_style.h"

namespace phpvpn::tray {
namespace {
// tray_UX.md: default client area 760 x 420 DIP, minimum 480 x 260 DIP.
constexpr Size kHeadersClient{760, 420}, kHeadersMinimum{480, 260};
constexpr int kReset = 1, kCancel = 2, kOk = 3;

std::string bufferText(GtkTextBuffer* buffer) {
    GtkTextIter start, end;
    gtk_text_buffer_get_bounds(buffer, &start, &end);
    gchar* raw = gtk_text_buffer_get_text(buffer, &start, &end, FALSE);
    std::string text = raw ? raw : "";
    if (raw) { secureWipe(raw, std::char_traits<char>::length(raw)); g_free(raw); }
    return text;
}
void setBufferText(GtkWidget* view, const std::string& text) {
    gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(view)), text.c_str(), static_cast<gint>(text.size()));
}
}  // namespace

bool HttpHeadersWindow::open(GtkWindow* owner, const std::string& value, std::function<Status(std::string)> accept,
                             std::function<void()> closed) {
    if (window_) { gtk_window_present(GTK_WINDOW(window_)); return true; }
    if (!owner) return false;
    const auto display = httpHeadersDisplayText(value);
    if (!display) return false;
    wizardStyle::install();
    accept_ = std::move(accept);
    closed_ = std::move(closed);
    window_ = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    style::prepareWindow(GTK_WINDOW(window_), "HTTP headers", kHeadersClient);
    gtk_widget_set_size_request(window_, kHeadersMinimum.width, kHeadersMinimum.height);
    gtk_window_set_transient_for(GTK_WINDOW(window_), owner);
    gtk_window_set_modal(GTK_WINDOW(window_), TRUE);
    gtk_window_set_destroy_with_parent(GTK_WINDOW(window_), TRUE);
    gtk_window_set_type_hint(GTK_WINDOW(window_), GDK_WINDOW_TYPE_HINT_DIALOG);
    g_signal_connect(window_, "destroy", gtkCallback([](GtkWidget*, gpointer data) {
        static_cast<HttpHeadersWindow*>(data)->onDestroyed();
    }), this);
    // Escape, the title bar X and Alt+F4 cancel the local edit.
    g_signal_connect(window_, "delete-event", gtkCallback([](GtkWidget*, GdkEvent*, gpointer data) -> gboolean {
        static_cast<HttpHeadersWindow*>(data)->close(true);
        return TRUE;
    }), this);
    g_signal_connect(window_, "key-press-event", gtkCallback([](GtkWidget*, GdkEventKey* event, gpointer data) -> gboolean {
        if (event->keyval != GDK_KEY_Escape) return FALSE;
        static_cast<HttpHeadersWindow*>(data)->close(true);
        return TRUE;
    }), this);

    GtkWidget* root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_container_set_border_width(GTK_CONTAINER(root), ui::cardPadding);
    GtkWidget* scroller = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroller), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(scroller), GTK_SHADOW_IN);
    field_ = gtk_text_view_new();
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(field_), GTK_WRAP_NONE);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(field_), TRUE);
    // Tab and Shift+Tab move between the field and the buttons; Enter inserts a line.
    gtk_text_view_set_accepts_tab(GTK_TEXT_VIEW(field_), FALSE);
    gtk_text_view_set_left_margin(GTK_TEXT_VIEW(field_), 6);
    gtk_text_view_set_top_margin(GTK_TEXT_VIEW(field_), 6);
    style::addClass(field_, style::kMono);
    atk_object_set_name(gtk_widget_get_accessible(field_), "HTTP headers");
    setBufferText(field_, *display);
    gtk_container_add(GTK_CONTAINER(scroller), field_);
    gtk_box_pack_start(GTK_BOX(root), scroller, TRUE, TRUE, 0);

    GtkWidget* footer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, ui::fieldGap);
    struct Spec { const char* label; int id; const char* cssClass; bool end; };
    for (const auto& spec : {Spec{"Reset", kReset, style::kErrorButton, false}, Spec{"OK", kOk, style::kPrimaryButton, true},
                             Spec{"Cancel", kCancel, style::kSecondaryButton, true}}) {
        GtkWidget* button = gtk_button_new_with_label(spec.label);
        style::addClass(button, spec.cssClass);
        g_object_set_data(G_OBJECT(button), "phpvpn-command", GINT_TO_POINTER(spec.id));
        g_signal_connect(button, "clicked", gtkCallback([](GtkButton* self, gpointer data) {
            auto* window = static_cast<HttpHeadersWindow*>(data);
            const int id = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(self), "phpvpn-command"));
            window->deferred_.post([window, id] { window->command(id); });
        }), this);
        if (spec.end) gtk_box_pack_end(GTK_BOX(footer), button, FALSE, FALSE, 0);
        else gtk_box_pack_start(GTK_BOX(footer), button, FALSE, FALSE, 0);
        if (spec.id == kOk) {
            gtk_widget_set_can_default(button, TRUE);
            gtk_window_set_default(GTK_WINDOW(window_), button);
        }
    }
    gtk_box_pack_start(GTK_BOX(root), footer, FALSE, FALSE, 0);
    gtk_container_add(GTK_CONTAINER(window_), root);
    gtk_widget_show_all(window_);
    gtk_window_present(GTK_WINDOW(window_));
    gtk_widget_grab_focus(field_);
    return true;
}

void HttpHeadersWindow::close(bool notify) {
    if (!window_) return;
    auto closed = notify ? std::move(closed_) : std::function<void()>{};
    closed_ = {};
    accept_ = {};
    gtk_widget_destroy(window_);
    if (closed) closed();
}

void HttpHeadersWindow::onDestroyed() {
    if (field_) setBufferText(field_, {});
    field_ = nullptr;
    window_ = nullptr;
    accept_ = {};
    closed_ = {};
}

void HttpHeadersWindow::command(int id) {
    if (!window_) return;
    if (id == kReset) { setBufferText(field_, defaultHttpHeadersDisplayText()); gtk_widget_grab_focus(field_); return; }
    if (id == kCancel) { close(); return; }
    if (id != kOk || !accept_) return;
    auto text = bufferText(gtk_text_view_get_buffer(GTK_TEXT_VIEW(field_)));
    // The error keeps the dialog and its text open.
    const Status result = accept_(std::move(text));
    if (result) { close(); return; }
    showError(GTK_WINDOW(window_), httpHeadersErrorText(result), "HTTP headers");
    if (field_) gtk_widget_grab_focus(field_);
}
}  // namespace phpvpn::tray
