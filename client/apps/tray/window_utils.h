#pragma once
#include <windows.h>
#include <shlobj.h>
#include <algorithm>
#include <limits>
#include <string>
#include <string_view>
#include "layout.h"

namespace phpvpn::tray {
inline std::wstring expectedConfigurationPath() {
    PWSTR path = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_ProgramData, KF_FLAG_DONT_VERIFY, nullptr, &path)) || !path)
        return L"(the shared configuration path could not be resolved)";
    const std::wstring result = std::wstring(path) + L"\\PHPVPN\\config.json";
    CoTaskMemFree(path); return result;
}
inline std::wstring widen(std::string_view text) {
    if (text.empty()) return {};
    if (text.size() > static_cast<std::size_t>((std::numeric_limits<int>::max)())) return {};
    const int required = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (required <= 0) return {};
    std::wstring result(static_cast<std::size_t>(required), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), result.data(), required);
    return result;
}
inline std::string narrow(const std::wstring& text) {
    if (text.empty()) return {};
    if (text.size() > static_cast<std::size_t>((std::numeric_limits<int>::max)())) return {};
    const int required = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (required <= 0) return {};
    std::string result(static_cast<std::size_t>(required), '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), result.data(), required, nullptr, nullptr);
    return result;
}
inline std::int64_t nowMs() { return static_cast<std::int64_t>(GetTickCount64()); }
inline Rect activeWorkArea() {
    POINT cursor{};
    GetCursorPos(&cursor);
    MONITORINFO monitor{};
    monitor.cbSize = sizeof(monitor);
    GetMonitorInfoW(MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST), &monitor);
    return {monitor.rcWork.left, monitor.rcWork.top, monitor.rcWork.right, monitor.rcWork.bottom};
}
inline void sizeAndCenter(HWND window, Size logicalClient) {
    const UINT dpi = GetDpiForWindow(window);
    RECT outer{0, 0, scaleForDpi(logicalClient.width, static_cast<int>(dpi)),
               scaleForDpi(logicalClient.height, static_cast<int>(dpi))};
    AdjustWindowRectExForDpi(&outer, static_cast<DWORD>(GetWindowLongPtrW(window, GWL_STYLE)), FALSE,
                             static_cast<DWORD>(GetWindowLongPtrW(window, GWL_EXSTYLE)), dpi);
    const auto work = activeWorkArea();
    const auto rect = centerInWorkArea(clampToWorkArea({outer.right - outer.left, outer.bottom - outer.top},
                                                     work, static_cast<int>(dpi)), work);
    SetWindowPos(window, nullptr, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top,
                 SWP_NOZORDER | SWP_NOACTIVATE);
}
inline HFONT makeFont(int dpi, int pixels, bool bold = false, bool mono = false) {
    return CreateFontW(-scaleForDpi(pixels, dpi), 0, 0, 0, bold ? FW_BOLD : FW_NORMAL,
                       FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                       CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                       mono ? FIXED_PITCH | FF_MODERN : DEFAULT_PITCH, mono ? L"Consolas" : L"Segoe UI");
}
inline void applyDpiRect(HWND window, LPARAM lParam) {
    const auto* rect = reinterpret_cast<const RECT*>(lParam);
    MONITORINFO monitor{};
    monitor.cbSize = sizeof(monitor);
    GetMonitorInfoW(MonitorFromRect(rect, MONITOR_DEFAULTTONEAREST), &monitor);
    const Rect work{monitor.rcWork.left, monitor.rcWork.top, monitor.rcWork.right, monitor.rcWork.bottom};
    const auto size = clampToWorkArea({rect->right - rect->left, rect->bottom - rect->top}, work,
                                      static_cast<int>(GetDpiForWindow(window)));
    const int x = std::clamp(static_cast<int>(rect->left), work.left, work.right - size.width);
    const int y = std::clamp(static_cast<int>(rect->top), work.top, work.bottom - size.height);
    SetWindowPos(window, nullptr, x, y, size.width, size.height,
                 SWP_NOZORDER | SWP_NOACTIVATE);
}
inline void drawText(HDC dc, const std::wstring& text, Rect rect, UINT flags) {
    RECT native{rect.left, rect.top, rect.right, rect.bottom};
    DrawTextW(dc, text.c_str(), static_cast<int>(text.size()), &native, flags | DT_NOPREFIX);
}
}  // namespace phpvpn::tray
