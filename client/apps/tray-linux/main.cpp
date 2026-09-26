// Entry of the GTK tray (plan 4.12). Autostart runs it without arguments, the
// application launcher with --show-window and the installer with
// --installation-complete; every later launch forwards to the running instance.
#include <glib.h>

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
    TrayApp application(std::move(*options));
    return application.run(argc, argv);
}
