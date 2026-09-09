#pragma once
#include <windows.h>
#include <shlobj.h>
#include <algorithm>
#include <limits>
#include <string>
#include <string_view>
#include <vector>
#include "layout.h"

namespace phpvpn::tray {
std::wstring pathWithExistingCasing(std::wstring_view path);
inline std::wstring expectedConfigurationPath() {
    PWSTR path = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_ProgramData, KF_FLAG_DONT_VERIFY, nullptr, &path)) || !path)
        return L"(the shared configuration path could not be resolved)";
    const std::wstring result = std::wstring(path) + L"\\phpvpn\\config.json";
    CoTaskMemFree(path); return pathWithExistingCasing(result);
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
// A scope belongs to one dispatched application action. Copy menuPoint into
// the pending operation when opening a window after asynchronous IPC.
struct WindowPlacementContext { HWND parent = nullptr; std::optional<Point> menuPoint; };
class ScopedWindowPlacement {
public:
    explicit ScopedWindowPlacement(const WindowPlacementContext& context);
    ~ScopedWindowPlacement();
    ScopedWindowPlacement(const ScopedWindowPlacement&) = delete;
    ScopedWindowPlacement& operator=(const ScopedWindowPlacement&) = delete;
private:
    WindowPlacementContext previous_;
};
std::vector<MonitorGeometry> availableMonitors();
MonitorGeometry targetMonitor(const WindowPlacementContext& context);
Rect activeWorkArea();
void sizeAndCenter(HWND window, Size logicalClient);
void sizeAndCenter(HWND window, Size logicalClient, const WindowPlacementContext& context);
void ensureUsablePlacement(HWND window);
void reactivateWindow(HWND window);
void applyDpiRect(HWND window, LPARAM lParam);
void setMinimumClientTrackSize(HWND window, MINMAXINFO& info, Size clientDip);
inline HFONT makeFont(int dpi, int pixels, bool bold = false, bool mono = false) {
    return CreateFontW(-scaleForDpi(pixels, dpi), 0, 0, 0, bold ? FW_BOLD : FW_NORMAL,
                       FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                       CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                       mono ? FIXED_PITCH | FF_MODERN : DEFAULT_PITCH, mono ? L"Consolas" : L"Segoe UI");
}
inline void drawText(HDC dc, const std::wstring& text, Rect rect, UINT flags) {
    RECT native{rect.left, rect.top, rect.right, rect.bottom};
    DrawTextW(dc, text.c_str(), static_cast<int>(text.size()), &native, flags | DT_NOPREFIX);
}
}  // namespace phpvpn::tray
