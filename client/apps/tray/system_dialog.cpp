#include "system_dialog.h"

#include <commctrl.h>

namespace phpvpn::tray {
namespace {

PCWSTR taskDialogIcon(SystemDialogIcon icon) noexcept {
    switch (icon) {
        case SystemDialogIcon::information: return TD_INFORMATION_ICON;
        case SystemDialogIcon::warning: return TD_WARNING_ICON;
        case SystemDialogIcon::error: return TD_ERROR_ICON;
    }
    return nullptr;
}

HRESULT runTaskDialog(HWND owner, const wchar_t* text, const wchar_t* title,
                      TASKDIALOG_COMMON_BUTTON_FLAGS buttons, int defaultButton,
                      PCWSTR icon, int& selectedButton, const wchar_t* heading = nullptr) noexcept {
    TASKDIALOGCONFIG configuration{};
    configuration.cbSize = sizeof(configuration);
    configuration.hwndParent = owner;
    configuration.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION;
    if (owner && IsWindowVisible(owner)) {
        configuration.dwFlags |= TDF_POSITION_RELATIVE_TO_WINDOW;
    }
    configuration.dwCommonButtons = buttons;
    configuration.pszWindowTitle = title;
    configuration.pszContent = text;
    configuration.pszMainInstruction = heading;
    configuration.pszMainIcon = icon;
    configuration.nDefaultButton = defaultButton;
    return TaskDialogIndirect(&configuration, &selectedButton, nullptr, nullptr);
}

} // namespace

void showSystemMessage(HWND owner, const wchar_t* text, const wchar_t* title,
                       SystemDialogIcon icon, const wchar_t* heading) noexcept {
    int selectedButton = 0;
    (void)runTaskDialog(owner, text, title, TDCBF_OK_BUTTON, IDOK,
                        taskDialogIcon(icon), selectedButton, heading);
}

bool confirmSystemAction(HWND owner, const wchar_t* text, const wchar_t* title, bool defaultYes) noexcept {
    int selectedButton = IDNO;
    const HRESULT result = runTaskDialog(owner, text, title,
                                         TDCBF_YES_BUTTON | TDCBF_NO_BUTTON,
                                         defaultYes ? IDYES : IDNO, nullptr, selectedButton);
    return SUCCEEDED(result) && selectedButton == IDYES;
}

void showStartupError(const wchar_t* text, const wchar_t* title) noexcept {
    int selectedButton = 0;
    if (FAILED(runTaskDialog(nullptr, text, title, TDCBF_OK_BUTTON, IDOK,
                             TD_ERROR_ICON, selectedButton))) {
        // The tray may fail before Common Controls is usable. Keep this sole,
        // early fatal fallback so the startup failure is still visible.
        (void)MessageBoxW(nullptr, text, title, MB_OK | MB_ICONERROR);
    }
}

} // namespace phpvpn::tray
