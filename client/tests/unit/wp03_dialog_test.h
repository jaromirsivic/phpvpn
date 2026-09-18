#pragma once
#include <windows.h>
#include <commctrl.h>
#include <oleacc.h>
#pragma comment(lib, "oleacc.lib")
#include <functional>
#include <string>
#include <vector>

namespace phpvpn::tray::test {
inline int dialogAnswer = IDOK;
inline std::vector<std::wstring> dialogTexts;
inline std::function<void(HWND)> dialogObserver;
// TaskDialog content lives in DirectUI rather than native STATIC controls.
inline void appendAccessibleText(IAccessible* object, std::wstring& text, unsigned depth = 0) {
    if (!object || depth > 12) return;
    VARIANT self{}; self.vt = VT_I4; self.lVal = CHILDID_SELF;
    BSTR name = nullptr;
    if (SUCCEEDED(object->get_accName(self, &name)) && name) { text.append(name, SysStringLen(name)); SysFreeString(name); }
    long count = 0;
    if (FAILED(object->get_accChildCount(&count)) || count <= 0 || count > 512) return;
    std::vector<VARIANT> children(static_cast<std::size_t>(count));
    long obtained = 0;
    if (SUCCEEDED(AccessibleChildren(object, 0, count, children.data(), &obtained))) {
        for (long i = 0; i < obtained; ++i) {
            auto& child = children[static_cast<std::size_t>(i)];
            if (child.vt == VT_DISPATCH && child.pdispVal) {
                IAccessible* nested = nullptr;
                if (SUCCEEDED(child.pdispVal->QueryInterface(IID_IAccessible, reinterpret_cast<void**>(&nested)))) {
                    appendAccessibleText(nested, text, depth + 1); nested->Release();
                }
            } else if (child.vt == VT_I4) {
                name = nullptr;
                if (SUCCEEDED(object->get_accName(child, &name)) && name) { text.append(name, SysStringLen(name)); SysFreeString(name); }
            }
            VariantClear(&child);
        }
    }
}
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
        IAccessible* accessible = nullptr;
        if (SUCCEEDED(AccessibleObjectFromWindow(window, static_cast<DWORD>(OBJID_CLIENT), IID_IAccessible,
            reinterpret_cast<void**>(&accessible)))) {
            appendAccessibleText(accessible, content); accessible->Release();
        }
        dialogTexts.push_back(std::move(content));
        if (dialogObserver) dialogObserver(window);
        if (!IsWindow(window)) return TRUE;
        if (dialogAnswer == 0) {
            PostMessageW(window, WM_KEYDOWN, VK_RETURN, 0);
            PostMessageW(window, WM_KEYUP, VK_RETURN, 0);
            return TRUE;
        }
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
