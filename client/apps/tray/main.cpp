#include "tray_application.h"
#include "system_dialog.h"
#include "tray_arguments.h"
#include <commctrl.h>

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int) {
    using namespace phpvpn::tray;
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_STANDARD_CLASSES};
    if (!InitCommonControlsEx(&controls)) {
        showStartupError(ui::startupError, ui::product);
        return 1;
    }
    int argc = 0;
    auto argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) { showStartupError(ui::startupError, ui::product); return 1; }
    const bool installationComplete = requestsInstallationNotice(argc, argv);
    LocalFree(argv);
    TrayApplication application({phpvpn::kIpcPipeNameW, L"Local\\phpvpn-tray-session",
                                 std::make_unique<Win32ShellAdapter>(), {}, installationComplete});
    if (application.initialise(instance)) return application.run();
    showStartupError(ui::startupError, ui::product);
    return 1;
}
