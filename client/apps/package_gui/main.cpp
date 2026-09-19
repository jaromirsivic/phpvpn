#include "package_window.h"

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    // Arguments deliberately cannot select a script, mode or working directory.
#ifdef PHPVPN_PACKAGE_UNINSTALL
    constexpr auto mode = phpvpn::package::Mode::uninstall;
#else
    constexpr auto mode = phpvpn::package::Mode::install;
#endif
    try {
        phpvpn::package::PackageWindow window(mode);
        if (!window.open(instance, phpvpn::package::executableDirectory())) {
            MessageBoxW(nullptr, L"The operation window could not be created.", L"phpvpn launcher failed", MB_OK | MB_ICONERROR);
            return 1;
        }
        MSG message{};
        while (window.handle()) {
            const auto result = GetMessageW(&message, nullptr, 0, 0);
            if (result <= 0) return 1;
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        return window.state() == phpvpn::package::State::succeeded ? 0 : 1;
    } catch (...) {
        MessageBoxW(nullptr, L"The launcher could not locate its package directory.", L"phpvpn launcher failed", MB_OK | MB_ICONERROR);
        return 1;
    }
}
