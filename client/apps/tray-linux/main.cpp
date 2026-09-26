// Entry of the GTK tray (plan 4.12). Autostart runs it without arguments, the
// application launcher with --show-window and the installer with
// --installation-complete; every later launch forwards to the running instance.
#include <glib.h>
#include <gtk/gtk.h>

#include <cstdio>
#include <cstdlib>

#include "tray_app.h"

int main(int argc, char** argv) {
    using namespace phpvpn::tray;
    // The Wayland app_id and the X11 class follow the program name, so windows
    // match phpvpn-tray.desktop however the binary was started.
    g_set_prgname("phpvpn-tray");
    auto options = trayOptionsFromEnvironment([](const char* name) -> const char* { return std::getenv(name); });
    if (!options) {
        std::fputs("phpvpn-tray: invalid PHPVPN_TRAY_* development setting\n", stderr);
        return 2;
    }
    // Same initialisation as GtkApplication startup (no argv parsing), checked up
    // front: without a display gtk_init would only warn and exit. The tray belongs
    // to a desktop session; the service and the phpvpn command need none.
    if (!gtk_init_check(nullptr, nullptr)) {
        std::fputs("phpvpn-tray: cannot open a graphical display (neither WAYLAND_DISPLAY nor DISPLAY works).\n"
                   "The tray runs in a desktop session; use the phpvpn command on a system without one.\n", stderr);
        return 1;
    }
    TrayApp application(std::move(*options));
    return application.run(argc, argv);
}
