#include "http_error_detail_window.h"
#include "gtk_util.h"

#include "style.h"
#include "ui_style.h"
#include "wizard_dialog_text.h"
#include "wizard_style.h"

namespace phpvpn::tray {
namespace {
constexpr Size kDetailClient{1000, 680}, kDetailMinimum{480, 400};
}  // namespace

bool HttpErrorDetailWindow::open(GtkWindow* owner, const HttpDiagnostic& diagnostic) {
    if (window_) { gtk_window_present(GTK_WINDOW(window_)); return true; }
    if (!owner) return false;
    auto pages = httpErrorDetailPages(diagnostic);
    if (!pages) return false;
    wizardStyle::install();
    window_ = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    style::prepareWindow(GTK_WINDOW(window_), "Error detail", kDetailClient);
    gtk_widget_set_size_request(window_, kDetailMinimum.width, kDetailMinimum.height);
    gtk_window_set_transient_for(GTK_WINDOW(window_), owner);
    gtk_window_set_destroy_with_parent(GTK_WINDOW(window_), TRUE);
    g_signal_connect(window_, "destroy", gtkCallback([](GtkWidget*, gpointer data) {
        static_cast<HttpErrorDetailWindow*>(data)->onDestroyed();
    }), this);

    GtkWidget* tabs = gtk_notebook_new();
    gtk_container_set_border_width(GTK_CONTAINER(tabs), 12);
    const char* titles[]{"Request", "Response"};
    for (std::size_t i = 0; i < 2; ++i) {
        GtkWidget* page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
        gtk_container_set_border_width(GTK_CONTAINER(page), 8);
        GtkWidget* note = gtk_label_new((*pages)[i].note.c_str());
        gtk_label_set_xalign(GTK_LABEL(note), 0.0f);
        gtk_label_set_line_wrap(GTK_LABEL(note), TRUE);
        gtk_label_set_line_wrap_mode(GTK_LABEL(note), PANGO_WRAP_WORD_CHAR);
        gtk_label_set_selectable(GTK_LABEL(note), TRUE);
        style::addClass(note, style::kLabel);
        gtk_box_pack_start(GTK_BOX(page), note, FALSE, FALSE, 0);
        GtkWidget* scroller = gtk_scrolled_window_new(nullptr, nullptr);
        gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroller), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
        gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(scroller), GTK_SHADOW_IN);
        GtkWidget* view = gtk_text_view_new();
        // Read-only but selectable and copyable; the content is never interpreted.
        gtk_text_view_set_editable(GTK_TEXT_VIEW(view), FALSE);
        gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(view), TRUE);
        gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(view), GTK_WRAP_NONE);
        gtk_text_view_set_monospace(GTK_TEXT_VIEW(view), TRUE);
        style::addClass(view, style::kMono);
        style::addClass(view, wizardStyle::kRawText);
        buffers_[i] = gtk_text_view_get_buffer(GTK_TEXT_VIEW(view));
        const auto& text = (*pages)[i].text;
        gtk_text_buffer_set_text(buffers_[i], text.c_str(), static_cast<gint>(text.size()));
        atk_object_set_name(gtk_widget_get_accessible(view), titles[i]);
        gtk_container_add(GTK_CONTAINER(scroller), view);
        gtk_widget_set_vexpand(scroller, TRUE);
        gtk_box_pack_start(GTK_BOX(page), scroller, TRUE, TRUE, 0);
        gtk_notebook_append_page(GTK_NOTEBOOK(tabs), page, gtk_label_new(titles[i]));
    }
    gtk_container_add(GTK_CONTAINER(window_), tabs);
    gtk_widget_show_all(window_);
    gtk_window_present(GTK_WINDOW(window_));
    gtk_widget_grab_focus(tabs);
    return true;
}

void HttpErrorDetailWindow::close() {
    if (!window_) return;
    gtk_widget_destroy(window_);
}

void HttpErrorDetailWindow::onDestroyed() {
    // The raw HTTP text leaves the widgets before they are released.
    for (auto*& buffer : buffers_) {
        if (buffer) gtk_text_buffer_set_text(buffer, "", 0);
        buffer = nullptr;
    }
    window_ = nullptr;
}
}  // namespace phpvpn::tray
