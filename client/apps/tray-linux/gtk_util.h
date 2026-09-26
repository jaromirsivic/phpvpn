// Small GLib/GTK helpers shared by the tray windows.
#pragma once
#include <glib-object.h>

namespace phpvpn::tray {
// Captureless lambda as a signal handler. G_CALLBACK is a macro and cannot take a
// lambda with commas; GCallback (void (*)(void)) is the generic function type.
template <class F>
GCallback gtkCallback(F function) {
    return reinterpret_cast<GCallback>(+function);
}
}  // namespace phpvpn::tray
