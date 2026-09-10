#include "tray_application.h"
#include "system_dialog.h"
#include <commctrl.h>

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int) {
    using namespace phpvpn::tray;
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_STANDARD_CLASSES};
    if (!InitCommonControlsEx(&controls)) {
        showStartupError(ui::startupError, ui::product);
        return 1;
    }
    TrayApplication application({phpvpn::kIpcPipeNameW, L"Local\\phpvpn-tray-session",
                                 std::make_unique<Win32ShellAdapter>(), {}});
    if (application.initialise(instance)) return application.run();
    showStartupError(ui::startupError, ui::product);
    return 1;
}
