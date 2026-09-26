// GLib binding of the IPC Dispatcher. Kept apart from ipc_async.cpp so the channel
// logic can be unit tested without GLib.
#include <glib.h>

#include "ipc_async.h"

namespace phpvpn::tray {
Dispatcher glibMainDispatcher() {
    GMainContext* context = g_main_context_default();
    return [context](std::function<void()> task) {
        auto* heap = new std::function<void()>(std::move(task));
        // Always queued, never run inline: g_main_context_invoke would run the task
        // on the worker thread whenever that thread could acquire the context. The
        // destroy notify runs on the UI thread after the task or at teardown.
        GSource* source = g_idle_source_new();
        g_source_set_priority(source, G_PRIORITY_DEFAULT);
        g_source_set_callback(
            source,
            [](gpointer data) -> gboolean {
                (*static_cast<std::function<void()>*>(data))();
                return G_SOURCE_REMOVE;
            },
            heap, [](gpointer data) { delete static_cast<std::function<void()>*>(data); });
        g_source_attach(source, context);
        g_source_unref(source);
    };
}
}  // namespace phpvpn::tray
