#include "system_dialog.h"
#include "ui_resources.h"

#include <commctrl.h>

namespace phpvpn::tray {
namespace {
struct DialogIcons {
    UiResources resources;
    HWND window = nullptr;
    void apply(HWND value) {
        window = value;
        if (resources.update(GetModuleHandleW(nullptr), static_cast<int>(GetDpiForWindow(value)), false))
            resources.applyApplicationIcons(value);
    }
};
LRESULT CALLBACK dialogIconProcedure(HWND window, UINT message, WPARAM wp, LPARAM lp, UINT_PTR id, DWORD_PTR data) {
    auto& icons = *reinterpret_cast<DialogIcons*>(data);
    const auto result = DefSubclassProc(window, message, wp, lp);
    if (message == WM_DPICHANGED) icons.apply(window);
    if (message == WM_NCDESTROY) { RemoveWindowSubclass(window, dialogIconProcedure, id); icons.window = nullptr; }
    return result;
}
void attachDialogIcons(HWND window, DialogIcons& icons) {
    icons.apply(window);
    SetWindowSubclass(window, dialogIconProcedure, 1, reinterpret_cast<DWORD_PTR>(&icons));
}
HRESULT CALLBACK dialogCreated(HWND window, UINT notification, WPARAM, LPARAM, LONG_PTR data) {
    if (notification == TDN_CREATED) attachDialogIcons(window, *reinterpret_cast<DialogIcons*>(data));
    return S_OK;
}
thread_local DialogIcons* fallbackIcons = nullptr;
LRESULT CALLBACK fallbackHook(int code, WPARAM wp, LPARAM lp) {
    if (code == HCBT_ACTIVATE && fallbackIcons && !fallbackIcons->window) {
        wchar_t name[32]{};
        const auto window = reinterpret_cast<HWND>(wp);
        GetClassNameW(window, name, 32);
        if (wcscmp(name, L"#32770") == 0) attachDialogIcons(window, *fallbackIcons);
    }
    return CallNextHookEx(nullptr, code, wp, lp);
}
int fallbackMessage(HWND owner, const wchar_t* text, const wchar_t* title, UINT flags) {
    DialogIcons icons;
    auto previous = fallbackIcons;
    fallbackIcons = &icons;
    const auto hook = SetWindowsHookExW(WH_CBT, fallbackHook, nullptr, GetCurrentThreadId());
    const int result = MessageBoxW(owner, text, title, flags);
    if (hook) UnhookWindowsHookEx(hook);
    fallbackIcons = previous;
    return result;
}

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
    DialogIcons icons;
    configuration.pfCallback = dialogCreated;
    configuration.lpCallbackData = reinterpret_cast<LONG_PTR>(&icons);
    return TaskDialogIndirect(&configuration, &selectedButton, nullptr, nullptr);
}

} // namespace

void showSystemMessage(HWND owner, const wchar_t* text, const wchar_t* title,
                       SystemDialogIcon icon, const wchar_t* heading) noexcept {
    int selectedButton = 0;
    if (FAILED(runTaskDialog(owner, text, title, TDCBF_OK_BUTTON, IDOK,
                        taskDialogIcon(icon), selectedButton, heading)))
        (void)fallbackMessage(owner, text, title, MB_OK | (icon == SystemDialogIcon::error ? MB_ICONERROR :
            icon == SystemDialogIcon::warning ? MB_ICONWARNING : MB_ICONINFORMATION));
}

bool confirmSystemAction(HWND owner, const wchar_t* text, const wchar_t* title, bool defaultYes) noexcept {
    int selectedButton = IDNO;
    const HRESULT result = runTaskDialog(owner, text, title,
                                         TDCBF_YES_BUTTON | TDCBF_NO_BUTTON,
                                         defaultYes ? IDYES : IDNO, nullptr, selectedButton);
    if (FAILED(result)) return fallbackMessage(owner, text, title, MB_YESNO | (defaultYes ? MB_DEFBUTTON1 : MB_DEFBUTTON2)) == IDYES;
    return selectedButton == IDYES;
}

void showStartupError(const wchar_t* text, const wchar_t* title) noexcept {
    int selectedButton = 0;
    if (FAILED(runTaskDialog(nullptr, text, title, TDCBF_OK_BUTTON, IDOK,
                             TD_ERROR_ICON, selectedButton))) {
        // The tray may fail before Common Controls is usable. Keep this sole,
        // early fatal fallback so the startup failure is still visible.
        (void)fallbackMessage(nullptr, text, title, MB_OK | MB_ICONERROR);
    }
}

} // namespace phpvpn::tray
