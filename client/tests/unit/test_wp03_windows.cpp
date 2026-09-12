#include "../../apps/tray/editor_window.h"
#include "../../apps/tray/progress_window.h"
#include "../../apps/tray/resource.h"
#include "../../apps/tray/statistics_window.h"
#include "../../apps/tray/window_utils.h"
#include "contracts_fixture.h"
#include "harness.h"
#include "platform/windows/ipc_pipe.h"
#include "tray_fixture.h"
#include "wp03_dialog_test.h"
#include <atomic>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <thread>
#include <commctrl.h>
#include <ole2.h>
#pragma comment(lib, "ole32.lib")

namespace {
using namespace phpvpn;
using namespace phpvpn::tray;
std::wstring windowText(HWND h) {
    const int n = GetWindowTextLengthW(h);
    std::wstring text(n + 1, L'\0');
    GetWindowTextW(h, text.data(), n + 1);
    text.resize(n);
    return text;
}
void pump(unsigned milliseconds = 1) {
    const auto deadline = GetTickCount64() + milliseconds;
    do {
        MSG m{};
        while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE))
            if (m.message != WM_QUIT) {
                TranslateMessage(&m);
                DispatchMessageW(&m);
            }
        Sleep(1);
    } while (GetTickCount64() < deadline);
}
bool until(const std::function<bool()> &predicate, unsigned ms = 2000) {
    const auto deadline = GetTickCount64() + ms;
    while (!predicate() && GetTickCount64() < deadline)
        pump();
    return predicate();
}
TrayStatus connected() {
    auto s = parseStatusResponse(phpvpn::tray::test::kStatusFixture);
    s.state = "connected";
    s.phase = "running";
    s.tunnelReady = "yes";
    s.serverUrl = s.serverSecret = s.accessKey = phpvpn::tray::CheckpointResult::success;
    return s;
}
struct Backend {
    const std::wstring pipe =
        L"\\\\.\\pipe\\phpvpn-wp03-" + widen(createPlatformCryptoProvider().value()->randomUuidV4().value());
    std::atomic<unsigned> configs{0}, statuses{0}, other{0}, inflight{0}, maximum{0}, delay{0};
    std::mutex mutex;
    std::string document = contracts_test::document("10.99.99.3");
    IpcPipeServer server;
    Backend() : server([this](std::string_view request) { return reply(request); }, options()) {
        PHPVPN_CHECK(server.start().ok());
    }
    ~Backend() { server.stop(); }
    IpcPipeServerOptions options() {
        IpcPipeServerOptions o;
        o.pipeName = pipe;
        return o;
    }
    std::string reply(std::string_view request) {
        const auto parsed = IpcProtocol::parseRequest(request);
        if (!parsed) {
            ++other;
            return "{}";
        }
        if (parsed->command == IpcCommand::get_config) {
            ++configs;
            std::lock_guard lock(mutex);
            return document;
        }
        if (parsed->command == IpcCommand::get_status) {
            ++statuses;
            const auto active = ++inflight;
            auto old = maximum.load();
            while (old < active && !maximum.compare_exchange_weak(old, active)) {
            }
            Sleep(delay.load());
            auto fixture = phpvpn::tray::test::fixture();
            phpvpn::tray::test::member(fixture, "requestId") = JsonValue::makeString(parsed->requestId);
            --inflight;
            return Json::serialize(fixture);
        }
        ++other;
        return "{}";
    }
};
Rect bounds(HWND h, HWND parent) {
    RECT r{};
    GetWindowRect(h, &r);
    MapWindowPoints(nullptr, parent, reinterpret_cast<POINT *>(&r), 2);
    return {r.left, r.top, r.right, r.bottom};
}
bool hasChildText(HWND parent, std::wstring_view expected) {
    struct Search { std::wstring_view expected; bool found = false; } search{expected};
    EnumChildWindows(parent, [](HWND child, LPARAM value) -> BOOL {
        auto &search = *reinterpret_cast<Search *>(value);
        if (windowText(child) == search.expected) {
            search.found = true;
            return FALSE;
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&search));
    return search.found;
}
void resizeClient(HWND h, Size size) {
    const int dpi = GetDpiForWindow(h);
    // Initial-placement helpers deliberately preserve the size of existing windows.
    // Resize the real frame and allow for a scrollbar appearing or disappearing.
    for (int attempt = 0; attempt < 4; ++attempt) {
        RECT client{}, frame{};
        GetClientRect(h, &client);
        GetWindowRect(h, &frame);
        const int dx = scaleForDpi(size.width, dpi) - client.right;
        const int dy = scaleForDpi(size.height, dpi) - client.bottom;
        if (!dx && !dy)
            break;
        PHPVPN_CHECK(SetWindowPos(h, nullptr, 0, 0, frame.right - frame.left + dx,
                                 frame.bottom - frame.top + dy, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE));
    }
    RECT client{};
    GetClientRect(h, &client);
    PHPVPN_CHECK_EQ(client.right, scaleForDpi(size.width, dpi));
    PHPVPN_CHECK_EQ(client.bottom, scaleForDpi(size.height, dpi));
    std::cout << "Resized client " << client.right << "x" << client.bottom << " DPI=" << dpi << "\n";
}
void capture(HWND h, const wchar_t *name, bool verifyConnectCard = false) {
    wchar_t directory[32768]{};
    const bool save = GetEnvironmentVariableW(L"PHPVPN_WP03_CAPTURE_DIR", directory, 32768) != 0;
    if (!save && !verifyConnectCard)
        return;
    RECT client{};
    GetClientRect(h, &client);
    HDC source = GetDC(h), dc = CreateCompatibleDC(source);
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = client.right;
    info.bmiHeader.biHeight = -client.bottom;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    void *pixels = nullptr;
    HBITMAP bitmap = CreateDIBSection(source, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
    auto old = SelectObject(dc, bitmap);
    PHPVPN_CHECK(PrintWindow(h, dc, PW_CLIENTONLY));
    std::wcout << L"WP03 capture " << name << L": " << client.right << L"x" << client.bottom
               << L" pixels, DPI " << GetDpiForWindow(h) << L"\n";
    BITMAPFILEHEADER header{};
    header.bfType = 0x4d42;
    header.bfOffBits = sizeof(header) + sizeof(BITMAPINFOHEADER);
    header.bfSize = header.bfOffBits + client.right * client.bottom * 4;
    if (save) {
        std::ofstream out(std::filesystem::path(directory) / name, std::ios::binary);
        out.write(reinterpret_cast<const char *>(&header), sizeof(header));
        out.write(reinterpret_cast<const char *>(&info.bmiHeader), sizeof(BITMAPINFOHEADER));
        out.write(static_cast<const char *>(pixels), client.right * client.bottom * 4);
        PHPVPN_CHECK(out.good());
    }
    if (verifyConnectCard) {
        const int dpi = GetDpiForWindow(h);
        const auto p = [dpi](int n) { return scaleForDpi(n, dpi); };
        // Scan the rendered border across the whole client, independently of the layout helper.
        // These lines cross blank card content and avoid headings, fields and the separator.
        std::vector<int> horizontal, vertical;
        for (int x = 0; x < client.right; ++x)
            if (GetPixel(dc, x, p(300)) == RGB(214, 220, 231))
                horizontal.push_back(x);
        for (int y = 0; y < client.bottom; ++y)
            if (GetPixel(dc, p(840), y) == RGB(214, 220, 231))
                vertical.push_back(y);
        PHPVPN_CHECK(horizontal.size() >= 2 && vertical.size() >= 2);
        if (horizontal.size() >= 2 && vertical.size() >= 2) {
            const std::array<int, 4> edges{horizontal.front(), vertical.front(), horizontal.back(), vertical.back()};
            std::cout << "Painted Connect edges DIP:";
            for (const auto edge : edges)
                std::cout << " " << edge * 96.0 / dpi;
            std::cout << "\n";
            const int margin = buttonFooterMargin(client.right, dpi);
            PHPVPN_CHECK(std::abs(edges[0] - margin) <= scaleForDpi(2, dpi));
            PHPVPN_CHECK(std::abs(edges[2] - (client.right - margin - 1)) <= scaleForDpi(2, dpi));
            PHPVPN_CHECK(edges[1] < edges[3]);
        }
    }
    SelectObject(dc, old);
    DeleteObject(bitmap);
    DeleteDC(dc);
    ReleaseDC(h, source);
}
} // namespace
PHPVPN_TEST(WP03Windows, ConnectCardKeepsSharedMarginsAndFourPaintedEdgesAfterResize) {
    Backend backend;
    for (const bool success : {false, true}) {
        ProgressWindow progress(backend.pipe);
        progress.open(GetModuleHandleW(nullptr));
        PHPVPN_CHECK(until([&] { return windowText(GetDlgItem(progress.window(), 411)) == L"10.99.99.3"; }));
        auto status = connected();
        if (!success) {
            status.hasError = true;
            status.errorCode = "access_key_rejected";
            status.accessKey = phpvpn::tray::CheckpointResult::failed;
        }
        progress.onSnapshot(status);
        const HWND url = GetDlgItem(progress.window(), 410), ip = GetDlgItem(progress.window(), 411);
        for (const Size size : {Size{940, 680}, Size{640, 480}, Size{320, 280}, Size{940, 680}}) {
            resizeClient(progress.window(), size);
            SendMessageW(progress.window(), WM_VSCROLL, SB_TOP, 0);
            pump(10);
            PHPVPN_CHECK_EQ(GetDlgItem(progress.window(), 410), url);
            PHPVPN_CHECK_EQ(GetDlgItem(progress.window(), 411), ip);
            PHPVPN_CHECK_EQ(windowText(ip), L"10.99.99.3");
            if (size.width == 940) {
                RECT client{};
                GetClientRect(progress.window(), &client);
                const int dpi = GetDpiForWindow(progress.window());
                PHPVPN_CHECK_EQ(client.right, scaleForDpi(940, dpi));
                PHPVPN_CHECK_EQ(client.bottom, scaleForDpi(680, dpi));
                capture(progress.window(), success ? L"connect-reference-success.bmp" : L"connect-reference-failure.bmp", true);
            }
            for (const HWND field : {url, ip}) {
                SetFocus(field);
                RECT client{};
                GetClientRect(progress.window(), &client);
                const auto r = bounds(field, progress.window());
                PHPVPN_CHECK(r.left >= 0 && r.right <= client.right && r.top >= 0 && r.bottom <= client.bottom);
            }
            if (size.width == 320) {
                SendMessageW(progress.window(), WM_VSCROLL, SB_BOTTOM, 0);
                capture(progress.window(), success ? L"connect-reference-success-narrow.bmp" : L"connect-reference-failure-narrow.bmp");
            }
        }
        SendMessageW(progress.window(), WM_CLOSE, 0, 0);
    }
    PHPVPN_CHECK_EQ(backend.configs.load(), 2u);
    PHPVPN_CHECK_EQ(backend.other.load(), 0u);
}
PHPVPN_TEST(WP03Windows, ReferenceGeometrySurvivesNativeResizeAndEditorUndo) {
    Backend backend;
    ProgressWindow progress(backend.pipe);
    progress.open(GetModuleHandleW(nullptr));
    auto failure = connected();
    failure.hasError = true;
    failure.errorCode = "server_verification_failed";
    failure.serverSecret = phpvpn::tray::CheckpointResult::failed;
    progress.onSnapshot(failure);
    const auto details = FindWindowExW(progress.window(), nullptr, L"STATIC", L"Connection details");
    PHPVPN_CHECK(details != nullptr);
    for (const Size size : {Size{940, 680}, Size{640, 480}, Size{320, 280}, Size{940, 680}}) {
        resizeClient(progress.window(), size);
        SendMessageW(progress.window(), WM_VSCROLL, SB_TOP, 0);
        pump(10);
        const int dpi = GetDpiForWindow(progress.window());
        const double cardLeft = (bounds(details, progress.window()).left - scaleForDpi(24, dpi)) * 96.0 / dpi;
        if (size.width == 940) {
            RECT client{};
            GetClientRect(progress.window(), &client);
            PHPVPN_CHECK_EQ(client.right, scaleForDpi(940, dpi));
            PHPVPN_CHECK_EQ(client.bottom, scaleForDpi(680, dpi));
            PHPVPN_CHECK(std::abs(cardLeft - buttonFooterMargin(client.right, dpi) * 96.0 / dpi) <= 2.0);
            std::cout << "Geometry Connect: DPI=" << dpi << " client=" << client.right << "x" << client.bottom
                      << " cardLeftDIP=" << cardLeft << "\n";
        }
        for (int id : {410, 411}) {
            const auto field = GetDlgItem(progress.window(), id);
            SetFocus(field);
            RECT client{};
            GetClientRect(progress.window(), &client);
            const auto r = bounds(field, progress.window());
            PHPVPN_CHECK(r.left >= 0 && r.right <= client.right && r.top >= 0 && r.bottom <= client.bottom);
        }
    }
    SendMessageW(progress.window(), WM_CLOSE, 0, 0);
    EditorWindow editor(backend.pipe);
    editor.open(GetModuleHandleW(nullptr), L"PHPVPN configuration", "{}",
                [](const std::string &) { return false; }, "Z:\\synthetic\\config.json");
    const auto field = GetDlgItem(editor.window(), IDC_TEXT_AREA);
    SetFocus(field);
    SendMessageW(field, EM_SETSEL, 1, 1);
    SendMessageW(field, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(L"x"));
    SendMessageW(field, EM_SETSEL, 1, 2);
    for (const Size size : {Size{1080, 560}, Size{640, 400}, Size{320, 360}, Size{1080, 560}}) {
        resizeClient(editor.window(), size);
        pump(10);
        const int dpi = GetDpiForWindow(editor.window());
        const auto r = bounds(field, editor.window());
        if (size.width == 1080) {
            RECT client{};
            GetClientRect(editor.window(), &client);
            PHPVPN_CHECK_EQ(client.right, scaleForDpi(1080, dpi));
            PHPVPN_CHECK_EQ(client.bottom, scaleForDpi(560, dpi));
            // The reference describes the outer text frame. Native EDIT has a 6 DIP inset.
            PHPVPN_CHECK(std::abs(r.top * 96.0 / dpi - 6.0 - (202.0 - 55.0) * 560.0 / 949.0) <= 6.0);
            std::cout << "Geometry Editor: DPI=" << dpi << " client=" << client.right << "x" << client.bottom
                      << " textTopDIP=" << r.top * 96.0 / dpi << "\n";
        }
        DWORD first = 0, last = 0;
        SendMessageW(field, EM_GETSEL, reinterpret_cast<WPARAM>(&first), reinterpret_cast<LPARAM>(&last));
        PHPVPN_CHECK_EQ(first, 1u);
        PHPVPN_CHECK_EQ(last, 2u);
        PHPVPN_CHECK_EQ(GetFocus(), field);
        PHPVPN_CHECK(SendMessageW(field, EM_CANUNDO, 0, 0));
        PHPVPN_CHECK(r.bottom <= bounds(GetDlgItem(editor.window(), IDC_SAVE), editor.window()).top);
    }
    PHPVPN_CHECK(SendMessageW(field, EM_UNDO, 0, 0));
    PHPVPN_CHECK_EQ(windowText(field), L"{}");
    PHPVPN_CHECK_EQ(backend.other.load(), 0u);
    for (const auto &monitor : availableMonitors())
        std::cout << "Available monitor DPI=" << monitor.dpi << " work=" << monitor.work.left << ","
                  << monitor.work.top << "," << monitor.work.right << "," << monitor.work.bottom << "\n";
}
PHPVPN_TEST(WP03Windows, OneConfigPerOpenAndIndependentStatusWithNoDisconnect) {
    Backend backend;
    backend.delay = 350;
    ProgressWindow progress(backend.pipe);
    progress.open(GetModuleHandleW(nullptr));
    PHPVPN_CHECK(until([&] { return windowText(GetDlgItem(progress.window(), 411)) == L"10.99.99.3"; }));
    PHPVPN_CHECK_EQ(backend.configs.load(), 1u);
    {
        std::lock_guard lock(backend.mutex);
        backend.document = contracts_test::document("10.99.99.4");
    }
    pump(1300);
    PHPVPN_CHECK_EQ(windowText(GetDlgItem(progress.window(), 411)), L"10.99.99.3");
    PHPVPN_CHECK_EQ(backend.configs.load(), 1u);
    PHPVPN_CHECK(backend.statuses >= 2 && backend.maximum == 1);
    SendMessageW(progress.window(), WM_CLOSE, 0, 0);
    const auto reads = backend.statuses.load();
    pump(550);
    PHPVPN_CHECK_EQ(backend.statuses.load(), reads);
    progress.open(GetModuleHandleW(nullptr));
    PHPVPN_CHECK(until([&] { return windowText(GetDlgItem(progress.window(), 411)) == L"10.99.99.4"; }));
    PHPVPN_CHECK_EQ(backend.configs.load(), 2u);
    PHPVPN_CHECK_EQ(progress.model().view().stage, ProgressStage::connecting);
    SendMessageW(progress.window(), WM_CLOSE, 0, 0);
    PHPVPN_CHECK_EQ(backend.other.load(), 0u);
    std::cout << "WP03 two opens: get-config=" << backend.configs << ", get-status=" << backend.statuses
              << ", maximum simultaneous status=" << backend.maximum << ", other=" << backend.other << "\n";
}
PHPVPN_TEST(WP03Windows, StaticProgressContextSurvivesEveryState) {
    Backend backend;
    ProgressWindow progress(backend.pipe);
    progress.open(GetModuleHandleW(nullptr));
    const HWND context = GetDlgItem(progress.window(), IDC_PROGRESS_CONTEXT);
    PHPVPN_CHECK(context != nullptr);
    PHPVPN_CHECK_EQ(windowText(context), L"Connecting client to the server.");
    progress.onSnapshot(parseStatusResponse(phpvpn::tray::test::kStatusFixture));
    PHPVPN_CHECK_EQ(windowText(context), L"Connecting client to the server.");
    progress.onSnapshot(connected());
    PHPVPN_CHECK_EQ(windowText(context), L"Connecting client to the server.");
    progress.open(GetModuleHandleW(nullptr));
    auto failure = connected();
    failure.hasError = true;
    failure.errorCode = "access_key_rejected";
    progress.onSnapshot(failure);
    PHPVPN_CHECK_EQ(GetDlgItem(progress.window(), IDC_PROGRESS_CONTEXT), context);
    PHPVPN_CHECK_EQ(windowText(context), L"Connecting client to the server.");
    progress.open(GetModuleHandleW(nullptr));
    progress.onSnapshot({});
    PHPVPN_CHECK_EQ(windowText(context), L"Connecting client to the server.");

    progress.open(GetModuleHandleW(nullptr), true);
    PHPVPN_CHECK_EQ(GetDlgItem(progress.window(), IDC_PROGRESS_CONTEXT), context);
    PHPVPN_CHECK_EQ(windowText(context), L"Disconnecting client from the server.");
    auto disconnected = connected();
    disconnected.state = "disconnected";
    disconnected.phase = "idle";
    disconnected.tunnelReady = "no";
    progress.onDisconnectReply(disconnected);
    PHPVPN_CHECK_EQ(windowText(context), L"Disconnecting client from the server.");
    progress.open(GetModuleHandleW(nullptr), true);
    auto disconnectFailure = disconnected;
    disconnectFailure.hasError = true;
    disconnectFailure.errorCode = "communication_failed";
    progress.onDisconnectReply(disconnectFailure);
    PHPVPN_CHECK_EQ(windowText(context), L"Disconnecting client from the server.");

    for (const auto old : {L"Connection status", L"Connecting", L"Successfully connected", L"Connection failed",
                           L"VPN connection status", L"The VPN connection is ready."})
        PHPVPN_CHECK(!hasChildText(progress.window(), old));
    SendMessageW(progress.window(), WM_CLOSE, 0, 0);
}
PHPVPN_TEST(WP03Windows, DefaultProgressHeightsFitEveryVisiblePanelWithLightBottomGap) {
    Backend backend;
    const auto instance = GetModuleHandleW(nullptr);
    const auto monitors = availableMonitors();
    PHPVPN_CHECK(!monitors.empty());
    if (monitors.empty()) return;
    const auto &monitor = monitors.front();
    const WindowPlacementContext placementContext{
        nullptr, Point{(monitor.work.left + monitor.work.right) / 2, (monitor.work.top + monitor.work.bottom) / 2}};
    const auto verify = [](ProgressWindow &progress, Size expected, const wchar_t *actionText) {
        RECT client{};
        GetClientRect(progress.window(), &client);
        const int dpi = GetDpiForWindow(progress.window());
        const auto p = [dpi](int n) { return scaleForDpi(n, dpi); };
        PHPVPN_CHECK_EQ(client.right, p(expected.width));
        PHPVPN_CHECK_EQ(client.bottom, p(expected.height));
        const HWND action = FindWindowExW(progress.window(), nullptr, L"STATIC", actionText);
        PHPVPN_CHECK(action != nullptr);
        if (action) {
            const int panelBottom = bounds(action, progress.window()).bottom + p(24);
            PHPVPN_CHECK(panelBottom + p(8) <= client.bottom);
            PHPVPN_CHECK(client.bottom - panelBottom <= p(24));
        }
    };

    ProgressWindow connect(backend.pipe);
    {
        ScopedWindowPlacement placement(placementContext);
        connect.open(instance);
    }
    auto failure = connected();
    failure.hasError = true;
    failure.errorCode = "communication_failed";
    connect.onSnapshot(failure);
    verify(connect, {940, 560}, L"Check the network and server availability, then try again.");
    SendMessageW(connect.window(), WM_CLOSE, 0, 0);

    ProgressWindow disconnect(backend.pipe);
    {
        ScopedWindowPlacement placement(placementContext);
        disconnect.open(instance, true);
    }
    verify(disconnect, {760, 192}, L"Please wait while the VPN disconnects.");
    auto disconnected = connected();
    disconnected.state = "disconnected";
    disconnected.phase = "idle";
    disconnected.tunnelReady = "no";
    disconnect.onDisconnectReply(disconnected);
    verify(disconnect, {760, 192}, L"You have been disconnected successfully.");
    disconnect.open(instance, true);
    auto disconnectFailure = disconnected;
    disconnectFailure.hasError = true;
    disconnectFailure.errorCode = "communication_failed";
    disconnect.onDisconnectReply(disconnectFailure);
    verify(disconnect, {760, 192},
           L"The service did not confirm Disconnect. Try Disconnect again or check the service status.");
    SendMessageW(disconnect.window(), WM_CLOSE, 0, 0);
}
PHPVPN_TEST(WP03Windows, ActualPaintStartsCountdownAfterHiddenSuccessAndClosesInBounds) {
    for (bool disconnect : {false, true}) {
    Backend backend;
    ProgressWindow progress(backend.pipe);
    progress.open(GetModuleHandleW(nullptr), disconnect);
    if (!disconnect) PHPVPN_CHECK(until([&] { return backend.configs == 1; }));
    else {
        pump(10);
        capture(progress.window(), L"disconnect-waiting.bmp");
        SendMessageW(progress.window(), WM_CLOSE, 0, 0);
        PHPVPN_CHECK(progress.window() != nullptr);
        progress.onDisconnectReply(connected());
        progress.onSnapshot({});
        PHPVPN_CHECK_EQ(progress.model().view().stage, ProgressStage::connecting);
    }
    ShowWindow(progress.window(), SW_MINIMIZE);
    auto completed = connected();
    if (disconnect) { completed.state = "disconnected"; completed.phase = "idle"; completed.tunnelReady = "no"; }
    progress.onSnapshot(completed);
    pump(100);
    PHPVPN_CHECK(!progress.model().successDeadlineMs());
    const HWND panelHeading = FindWindowExW(progress.window(), nullptr, L"STATIC",
        disconnect ? L"Disconnected" : L"Connected successfully");
    PHPVPN_CHECK(panelHeading != nullptr);
    const HDC printDc = GetDC(progress.window());
    SendMessageW(panelHeading, WM_PRINT, reinterpret_cast<WPARAM>(printDc), PRF_CLIENT);
    ReleaseDC(progress.window(), printDc);
    PHPVPN_CHECK(!progress.model().successDeadlineMs());
    ShowWindow(progress.window(), SW_HIDE);
    pump(10);
    PHPVPN_CHECK(!progress.model().successDeadlineMs());
    ShowWindow(progress.window(), SW_RESTORE);
    RedrawWindow(progress.window(), nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
    PHPVPN_CHECK(until([&] { return progress.model().successDeadlineMs().has_value(); }));
    if (!progress.model().successDeadlineMs())
        return;
    const auto first = *progress.model().successDeadlineMs() - 3000;
    capture(progress.window(), disconnect ? L"disconnect-success.bmp" : L"connect-success.bmp");
    bool two = false, one = false;
    const auto limit = GetTickCount64() + 4000;
    while (progress.window() && GetTickCount64() < limit) {
        pump();
        two = two || progress.model().view().closingSeconds == 2;
        one = one || progress.model().view().closingSeconds == 1;
    }
    const auto elapsed = nowMs() - first;
    std::cout << "WP03 success visible-to-close ms: " << elapsed << "\n";
    PHPVPN_CHECK(!progress.window());
    PHPVPN_CHECK(elapsed >= 3000 && elapsed <= 3500);
    PHPVPN_CHECK(two && one);
    PHPVPN_CHECK_EQ(backend.configs.load(), disconnect ? 0u : 1u);
    PHPVPN_CHECK_EQ(backend.other.load(), 0u);
    std::cout << "WP03 success: get-config=" << backend.configs << ", get-status=" << backend.statuses
              << ", other=" << backend.other << "\n";
    }
}
PHPVPN_TEST(WP03Windows, FailureFreezesAndBadConfigDoesNotBecomeBootstrapFailure) {
    Backend backend;
    {
        std::lock_guard lock(backend.mutex);
        backend.document = "{}";
    }
    ProgressWindow progress(backend.pipe);
    progress.open(GetModuleHandleW(nullptr));
    pump(100);
    PHPVPN_CHECK_EQ(progress.model().view().stage, ProgressStage::connecting);
    PHPVPN_CHECK_EQ(windowText(GetDlgItem(progress.window(), 410)), L"Unavailable");
    auto s = connected();
    s.hasError = true;
    s.errorCode = "server_verification_failed";
    s.serverSecret = phpvpn::tray::CheckpointResult::failed;
    s.accessKey = phpvpn::tray::CheckpointResult::pending;
    s.errorDescription = "synthetic raw detail that must not escape";
    progress.onSnapshot(s);
    pump(10);
    capture(progress.window(), L"connect-failure.bmp");
    SetWindowPos(progress.window(), nullptr, 0, 0, 480, 360, SWP_NOMOVE | SWP_NOZORDER);
    SendMessageW(progress.window(), WM_VSCROLL, SB_BOTTOM, 0);
    pump(10);
    capture(progress.window(), L"connect-narrow.bmp");
    PHPVPN_CHECK_EQ(progress.model().view().errorCode, "server_verification_failed");
    progress.onSnapshot(connected());
    PHPVPN_CHECK_EQ(progress.model().view().stage, ProgressStage::failed);
    SendMessageW(progress.window(), WM_CLOSE, 0, 0);
    PHPVPN_CHECK_EQ(backend.other.load(), 0u);
}
PHPVPN_TEST(WP03Windows, StatisticsNativeExactValuesStaleAndFocusScroll) {
    StatisticsWindow stats;
    stats.open(GetModuleHandleW(nullptr), {});
    PHPVPN_CHECK_EQ(windowText(GetDlgItem(stats.window(), 500)), L"-");
    PHPVPN_CHECK(FindWindowExW(stats.window(), nullptr, L"STATIC", L"(Disconnected)") != nullptr);
    auto s = connected();
    s.connectionStartedAtUtcMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count() - 254035000;
    stats.onSnapshot(s, false);
    pump(10);
    const auto duration = FindWindowExW(stats.window(), nullptr, L"STATIC", L"(Connected for 2d 22h 33m 55s)");
    PHPVPN_CHECK(duration != nullptr);
    PHPVPN_CHECK(until([&] { return windowText(duration) != L"(Connected for 2d 22h 33m 55s)"; }, 2200));
    capture(stats.window(), L"statistics.bmp");
    for (auto field : kCounterFields)
        s.counters.*field.member = UINT64_MAX;
    stats.onSnapshot(s, false);
    for (std::size_t i = 0; i < statisticsFields.size(); ++i) {
        const auto expected = statisticsFields[i].addCounterIndex != kNoStatisticsCounter
                                  ? L"36893.49 PB"
                              : statisticsFields[i].bytes ? L"18446.74 PB" : L"18,446,744,073,709,551,615";
        PHPVPN_CHECK_EQ(windowText(GetDlgItem(stats.window(), 500 + static_cast<int>(i))), expected);
    }
    stats.onSnapshot({}, true);
    PHPVPN_CHECK(windowText(duration).starts_with(L"(Connected for 2d 22h 33m "));
    SetWindowPos(stats.window(), nullptr, 0, 0, 480, 360, SWP_NOMOVE | SWP_NOZORDER);
    for (std::size_t i = 0; i < statisticsFields.size(); ++i) {
        const auto value = GetDlgItem(stats.window(), 500 + static_cast<int>(i));
        SetFocus(value);
        RECT client{};
        GetClientRect(stats.window(), &client);
        auto r = bounds(value, stats.window());
        PHPVPN_CHECK(r.top >= 0 && r.bottom <= client.bottom);
        PHPVPN_CHECK((GetWindowLongW(value, GWL_STYLE) & (ES_READONLY | WS_TABSTOP)) == (ES_READONLY | WS_TABSTOP));
    }
    pump(10);
    capture(stats.window(), L"statistics-narrow-max.bmp");
    SendMessageW(stats.window(), WM_CLOSE, 0, 0);
}
// Snapshot every format before WM_COPY. Unsupported data aborts before any
// clipboard mutation. Destruction restores even when an assertion fails.
class ClipboardRestore {
    std::vector<std::pair<UINT, HANDLE>> data_;
    HWND owner_;
    bool captured_ = false;
public:
    explicit ClipboardRestore(HWND owner) : owner_(owner) {}
    bool capture() {
        if (!OpenClipboard(owner_)) return false;
        bool ok = true;
        for (UINT format = EnumClipboardFormats(0); format; format = EnumClipboardFormats(format)) {
            auto source = GetClipboardData(format);
            auto copy = source ? OleDuplicateData(source, static_cast<CLIPFORMAT>(format), 0) : nullptr;
            if (!copy) { ok = false; break; }
            data_.emplace_back(format, copy);
        }
        CloseClipboard();
        captured_ = ok;
        return ok;
    }
    ~ClipboardRestore() {
        if (captured_) {
            bool opened = false;
            for (int retry = 0; retry < 100 && !opened; ++retry) {
                opened = OpenClipboard(owner_) != FALSE;
                if (!opened) Sleep(10);
            }
            PHPVPN_CHECK(opened);
            if (opened) {
                PHPVPN_CHECK(EmptyClipboard());
                for (auto& [format, handle] : data_) {
                    if (SetClipboardData(format, handle)) handle = nullptr;
                    else PHPVPN_CHECK(false);
                }
                CloseClipboard();
            }
        }
        for (auto [format, handle] : data_) if (handle) {
            if (format == CF_BITMAP || format == CF_PALETTE || format == CF_DSPBITMAP) DeleteObject(handle);
            else if (format == CF_ENHMETAFILE || format == CF_DSPENHMETAFILE) DeleteEnhMetaFile(static_cast<HENHMETAFILE>(handle));
            else {
                if (format == CF_METAFILEPICT || format == CF_DSPMETAFILEPICT) {
                    auto picture = static_cast<METAFILEPICT*>(GlobalLock(handle));
                    if (picture) { DeleteMetaFile(picture->hMF); GlobalUnlock(handle); }
                }
                GlobalFree(handle);
            }
        }
    }
};
PHPVPN_TEST(WP03Windows, ServerInformationTabsAreNativeAndDataStable) {
    EditorWindow editor(L"\\\\.\\pipe\\unused-info-tabs-test");
    editor.open(GetModuleHandleW(nullptr), L"Server Information", "{\"server\":\"unchanged\"}");
    const auto tabs = GetDlgItem(editor.window(), IDC_INFO_TABS);
    PHPVPN_CHECK(tabs != nullptr);
    if (!tabs) return;
    const auto field = GetDlgItem(editor.window(), IDC_TEXT_AREA);
    const auto original = windowText(field);
    PHPVPN_CHECK_EQ(TabCtrl_GetItemCount(tabs), 2);
    for (int i = 0; i < 2; ++i) {
        wchar_t label[80]{};
        TCITEMW item{}; item.mask = TCIF_TEXT; item.pszText = label; item.cchTextMax = 80;
        PHPVPN_CHECK(SendMessageW(tabs, TCM_GETITEMW, i, reinterpret_cast<LPARAM>(&item)));
        PHPVPN_CHECK_EQ(std::wstring(label), i == 0 ? L"Who is online" : L"Raw server information");
    }
    PHPVPN_CHECK_EQ(TabCtrl_GetCurSel(tabs), 0);
    PHPVPN_CHECK(!IsWindowVisible(field));
    PHPVPN_CHECK_EQ(GetFocus(), tabs);
    SendMessageW(tabs, WM_KEYDOWN, VK_RIGHT, 0);
    PHPVPN_CHECK_EQ(TabCtrl_GetCurSel(tabs), 1);
    PHPVPN_CHECK(IsWindowVisible(field));
    const auto fieldBounds = bounds(field, editor.window());
    PHPVPN_CHECK_EQ(ChildWindowFromPointEx(editor.window(),
        POINT{fieldBounds.left + 2, fieldBounds.top + 2}, CWP_SKIPINVISIBLE), field);
    PHPVPN_CHECK_EQ(windowText(field), original);
    {
        ClipboardRestore clipboard(editor.window());
        const bool captured = clipboard.capture();
        PHPVPN_CHECK(captured);
        if (captured) {
            SendMessageW(field, EM_SETSEL, 2, 7);
            SendMessageW(field, WM_COPY, 0, 0);
            const bool opened = OpenClipboard(editor.window()) != FALSE;
            PHPVPN_CHECK(opened);
            if (opened) {
                const auto handle = GetClipboardData(CF_UNICODETEXT);
                const auto copied = handle ? static_cast<const wchar_t*>(GlobalLock(handle)) : nullptr;
                PHPVPN_CHECK(copied != nullptr);
                if (copied) { PHPVPN_CHECK_EQ(std::wstring(copied), original.substr(2, 5)); GlobalUnlock(handle); }
                CloseClipboard();
            }
        }
    }
    PHPVPN_CHECK((GetWindowLongW(field, GWL_STYLE) & (ES_READONLY | ES_MULTILINE | WS_VSCROLL | WS_HSCROLL)) ==
                 (ES_READONLY | ES_MULTILINE | WS_VSCROLL | WS_HSCROLL));
    SendMessageW(field, EM_SETSEL, 2, 7);
    const auto scroll = SendMessageW(field, EM_GETFIRSTVISIBLELINE, 0, 0);
    for (int i = 0; i < 12; ++i) editor.selectInfoTab(i % 2);
    editor.selectInfoTab(-1); editor.selectInfoTab(2);
    PHPVPN_CHECK_EQ(TabCtrl_GetCurSel(tabs), 1);
    DWORD first = 0, last = 0;
    SendMessageW(field, EM_GETSEL, reinterpret_cast<WPARAM>(&first), reinterpret_cast<LPARAM>(&last));
    PHPVPN_CHECK_EQ(first, 2u); PHPVPN_CHECK_EQ(last, 7u);
    PHPVPN_CHECK_EQ(SendMessageW(field, EM_GETFIRSTVISIBLELINE, 0, 0), scroll);
    PHPVPN_CHECK_EQ(windowText(field), original);
    editor.selectInfoTab(0);
    SendMessageW(editor.window(), WM_LBUTTONDOWN, 0, MAKELPARAM(100, 150));
    PHPVPN_CHECK(GetFocus() != field);
    PHPVPN_CHECK_EQ(GetNextDlgTabItem(editor.window(), tabs, FALSE), GetDlgItem(editor.window(), IDOK));
    editor.selectInfoTab(1);
    PHPVPN_CHECK_EQ(GetNextDlgTabItem(editor.window(), tabs, FALSE), field);
    MSG key{}; key.hwnd = tabs; key.message = WM_KEYDOWN; key.wParam = VK_TAB;
    SetFocus(tabs); PHPVPN_CHECK(editor.translate(key)); PHPVPN_CHECK_EQ(GetFocus(), field);
    BYTE previousKeys[256]{}; GetKeyboardState(previousKeys);
    BYTE shifted[256]{}; std::copy(std::begin(previousKeys), std::end(previousKeys), shifted);
    shifted[VK_SHIFT] |= 0x80; SetKeyboardState(shifted);
    key = {}; key.hwnd = field; key.message = WM_KEYDOWN; key.wParam = VK_TAB;
    PHPVPN_CHECK(editor.translate(key));
    SetKeyboardState(previousKeys);
    PHPVPN_CHECK_EQ(GetFocus(), tabs);
    key.hwnd = field; key.wParam = VK_RETURN;
    PHPVPN_CHECK(editor.translate(key)); PHPVPN_CHECK(!editor.window());
    editor.open(GetModuleHandleW(nullptr), L"Server Information", "{}");
    key.hwnd = GetFocus(); key.wParam = VK_ESCAPE;
    PHPVPN_CHECK(editor.translate(key)); PHPVPN_CHECK(!editor.window());
}
PHPVPN_TEST(WP03Windows, ServerInformationTabsPreserveNonzeroScrollAfterResize) {
    std::string source = "{\"rows\":[";
    for (int i = 0; i < 150; ++i) source += (i ? ",\"" : "\"") + std::string(240, 'x') + "\"";
    source += "]}";
    EditorWindow editor(L"\\\\.\\pipe\\unused-info-scroll-test");
    editor.open(GetModuleHandleW(nullptr), L"Server Information", source);
    editor.selectInfoTab(1);
    const auto field = GetDlgItem(editor.window(), IDC_TEXT_AREA);
    const auto original = windowText(field);
    SendMessageW(field, EM_SETSEL, 5000, 5010);
    SendMessageW(field, EM_LINESCROLL, 20, 25);
    const auto vertical = SendMessageW(field, EM_GETFIRSTVISIBLELINE, 0, 0);
    const auto horizontal = GetScrollPos(field, SB_HORZ);
    PHPVPN_CHECK(vertical > 0 && horizontal > 0);
    for (int i = 0; i < 12; ++i) editor.selectInfoTab(i % 2);
    PHPVPN_CHECK_EQ(SendMessageW(field, EM_GETFIRSTVISIBLELINE, 0, 0), vertical);
    PHPVPN_CHECK_EQ(GetScrollPos(field, SB_HORZ), horizontal);
    SetWindowPos(editor.window(), nullptr, 0, 0, 850, 600, SWP_NOMOVE | SWP_NOZORDER);
    const auto resizedVertical = SendMessageW(field, EM_GETFIRSTVISIBLELINE, 0, 0);
    const auto resizedHorizontal = GetScrollPos(field, SB_HORZ);
    editor.selectInfoTab(0); editor.selectInfoTab(1);
    PHPVPN_CHECK_EQ(SendMessageW(field, EM_GETFIRSTVISIBLELINE, 0, 0), resizedVertical);
    PHPVPN_CHECK_EQ(GetScrollPos(field, SB_HORZ), resizedHorizontal);
    DWORD first = 0, last = 0;
    SendMessageW(field, EM_GETSEL, reinterpret_cast<WPARAM>(&first), reinterpret_cast<LPARAM>(&last));
    PHPVPN_CHECK_EQ(first, 5000u); PHPVPN_CHECK_EQ(last, 5010u);
    PHPVPN_CHECK_EQ(windowText(field), original);
    PHPVPN_CHECK_EQ(field, GetDlgItem(editor.window(), IDC_TEXT_AREA));
    pump(10);
    capture(editor.window(), L"server-information-raw.bmp");
    editor.selectInfoTab(0); pump(10);
    capture(editor.window(), L"server-information-who.bmp");
}
PHPVPN_TEST(WP03Windows, EditorPreservesNulSelectionDirtyAndSeparatesShowInfo) {
    Backend backend;
    EditorWindow editor(backend.pipe);
    std::string source("bad\0json\n", 9), saved;
    unsigned saves = 0;
    editor.open(
        GetModuleHandleW(nullptr), L"PHPVPN configuration", source,
        [&](const std::string &text) {
            ++saves;
            saved = text;
            return false;
        },
        "Z:\\synthetic\\config.json");
    auto field = GetDlgItem(editor.window(), IDC_TEXT_AREA);
    PHPVPN_CHECK(windowText(editor.window()).find(L"JSON string literal") != std::wstring::npos);
    const auto style = GetWindowLongW(field, GWL_STYLE);
    PHPVPN_CHECK((style & (WS_VSCROLL | WS_HSCROLL | ES_AUTOHSCROLL | ES_MULTILINE)) ==
                 (WS_VSCROLL | WS_HSCROLL | ES_AUTOHSCROLL | ES_MULTILINE));
    SendMessageW(field, EM_SETSEL, 2, 5);
    for (int width : {1500, 480, 1080})
        SetWindowPos(editor.window(), nullptr, 0, 0, width, 560, SWP_NOMOVE | SWP_NOZORDER);
    DWORD start = 0, end = 0;
    SendMessageW(field, EM_GETSEL, reinterpret_cast<WPARAM>(&start), reinterpret_cast<LPARAM>(&end));
    PHPVPN_CHECK_EQ(start, 2u);
    PHPVPN_CHECK_EQ(end, 5u);
    SendMessageW(editor.window(), WM_COMMAND, IDC_SAVE, 0);
    PHPVPN_CHECK_EQ(saves, 1u);
    PHPVPN_CHECK_EQ(saved, source);
    SendMessageW(field, EM_SETSEL, 1, 1);
    SendMessageW(field, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(L"X"));
    PHPVPN_CHECK(SendMessageW(field, EM_GETMODIFY, 0, 0));
    const auto operation = editor.beginSave(GetTickCount64() + 30000);
    PHPVPN_CHECK(operation.has_value());
    auto foreign = *operation;
    ++foreign.windowGeneration;
    PHPVPN_CHECK(!editor.finishSave({foreign, SaveOutcome::notSent, ErrorCode::ipc_unavailable}));
    SendMessageW(editor.window(), WM_COMMAND, IDC_SAVE, 0);
    PHPVPN_CHECK_EQ(saves, 1u);
    phpvpn::tray::test::answer(IDOK, [&] { editor.finishSave({*operation, SaveOutcome::notSent, ErrorCode::ipc_unavailable}); });
    SendMessageW(editor.window(), WM_COMMAND, IDC_SAVE, 0);
    PHPVPN_CHECK_EQ(saves, 2u);
    PHPVPN_CHECK_EQ(saved, "X" + source);
    PHPVPN_CHECK_EQ(GetNextDlgTabItem(editor.window(), field, FALSE), GetDlgItem(editor.window(), 304));
    PHPVPN_CHECK_EQ(GetNextDlgTabItem(editor.window(), GetDlgItem(editor.window(), 304), FALSE),
                    GetDlgItem(editor.window(), IDC_CANCEL));
    editor.open(
        GetModuleHandleW(nullptr), L"PHPVPN configuration", makeDefaultEditorDocument(),
        [&](const std::string &) { return false; }, "Z:\\synthetic\\config.json");
    pump(10);
    capture(editor.window(), L"edit-config.bmp");
    editor.open(GetModuleHandleW(nullptr), L"PHPVPN information", "synthetic info");
    PHPVPN_CHECK(!GetDlgItem(editor.window(), IDC_SAVE) && !GetDlgItem(editor.window(), 304));
    unsigned children = 0;
    for (auto h = GetWindow(editor.window(), GW_CHILD); h; h = GetWindow(h, GW_HWNDNEXT))
        ++children;
    PHPVPN_CHECK_EQ(children, 4u);
    PHPVPN_CHECK_EQ(backend.configs.load(), 0u);
}
PHPVPN_TEST(WP03Windows, FiveHundredPaintsAndTwentyWindowCyclesKeepObjectsStable) {
    Backend backend;
    const auto cycle = [&](bool stress) {
        ProgressWindow progress(backend.pipe);
        StatisticsWindow stats;
        EditorWindow editor(backend.pipe);
        progress.open(GetModuleHandleW(nullptr));
        stats.open(GetModuleHandleW(nullptr), connected());
        editor.open(
            GetModuleHandleW(nullptr), L"PHPVPN configuration", "{}", [](const std::string &) { return false; },
            "synthetic");
        pump(10);
        const auto process = GetCurrentProcess();
        const auto gdi = GetGuiResources(process, GR_GDIOBJECTS), user = GetGuiResources(process, GR_USEROBJECTS);
        if (stress) {
            for (int i = 0; i < 500; ++i)
                for (HWND h : {progress.window(), stats.window(), editor.window()})
                    RedrawWindow(h, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
            PHPVPN_CHECK_EQ(GetGuiResources(process, GR_GDIOBJECTS), gdi);
            PHPVPN_CHECK_EQ(GetGuiResources(process, GR_USEROBJECTS), user);
        }
    };
    cycle(false);
    pump(10);
    const auto process = GetCurrentProcess();
    const auto gdi = GetGuiResources(process, GR_GDIOBJECTS), user = GetGuiResources(process, GR_USEROBJECTS);
    cycle(true);
    for (int i = 0; i < 20; ++i)
        cycle(false);
    pump(10);
    std::cout << "WP03 GDI/USER after cycles: " << GetGuiResources(process, GR_GDIOBJECTS) - gdi << "/"
              << GetGuiResources(process, GR_USEROBJECTS) - user << "\n";
    PHPVPN_CHECK(GetGuiResources(process, GR_GDIOBJECTS) <= gdi + 5);
    PHPVPN_CHECK(GetGuiResources(process, GR_USEROBJECTS) <= user + 5);
}
