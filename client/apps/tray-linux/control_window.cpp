#include "control_window.h"
#include "gtk_util.h"

#include "style.h"
#include "ui_style.h"

namespace phpvpn::tray {
namespace {
// The height follows the content (natural height), so every button including Exit is
// visible without scrolling; the scroller only helps on screens lower than the window.
constexpr Size kControlClient{420, -1};
constexpr int kApplicationIconDip = 48;
}  // namespace

void ControlWindow::build() {
    window_ = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    style::prepareWindow(GTK_WINDOW(window_), "PHPVPN", kControlClient);
    gtk_window_set_role(GTK_WINDOW(window_), "phpvpn-control");
    // Close hides: the tray, its polling and the indicator keep running.
    g_signal_connect(window_, "delete-event", gtkCallback([](GtkWidget* widget, GdkEvent*, gpointer) -> gboolean {
        gtk_widget_hide(widget);
        return TRUE;
    }), nullptr);
    g_signal_connect(window_, "key-press-event", gtkCallback([](GtkWidget* widget, GdkEventKey* event, gpointer) -> gboolean {
        if (event->keyval != GDK_KEY_Escape) return FALSE;
        gtk_widget_hide(widget);
        return TRUE;
    }), nullptr);
    g_signal_connect(window_, "destroy", gtkCallback([](GtkWidget*, gpointer data) {
        auto* self = static_cast<ControlWindow*>(data);
        self->window_ = self->status_ = self->toggle_ = nullptr;
    }), this);

    GtkWidget* scroller = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroller), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_propagate_natural_height(GTK_SCROLLED_WINDOW(scroller), TRUE);
    gtk_container_add(GTK_CONTAINER(window_), scroller);
    GtkWidget* content = gtk_box_new(GTK_ORIENTATION_VERTICAL, ui::fieldGap);
    gtk_widget_set_margin_top(content, ui::compactMargin);
    gtk_widget_set_margin_bottom(content, ui::compactMargin);
    gtk_widget_set_margin_start(content, ui::compactMargin);
    gtk_widget_set_margin_end(content, ui::compactMargin);
    gtk_container_add(GTK_CONTAINER(scroller), content);

    // The PHPVPN application icon (default blue) from the icon theme, not a state symbol:
    // the state is the status text next to it.
    GtkWidget* card = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, ui::relatedGap);
    style::addClass(card, style::kCard);
    style::addClass(card, style::kCompactCard);
    GtkWidget* icon = gtk_image_new_from_icon_name(std::string(kApplicationIconName).c_str(), GTK_ICON_SIZE_DIALOG);
    gtk_image_set_pixel_size(GTK_IMAGE(icon), kApplicationIconDip);
    gtk_widget_set_valign(icon, GTK_ALIGN_CENTER);
    gtk_box_pack_start(GTK_BOX(card), icon, FALSE, FALSE, 0);
    status_ = gtk_label_new("");
    style::addClass(status_, style::kCardTitle);
    gtk_label_set_xalign(GTK_LABEL(status_), 0.0f);
    gtk_label_set_line_wrap(GTK_LABEL(status_), TRUE);
    gtk_label_set_selectable(GTK_LABEL(status_), FALSE);
    gtk_box_pack_start(GTK_BOX(card), status_, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(content), card, FALSE, FALSE, 0);

    for (const auto& entry : controlWindowActions(view_)) {
        if (entry.kind == IndicatorMenuEntry::Kind::separator) {
            GtkWidget* separator = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
            gtk_widget_set_margin_top(separator, ui::spacing / 2);
            gtk_widget_set_margin_bottom(separator, ui::spacing / 2);
            gtk_box_pack_start(GTK_BOX(content), separator, FALSE, FALSE, 0);
            continue;
        }
        GtkWidget* button = gtk_button_new_with_label(entry.label.c_str());
        style::addClass(button, entry.action == MenuAction::toggle ? style::kPrimaryButton : style::kSecondaryButton);
        if (entry.action == MenuAction::toggle) toggle_ = button;
        auto* binding = new Binding{this, entry.action};
        bindings_.push_back(binding);
        g_signal_connect(button, "clicked", gtkCallback([](GtkButton*, gpointer data) {
            auto* bound = static_cast<Binding*>(data);
            auto* owner = bound->owner;
            const auto action = bound->action;
            owner->deferred_.post([owner, action] { if (owner->onAction_) owner->onAction_(action); });
        }), binding);
        gtk_box_pack_start(GTK_BOX(content), button, FALSE, FALSE, 0);
    }
    gtk_widget_show_all(scroller);
}

void ControlWindow::show(const IndicatorView& view) {
    if (!window_) build();
    update(view);
    gtk_window_present(GTK_WINDOW(window_));
}

void ControlWindow::update(const IndicatorView& view) {
    view_ = view;
    if (!window_) return;
    gtk_label_set_text(GTK_LABEL(status_), view.statusText.c_str());
    gtk_button_set_label(GTK_BUTTON(toggle_), menuLabel(MenuAction::toggle, view.toggleDisconnects).c_str());
}

void ControlWindow::destroy() {
    if (window_) gtk_widget_destroy(window_);
    window_ = nullptr;
    for (auto* binding : bindings_) delete binding;
    bindings_.clear();
}
}  // namespace phpvpn::tray
