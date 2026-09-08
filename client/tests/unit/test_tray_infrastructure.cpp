#include "harness.h"
#include "../../apps/tray/ui_controls.h"
#include "../../apps/tray/window_utils.h"
#include "../../apps/tray/shell_adapter.h"
#include "../../apps/tray/progress_window.h"
#include "../../apps/tray/wizard_window.h"
#include "../../apps/tray/editor_window.h"
#include <array>
#include <limits>
#include <type_traits>

namespace {
using namespace phpvpn::tray;
static_assert(!std::is_default_constructible_v<StatusReader>);
static_assert(!std::is_default_constructible_v<CommandReader>);
static_assert(!std::is_default_constructible_v<WizardReader>);
static_assert(!std::is_default_constructible_v<ProgressWindow>);
static_assert(!std::is_default_constructible_v<WizardWindow>);
static_assert(!std::is_default_constructible_v<EditorWindow>);
struct FakeShell : ShellAdapter {
    unsigned adds = 0, modifies = 0, deletes = 0;
    bool succeed = true;
    HICON lastIcon = nullptr;
    std::wstring tip;
    bool notify(DWORD operation, NOTIFYICONDATAW& data) override {
        if (operation == NIM_ADD) ++adds;
        if (operation == NIM_MODIFY) ++modifies;
        if (operation == NIM_DELETE) ++deletes;
        lastIcon = data.hIcon; tip = data.szTip;
        return succeed;
    }
};
unsigned deleted = 0;
struct CountDelete { void operator()(int* p) const { ++deleted; delete p; } };
struct WindowOwner {
    HWND value = nullptr;
    ~WindowOwner() { if (value) DestroyWindow(value); }
};
int failStage = 0, defers = 0, sets = 0;
HDWP WINAPI beginBatch(int) { return failStage == 1 ? nullptr : reinterpret_cast<HDWP>(1); }
HDWP WINAPI deferBatch(HDWP batch, HWND, HWND, int, int, int, int, UINT) {
    ++defers;
    return failStage == 2 && defers == 2 ? nullptr : batch;
}
BOOL WINAPI endBatch(HDWP) { return failStage != 3; }
BOOL WINAPI setPosition(HWND window, HWND after, int x, int y, int width, int height, UINT flags) {
    ++sets;
    return SetWindowPos(window, after, x, y, width, height, flags);
}
Rect bounds(HWND window) { RECT r{}; GetWindowRect(window, &r); return {r.left, r.top, r.right, r.bottom}; }
}
PHPVPN_TEST(TrayShell, UnchangedAndChangedPublishedStatus) {
    FakeShell adapter;
    {
        ShellIcon icon(adapter);
        const auto grey = reinterpret_cast<HICON>(1), green = reinterpret_cast<HICON>(2);
        PHPVPN_CHECK(icon.publish(nullptr, WM_APP, grey, ui::disconnectedTip));
        for (int i = 0; i < 500; ++i) PHPVPN_CHECK(icon.publish(nullptr, WM_APP, grey, ui::disconnectedTip));
        PHPVPN_CHECK_EQ(adapter.adds, 1u);
        PHPVPN_CHECK_EQ(adapter.modifies, 0u);
        PHPVPN_CHECK(icon.publish(nullptr, WM_APP, green, ui::connectedTip));
        PHPVPN_CHECK_EQ(adapter.modifies, 1u);
        PHPVPN_CHECK(icon.publish(nullptr, WM_APP, green, ui::unavailableTip));
        PHPVPN_CHECK_EQ(adapter.modifies, 2u);
        PHPVPN_CHECK(icon.publish(nullptr, WM_APP, grey, ui::unavailableTip));
        PHPVPN_CHECK_EQ(adapter.modifies, 3u);
        icon.remove(); icon.remove();
    }
    PHPVPN_CHECK_EQ(adapter.deletes, 1u);
}
PHPVPN_TEST(TrayShell, FailedPublicationNeverBecomesTheBaseline) {
    FakeShell adapter;
    ShellIcon icon(adapter);
    const auto grey = reinterpret_cast<HICON>(1);
    adapter.succeed = false;
    PHPVPN_CHECK(!icon.publish(nullptr, WM_APP, grey, L"first"));
    adapter.succeed = true;
    PHPVPN_CHECK(icon.publish(nullptr, WM_APP, grey, L"first"));
    PHPVPN_CHECK_EQ(adapter.adds, 2u);
    adapter.succeed = false;
    PHPVPN_CHECK(!icon.publish(nullptr, WM_APP, grey, L"second"));
    adapter.succeed = true;
    PHPVPN_CHECK(icon.publish(nullptr, WM_APP, grey, L"first"));
    PHPVPN_CHECK_EQ(adapter.modifies, 1u);
    PHPVPN_CHECK(icon.publish(nullptr, WM_APP, grey, L"second"));
    PHPVPN_CHECK_EQ(adapter.modifies, 2u);
    PHPVPN_CHECK(!icon.publish(nullptr, WM_APP, grey, std::wstring(128, L'x')));
    PHPVPN_CHECK_EQ(adapter.modifies, 2u);
}
PHPVPN_TEST(TrayResources, OwnershipMovesAndRepeatedResetDeleteExactlyOnce) {
    deleted = 0;
    {
        UniqueUiHandle<int*, CountDelete> first(new int(1));
        first.reset(first.get());
        auto second = std::move(first);
        PHPVPN_CHECK(!first.get());
        first = std::move(second);
        first = std::move(first);
        second.reset(new int(2));
        second = std::move(first);
        PHPVPN_CHECK_EQ(deleted, 1u);
        second.reset(); second.reset();
    }
    PHPVPN_CHECK_EQ(deleted, 2u);
}
PHPVPN_TEST(TrayResources, DpiCacheIsStableAndFailedReplacementPreservesIt) {
    UiResources resources;
    const auto instance = GetModuleHandleW(nullptr);
    PHPVPN_CHECK(resources.update(instance, 96));
    const auto font = resources.font(ui::Font::label);
    const auto icon = resources.icon(0);
    const auto brush = resources.brush(ui::Color::surface);
    for (int i = 0; i < 500; ++i) {
        PHPVPN_CHECK(resources.update(instance, 96));
        PHPVPN_CHECK_EQ(resources.font(ui::Font::label), font);
        PHPVPN_CHECK_EQ(resources.icon(0), icon);
        PHPVPN_CHECK_EQ(resources.brush(ui::Color::surface), brush);
    }
    PHPVPN_CHECK(!resources.update(instance, 0));
    PHPVPN_CHECK_EQ(resources.font(ui::Font::label), font);
    PHPVPN_CHECK(resources.update(instance, 192));
    LOGFONTW info{};
    PHPVPN_CHECK(GetObjectW(resources.font(ui::Font::label), sizeof(info), &info) != 0);
    PHPVPN_CHECK_EQ(info.lfHeight, -30);
    resources.clear(); resources.clear();
    PHPVPN_CHECK_EQ(resources.dpi(), 0);
}
PHPVPN_TEST(TrayResources, TwentyCreateDpiDestroyCyclesReturnGdiAndUserCounts) {
    const auto instance = GetModuleHandleW(nullptr);
    { UiResources warmup; PHPVPN_CHECK(warmup.update(instance, 192)); }
    const auto process = GetCurrentProcess();
    const auto gdi = GetGuiResources(process, GR_GDIOBJECTS), user = GetGuiResources(process, GR_USEROBJECTS);
    for (int cycle = 0; cycle < 20; ++cycle) {
        UiResources resources;
        for (const int dpi : {96, 120, 144, 192, 96}) PHPVPN_CHECK(resources.update(instance, dpi));
    }
    PHPVPN_CHECK_EQ(GetGuiResources(process, GR_GDIOBJECTS), gdi);
    PHPVPN_CHECK_EQ(GetGuiResources(process, GR_USEROBJECTS), user);
}
PHPVPN_TEST(TrayResources, FiveHundredCardRendersDoNotAllocateGdiObjects) {
    UiResources resources;
    PHPVPN_CHECK(resources.update(GetModuleHandleW(nullptr), 96));
    HDC dc = CreateCompatibleDC(nullptr);
    PHPVPN_CHECK(dc != nullptr);
    if (!dc) return;
    paintCard(dc, {0, 0, 100, 100}, resources);
    measureText(dc, resources.font(ui::Font::label), L"Accessible native text with wrapping", 120, true);
    const auto gdi = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
    const auto user = GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS);
    const auto oldFont = GetCurrentObject(dc, OBJ_FONT), oldBrush = GetCurrentObject(dc, OBJ_BRUSH), oldPen = GetCurrentObject(dc, OBJ_PEN);
    for (int i = 0; i < 500; ++i) {
        paintCard(dc, {0, 0, 100, 100}, resources);
        const auto measured = measureText(dc, resources.font(ui::Font::label), L"Accessible native text with wrapping", 120, true);
        PHPVPN_CHECK(measured.height > 15);
    }
    PHPVPN_CHECK_EQ(GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS), gdi);
    PHPVPN_CHECK_EQ(GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS), user);
    PHPVPN_CHECK_EQ(GetCurrentObject(dc, OBJ_FONT), oldFont);
    PHPVPN_CHECK_EQ(GetCurrentObject(dc, OBJ_BRUSH), oldBrush);
    PHPVPN_CHECK_EQ(GetCurrentObject(dc, OBJ_PEN), oldPen);
    DeleteDC(dc);
}
PHPVPN_TEST(TrayControls, EveryDeferredFailureRetriesAllControlsAndKeepsNativeSelection) {
    WindowOwner parent{CreateWindowExW(0, L"STATIC", L"fixture", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        100, 100, 500, 400, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr)};
    PHPVPN_CHECK(parent.value != nullptr);
    if (!parent.value) return;
    const HWND field = CreateWindowExW(0, L"EDIT", L"unchanged input", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        0, 0, 20, 20, parent.value, nullptr, nullptr, nullptr);
    const HWND button = CreateWindowExW(0, L"BUTTON", L"Native action", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        0, 0, 20, 20, parent.value, nullptr, nullptr, nullptr);
    SendMessageW(field, EM_SETSEL, 2, 7);
    const std::array children{ControlPlacement{field, {20, 30, 400, 65}}, ControlPlacement{button, {300, 250, 420, 290}}};
    const WindowPositionApi api{beginBatch, deferBatch, endBatch, setPosition};
    for (failStage = 1; failStage <= 3; ++failStage) {
        defers = sets = 0;
        PHPVPN_CHECK(positionControls(parent.value, children, api));
        PHPVPN_CHECK_EQ(sets, 2);
        DWORD start = 0, end = 0;
        SendMessageW(field, EM_GETSEL, reinterpret_cast<WPARAM>(&start), reinterpret_cast<LPARAM>(&end));
        PHPVPN_CHECK_EQ(start, 2u); PHPVPN_CHECK_EQ(end, 7u);
        wchar_t value[32]{}; GetWindowTextW(field, value, 32);
        PHPVPN_CHECK_EQ(std::wstring(value), L"unchanged input");
        for (const auto& child : children) {
            RECT r{}; GetWindowRect(child.window, &r); MapWindowPoints(nullptr, parent.value, reinterpret_cast<POINT*>(&r), 2);
            PHPVPN_CHECK_EQ((Rect{r.left, r.top, r.right, r.bottom}), child.rect);
        }
    }
    PHPVPN_CHECK(positionControls(parent.value, children));
    ShowWindow(parent.value, SW_SHOWNOACTIVATE);
    ValidateRect(parent.value, nullptr);
    invalidateChanged(parent.value, {1, 2, 3, 4}, false);
    PHPVPN_CHECK(!GetUpdateRect(parent.value, nullptr, FALSE));
    invalidateChanged(parent.value, {1, 2, 3, 4}, true);
    PHPVPN_CHECK(GetUpdateRect(parent.value, nullptr, FALSE));
}
PHPVPN_TEST(TrayControls, EnglishUint64FormattingDoesNotLosePrecision) {
    PHPVPN_CHECK_EQ(ui::englishInteger(0), L"0");
    PHPVPN_CHECK_EQ(ui::englishInteger(9007199254740993ull), L"9,007,199,254,740,993");
    PHPVPN_CHECK_EQ(ui::englishInteger((std::numeric_limits<std::uint64_t>::max)()), L"18,446,744,073,709,551,615");
}
PHPVPN_TEST(TrayPlacement, NativeParentAndReactivationKeepGeometry) {
    const auto monitors = availableMonitors();
    PHPVPN_CHECK(!monitors.empty());
    if (monitors.empty()) return;
    const auto work = monitors.front().work;
    WindowOwner parent{CreateWindowExW(0, L"STATIC", L"fixture parent", WS_OVERLAPPEDWINDOW,
        work.left + 10, work.top + 10, 300, 200, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr)};
    WindowOwner child{CreateWindowExW(0, L"STATIC", L"fixture child", WS_OVERLAPPEDWINDOW,
        0, 0, 1, 1, parent.value, nullptr, GetModuleHandleW(nullptr), nullptr)};
    sizeAndCenter(child.value, {200, 120}, {parent.value, Point{work.right - 1, work.bottom - 1}});
    PHPVPN_CHECK_EQ(MonitorFromWindow(child.value, MONITOR_DEFAULTTONEAREST), MonitorFromWindow(parent.value, MONITOR_DEFAULTTONEAREST));
    const auto original = bounds(child.value);
    sizeAndCenter(child.value, {900, 800}, {});
    PHPVPN_CHECK_EQ(bounds(child.value), original);
    SendMessageW(child.value, WM_DISPLAYCHANGE, 32, 0);
    SendMessageW(child.value, WM_SETTINGCHANGE, SPI_SETWORKAREA, 0);
    PHPVPN_CHECK_EQ(bounds(child.value), original);
    const auto target = targetMonitor({parent.value, {}});
    const RECT recommendation{target.work.left - 50, target.work.top + 10, target.work.left + 150, target.work.top + 160};
    applyDpiRect(child.value, reinterpret_cast<LPARAM>(&recommendation));
    PHPVPN_CHECK_EQ(bounds(child.value), (Rect{target.work.left, target.work.top + 10, target.work.left + 200, target.work.top + 160}));
}
PHPVPN_TEST(TrayReaders, ExplicitMissingOrEmptyEndpointCannotUseProductionDefault) {
    for (const auto& endpoint : {std::wstring{}, L"\\\\.\\pipe\\phpvpn-wp02-missing-" + std::to_wstring(GetCurrentProcessId())}) {
        CommandReader reader(endpoint);
        PHPVPN_CHECK(reader.request(phpvpn::IpcCommand::get_status, {}, nullptr, WM_APP));
        std::optional<std::string> result;
        const auto deadline = GetTickCount64() + 2000;
        while (!(result = reader.take()) && GetTickCount64() < deadline) Sleep(1);
        PHPVPN_CHECK(result && result->empty());
        reader.stop();
    }
}
