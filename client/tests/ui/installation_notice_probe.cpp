// Explicit interactive probe. Uses an isolated HWND/icon and never contacts
// the VPN service, changes Explorer preferences, or touches the installed tray.
#include "shell_adapter.h"
#include "resource.h"
#include <iostream>
#include <string_view>

namespace {
constexpr UINT callback = WM_APP + 40;
int shown = 0, dismissed = 0;
LRESULT CALLBACK procedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == callback) {
        switch (LOWORD(lParam)) {
            case NIN_BALLOONSHOW: ++shown; std::cout << "NIN_BALLOONSHOW\n" << std::flush; break;
            case NIN_BALLOONHIDE: ++dismissed; std::cout << "NIN_BALLOONHIDE\n" << std::flush; break;
            case NIN_BALLOONTIMEOUT: ++dismissed; std::cout << "NIN_BALLOONTIMEOUT\n" << std::flush; break;
            case NIN_BALLOONUSERCLICK: ++dismissed; std::cout << "NIN_BALLOONUSERCLICK\n" << std::flush; break;
        }
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}
}

int main(int argc, char** argv) {
    using namespace phpvpn::tray;
    const bool hidden = argc > 1 && std::string_view(argv[1]) == "--hidden";
    const auto instance = GetModuleHandleW(nullptr);
    WNDCLASSW type{};
    type.hInstance = instance; type.lpfnWndProc = procedure;
    type.lpszClassName = L"PhpVpnInstallationNoticeProbe";
    if (!RegisterClassW(&type)) return 1;
    const auto window = CreateWindowExW(WS_EX_TOOLWINDOW, type.lpszClassName,
        L"PHPVPN notification probe", WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, instance, nullptr);
    if (!window) return 1;
    Win32ShellAdapter shell;
    ShellIcon icon(shell);
    const auto image = LoadIconW(instance, MAKEINTRESOURCEW(IDI_PHPVPN_APP));
    if (!icon.publish(window, callback, image, L"PHPVPN notification probe")) return 1;
    if (hidden) {
        NOTIFYICONDATAW data{sizeof(data)};
        data.hWnd = window; data.uID = 1; data.uFlags = NIF_STATE;
        data.dwState = NIS_HIDDEN; data.dwStateMask = NIS_HIDDEN;
        if (!shell.notify(NIM_MODIFY, data)) return 1;
    }
    std::cout << "Shell icon published; explicitly hidden=" << hidden << '\n';
    // Allow Explorer to settle the new icon before requesting the notice.
    Sleep(500);
    const bool accepted = phpvpn::showInstallationNotice(icon);
    std::cout << "Native information notice accepted=" << accepted << '\n' << std::flush;
    const auto end = GetTickCount64() + 20'000;
    while (GetTickCount64() < end) {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message); DispatchMessageW(&message);
        }
        Sleep(10);
    }
    icon.remove();
    DestroyWindow(window);
    std::cout << "Shown callbacks=" << shown << ", dismissed callbacks=" << dismissed << '\n';
    if (!accepted) return 1;
    if (!shown) {
        std::cout << "Request accepted but display unconfirmed: check notification/quiet-time policy.\n";
        return 2;
    }
    return 0;
}
