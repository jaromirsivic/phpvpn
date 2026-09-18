#pragma once

#include <windows.h>

namespace phpvpn::tray {

enum class SystemDialogIcon {
    information,
    warning,
    error
};

void showSystemMessage(HWND owner, const wchar_t* text, const wchar_t* title,
                       SystemDialogIcon icon, const wchar_t* heading = nullptr) noexcept;
bool confirmSystemAction(HWND owner, const wchar_t* text, const wchar_t* title, bool defaultYes = false) noexcept;
void showStartupError(const wchar_t* text, const wchar_t* title) noexcept;

} // namespace phpvpn::tray
