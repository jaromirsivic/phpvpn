#pragma once
#include <windows.h>
#include <commctrl.h>
#include <functional>
#include <string>
#include <vector>

namespace phpvpn::tray::test {
inline int dialogAnswer = IDOK;
inline std::vector<std::wstring> dialogTexts;
inline void CALLBACK answerDialog(HWND, UINT, UINT_PTR, DWORD) {
    EnumThreadWindows(GetCurrentThreadId(), [](HWND window, LPARAM) -> BOOL {
        wchar_t type[64]{};
        GetClassNameW(window, type, 64);
        if (std::wstring_view(type) != L"#32770" || !IsWindowVisible(window)) return TRUE;
        wchar_t title[256]{};
        GetWindowTextW(window, title, 256);
        std::wstring content(title);
        EnumChildWindows(window, [](HWND child, LPARAM p) -> BOOL {
            const int length = GetWindowTextLengthW(child);
            std::wstring text(length + 1, L'\0');
            GetWindowTextW(child, text.data(), length + 1); text.resize(length);
            *reinterpret_cast<std::wstring*>(p) += text;
            return TRUE;
        }, reinterpret_cast<LPARAM>(&content));
        dialogTexts.push_back(std::move(content));
        // TaskDialog uses DirectUI controls whose native button ID is zero.
        // Its public message takes the logical IDOK/IDYES/IDNO button ID.
        if (FindWindowExW(window, nullptr, L"DirectUIHWND", nullptr)) {
            PostMessageW(window, TDM_CLICK_BUTTON, dialogAnswer, 0);
            return TRUE;
        }
        auto button = GetDlgItem(window, dialogAnswer);
        if (!button && dialogAnswer == IDOK) button = GetDlgItem(window, IDCANCEL);
        if (button) PostMessageW(window, WM_COMMAND, MAKEWPARAM(GetDlgCtrlID(button), BN_CLICKED), reinterpret_cast<LPARAM>(button));
        return TRUE;
    }, 0);
}
inline void answer(int value, const std::function<void()>& action) {
    dialogAnswer = value;
    const auto timer = SetTimer(nullptr, 0, 10, answerDialog);
    action();
    KillTimer(nullptr, timer);
}
}
