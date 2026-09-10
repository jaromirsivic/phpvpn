#pragma once

#include <windows.h>

namespace phpvpn::tray {

enum class SystemDialogIcon {
    information,
    warning,
    error
};

void showSystemMessage(HWND owner, const wchar_t* text, const wchar_t* title,
                       SystemDialogIcon icon) noexcept;
bool confirmSystemAction(HWND owner, const wchar_t* text, const wchar_t* title) noexcept;
void showStartupError(const wchar_t* text, const wchar_t* title) noexcept;

} // namespace phpvpn::tray
