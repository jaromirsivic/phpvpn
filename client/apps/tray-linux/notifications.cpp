#include "notifications.h"

#include <string>

#include "core/config/config_document.h"
#include "indicator_model.h"

namespace phpvpn::tray {
DesktopNotifications::~DesktopNotifications() { detach(); }

void DesktopNotifications::attach(GDBusConnection* sessionBus) {
    detach();
    if (!sessionBus) return;
    bus_ = G_DBUS_CONNECTION(g_object_ref(sessionBus));
    cancellable_ = g_cancellable_new();
}

void DesktopNotifications::detach() {
    if (cancellable_) {
        g_cancellable_cancel(cancellable_);
        g_object_unref(cancellable_);
        cancellable_ = nullptr;
    }
    if (bus_) {
        g_object_unref(bus_);
        bus_ = nullptr;
    }
}

bool DesktopNotifications::showInformation(std::string_view title, std::string_view body) {
    if (!bus_ || title.empty() || body.empty() || !isStrictUtf8(title) || !isStrictUtf8(body)) return false;
    const std::string summary(title), text(body);
    GVariantBuilder actions;
    g_variant_builder_init(&actions, G_VARIANT_TYPE("as"));
    GVariantBuilder hints;
    g_variant_builder_init(&hints, G_VARIANT_TYPE("a{sv}"));
    g_variant_builder_add(&hints, "{sv}", "desktop-entry", g_variant_new_string("phpvpn-tray"));
    g_variant_builder_add(&hints, "{sv}", "urgency", g_variant_new_byte(1));
    // Notify(app_name, replaces_id, app_icon, summary, body, actions, hints, expire_timeout)
    g_dbus_connection_call(bus_, "org.freedesktop.Notifications", "/org/freedesktop/Notifications",
                           "org.freedesktop.Notifications", "Notify",
                           g_variant_new("(susssasa{sv}i)", "PHPVPN", 0u, std::string(kApplicationIconName).c_str(),
                                         summary.c_str(), text.c_str(), &actions, &hints, -1),
                           G_VARIANT_TYPE("(u)"), G_DBUS_CALL_FLAGS_NONE, 5000, cancellable_,
                           [](GObject* source, GAsyncResult* result, gpointer) {
                               GError* error = nullptr;
                               GVariant* reply = g_dbus_connection_call_finish(G_DBUS_CONNECTION(source), result, &error);
                               if (reply) g_variant_unref(reply);
                               if (error) {
                                   if (!g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
                                       g_warning("PHPVPN notification was not accepted by the desktop");
                                   g_error_free(error);
                               }
                           },
                           nullptr);
    return true;
}
}  // namespace phpvpn::tray
