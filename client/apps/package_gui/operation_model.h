#pragma once
#include <windows.h>
#include <string>

namespace phpvpn::package {
enum class Mode { install, uninstall };
enum class State { running, succeeded, failed };
struct Result {
    DWORD exitCode = 0;
    std::wstring error;
    bool succeeded() const { return error.empty() && exitCode == 0; }
};
inline const wchar_t* scriptName(Mode mode) {
    return mode == Mode::install ? L"install.ps1" : L"uninstall.ps1";
}
inline const wchar_t* title(Mode mode, State state) {
    if (mode == Mode::install)
        return state == State::running ? L"Installation in progress..." :
            state == State::succeeded ? L"Installation completed" : L"Installation failed";
    return state == State::running ? L"Uninstallation in progress..." :
        state == State::succeeded ? L"Uninstallation completed" : L"Uninstallation failed";
}
inline COLORREF color(State state) {
    return state == State::running ? RGB(230, 169, 35) :
        state == State::succeeded ? RGB(48, 150, 75) : RGB(195, 52, 52);
}
} // namespace phpvpn::package
