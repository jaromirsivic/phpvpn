#include "indicator.h"
#include "gtk_util.h"

#include <libayatana-appindicator/app-indicator.h>

#include <memory>

namespace phpvpn::tray {
namespace {
// Menu activations arrive from the dbusmenu method handler. The action runs from
// an idle callback, so a modal dialog never blocks inside that handler.
struct PendingAction {
    std::function<void(MenuAction)>* target;
    std::weak_ptr<int> alive;
    MenuAction action;
};
std::shared_ptr<int>& aliveToken() {
    static std::shared_ptr<int> token = std::make_shared<int>(0);
    return token;
}
}  // namespace

bool Indicator::create(const std::string& iconThemePath, const IndicatorView& initial) {
    if (indicator_) return true;
    // Without a StatusNotifierWatcher ayatana falls back to a GtkStatusIcon, which
    // only exists on X11 (XEmbed). Elsewhere it cannot work and only logs criticals;
    // the tray shows its control window instead (WatcherPresence).
    if (GdkDisplay* display = gdk_display_get_default();
        display && g_strcmp0(G_OBJECT_TYPE_NAME(display), "GdkX11Display") != 0) {
        auto* type = static_cast<AppIndicatorClass*>(g_type_class_ref(APP_INDICATOR_TYPE));
        type->fallback = nullptr;
        type->unfallback = nullptr;
        g_type_class_unref(type);
    }
    // app_indicator_new is deprecated in ayatana 0.6 in favour of a new library;
    // the GObject properties are the stable construction interface of 0.5 and 0.6.
    indicator_ = APP_INDICATOR(g_object_new(APP_INDICATOR_TYPE, "id", "phpvpn", "category", "ApplicationStatus",
                                            "icon-name", initial.iconName.c_str(), nullptr));
    if (!indicator_) return false;
    if (!iconThemePath.empty()) app_indicator_set_icon_theme_path(indicator_, iconThemePath.c_str());

    menu_ = gtk_menu_new();
    g_object_ref_sink(menu_);
    for (const auto& entry : indicatorMenu(initial)) {
        GtkWidget* item = nullptr;
        if (entry.kind == IndicatorMenuEntry::Kind::separator) {
            item = gtk_separator_menu_item_new();
        } else {
            item = gtk_menu_item_new_with_label(entry.label.c_str());
            gtk_widget_set_sensitive(item, entry.sensitive);
            if (entry.kind == IndicatorMenuEntry::Kind::status) statusItem_ = item;
            if (entry.action == MenuAction::toggle) toggleItem_ = item;
            if (entry.kind == IndicatorMenuEntry::Kind::action) {
                auto* binding = new ItemBinding{this, entry.action};
                bindings_.push_back(binding);
                g_signal_connect(item, "activate", gtkCallback([](GtkMenuItem*, gpointer data) {
                    auto* bound = static_cast<ItemBinding*>(data);
                    auto* pending = new PendingAction{&bound->owner->onAction_, aliveToken(), bound->action};
                    g_idle_add_full(G_PRIORITY_DEFAULT_IDLE, [](gpointer raw) -> gboolean {
                        auto* action = static_cast<PendingAction*>(raw);
                        if (!action->alive.expired() && *action->target) (*action->target)(action->action);
                        return G_SOURCE_REMOVE;
                    }, pending, [](gpointer raw) { delete static_cast<PendingAction*>(raw); });
                }), binding);
            }
        }
        gtk_menu_shell_append(GTK_MENU_SHELL(menu_), item);
    }
    gtk_widget_show_all(menu_);
    app_indicator_set_menu(indicator_, GTK_MENU(menu_));
    publish(initial, true);
    app_indicator_set_status(indicator_, APP_INDICATOR_STATUS_ACTIVE);
    return true;
}

void Indicator::update(const IndicatorView& view) { publish(view, false); }

void Indicator::publish(const IndicatorView& view, bool force) {
    if (!indicator_) return;
    if (force) gate_.reset();
    if (!gate_.changed(view)) return;
    if (force || view.iconName != published_.iconName || view.statusText != published_.statusText)
        app_indicator_set_icon_full(indicator_, view.iconName.c_str(), view.statusText.c_str());
    if (force || view.statusText != published_.statusText) {
        app_indicator_set_title(indicator_, view.statusText.c_str());
        if (statusItem_) gtk_menu_item_set_label(GTK_MENU_ITEM(statusItem_), view.statusText.c_str());
    }
    if ((force || view.toggleDisconnects != published_.toggleDisconnects) && toggleItem_)
        gtk_menu_item_set_label(GTK_MENU_ITEM(toggleItem_), menuLabel(MenuAction::toggle, view.toggleDisconnects).c_str());
    published_ = view;
}

void Indicator::destroy() {
    if (indicator_) {
        app_indicator_set_status(indicator_, APP_INDICATOR_STATUS_PASSIVE);
        g_object_unref(indicator_);
        indicator_ = nullptr;
    }
    if (menu_) {
        gtk_widget_destroy(menu_);
        g_object_unref(menu_);
        menu_ = nullptr;
    }
    statusItem_ = toggleItem_ = nullptr;
    for (auto* binding : bindings_) delete binding;
    bindings_.clear();
    // Revoke queued activations of this instance.
    aliveToken() = std::make_shared<int>(0);
    gate_.reset();
}
}  // namespace phpvpn::tray
