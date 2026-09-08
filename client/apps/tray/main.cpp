#include "tray_application.h"
#include <commctrl.h>

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int) {
    using namespace phpvpn::tray;
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&controls);
    TrayApplication application({phpvpn::kIpcPipeNameW, L"Local\\phpvpn-tray-session",
                                 std::make_unique<Win32ShellAdapter>(), {}});
    if (application.initialise(instance)) return application.run();
    MessageBoxW(nullptr, ui::startupError, ui::product, MB_OK | MB_ICONERROR);
    return 1;
}
