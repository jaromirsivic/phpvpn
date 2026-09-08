#include "window_utils.h"
#include "ui_controls.h"
#include <commctrl.h>
#include <shellscalingapi.h>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shcore.lib")

namespace phpvpn::tray {
namespace {
thread_local WindowPlacementContext currentPlacement;
constexpr UINT_PTR placementSubclassId = 0x5056504e;
Rect asRect(const RECT& r) { return {r.left, r.top, r.right, r.bottom}; }
BOOL CALLBACK collectMonitor(HMONITOR handle, HDC, LPRECT, LPARAM data) {
    MONITORINFO info{sizeof(info)};
    if (!GetMonitorInfoW(handle, &info)) return TRUE;
    UINT x = 96, y = 96;
    if (FAILED(GetDpiForMonitor(handle, MDT_EFFECTIVE_DPI, &x, &y))) x = 96;
    auto& monitors = *reinterpret_cast<std::vector<MonitorGeometry>*>(data);
    monitors.push_back({reinterpret_cast<std::uintptr_t>(handle), asRect(info.rcMonitor),
        asRect(info.rcWork), static_cast<int>(x), (info.dwFlags & MONITORINFOF_PRIMARY) != 0});
    return TRUE;
}
std::optional<std::uintptr_t> windowMonitor(HWND window) {
    // Message-only coordination windows are not placement parents.
    if (!window || !IsWindow(window) || GetParent(window) == HWND_MESSAGE) return {};
    const auto monitor = MonitorFromWindow(window, MONITOR_DEFAULTTONULL);
    return monitor ? std::optional(reinterpret_cast<std::uintptr_t>(monitor)) : std::nullopt;
}
void place(HWND window, Rect rect) {
    RECT old{};
    if (GetWindowRect(window, &old) && asRect(old) == rect) return;
    SetWindowPos(window, nullptr, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top,
                 SWP_NOZORDER | SWP_NOACTIVATE);
}
LRESULT CALLBACK placementProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam,
                                    UINT_PTR, DWORD_PTR busy) {
    if (message == WM_WINDOWPOSCHANGING && lParam)
        reinterpret_cast<WINDOWPOS*>(lParam)->flags |= SWP_NOCOPYBITS | SWP_NOREDRAW;
    if (message == WM_WINDOWPOSCHANGED || message == WM_DPICHANGED) {
        // DefWindowProc sends WM_SIZE inside WM_WINDOWPOSCHANGED. Commit after
        // the complete position change, including left/top edge exposure and
        // any nested DPI/scrollbar layout, rather than midway through WM_SIZE.
        LayoutFrame frame(window);
        return DefSubclassProc(window, message, wParam, lParam);
    }
    if (message == WM_NCDESTROY) {
        RemoveWindowSubclass(window, placementProcedure, placementSubclassId);
        return DefSubclassProc(window, message, wParam, lParam);
    }
    if (!busy && ((message == WM_ACTIVATE && LOWORD(wParam) != WA_INACTIVE) ||
        message == WM_DISPLAYCHANGE || message == WM_SETTINGCHANGE ||
        (message == WM_SIZE && wParam == SIZE_RESTORED))) {
        SetWindowSubclass(window, placementProcedure, placementSubclassId, 1);
        ensureUsablePlacement(window);
        SetWindowSubclass(window, placementProcedure, placementSubclassId, 0);
    }
    return DefSubclassProc(window, message, wParam, lParam);
}
}
ScopedWindowPlacement::ScopedWindowPlacement(const WindowPlacementContext& context) : previous_(currentPlacement) {
    currentPlacement = context;
}
ScopedWindowPlacement::~ScopedWindowPlacement() { currentPlacement = previous_; }
std::vector<MonitorGeometry> availableMonitors() {
    std::vector<MonitorGeometry> result;
    EnumDisplayMonitors(nullptr, nullptr, collectMonitor, reinterpret_cast<LPARAM>(&result));
    return result;
}
MonitorGeometry targetMonitor(const WindowPlacementContext& context) {
    const auto monitors = availableMonitors();
    POINT cursor{};
    MonitorPreference preference{windowMonitor(context.parent), context.menuPoint,
                                 windowMonitor(GetForegroundWindow()), {}};
    if (GetCursorPos(&cursor)) preference.cursor = Point{cursor.x, cursor.y};
    const auto selected = selectMonitor(monitors, preference);
    if (selected) return monitors[*selected];
    RECT work{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    return {0, asRect(work), asRect(work), 96, true};
}
Rect activeWorkArea() { return targetMonitor(currentPlacement).work; }
void sizeAndCenter(HWND window, Size dip) { sizeAndCenter(window, dip, currentPlacement); }
void sizeAndCenter(HWND window, Size dip, const WindowPlacementContext& context) {
    if (!IsWindow(window)) return;
    DWORD_PTR data = 0;
    if (GetWindowSubclass(window, placementProcedure, placementSubclassId, &data)) {
        ensureUsablePlacement(window);
        return;
    }
    auto effective = context;
    if (!effective.parent) effective.parent = GetWindow(window, GW_OWNER);
    const auto monitor = targetMonitor(effective);
    RECT frame{};
    const auto style = static_cast<DWORD>(GetWindowLongPtrW(window, GWL_STYLE));
    const auto extended = static_cast<DWORD>(GetWindowLongPtrW(window, GWL_EXSTYLE));
    if (!AdjustWindowRectExForDpi(&frame, style, GetMenu(window) != nullptr, extended, static_cast<UINT>(monitor.dpi))) return;
    // Initial placement can synchronously cause WM_DPICHANGED. Existing views
    // process it normally. The final rectangle still uses the target DPI.
    place(window, initialWindowRect(dip, monitor, {frame.right - frame.left, frame.bottom - frame.top}));
    SetWindowSubclass(window, placementProcedure, placementSubclassId, 0);
}
void ensureUsablePlacement(HWND window) {
    if (!IsWindow(window) || IsIconic(window) || IsZoomed(window)) return;
    RECT current{};
    if (GetWindowRect(window, &current)) place(window, usableWindowRect(asRect(current), availableMonitors()));
}
void reactivateWindow(HWND window) {
    if (!IsWindow(window)) return;
    if (IsIconic(window)) ShowWindow(window, SW_RESTORE);
    ensureUsablePlacement(window);
    ShowWindow(window, SW_SHOW);
    SetForegroundWindow(window);
}
void applyDpiRect(HWND window, LPARAM lParam) {
    if (!lParam) return;
    // Accept the OS DPI transition, then perform only necessary work-area repair.
    const auto recommended = asRect(*reinterpret_cast<const RECT*>(lParam));
    place(window, usableWindowRect(recommended, availableMonitors()));
}
} // namespace phpvpn::tray
