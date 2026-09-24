#include "package_window.h"
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <gdiplus.h>
#pragma comment(lib, "gdiplus.lib")

using namespace phpvpn::package;
namespace {
void require(bool value, const char* reason) { if (!value) throw std::runtime_error(reason); }
void pump() { MSG message{}; while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&message); DispatchMessageW(&message); } }
template<class Predicate> bool until(Predicate predicate, ULONGLONG timeout = 10000) {
    const auto deadline = GetTickCount64() + timeout;
    do { pump(); if (predicate()) return true; Sleep(5); } while (GetTickCount64() < deadline);
    return false;
}
std::wstring text(HWND window) {
    std::wstring result(static_cast<std::size_t>(GetWindowTextLengthW(window)) + 1, L'\0');
    result.resize(static_cast<std::size_t>(GetWindowTextW(window, result.data(), static_cast<int>(result.size()))));
    return result;
}
void checkGeometry(HWND window, HWND log) {
    RECT client{}, edit{};
    GetClientRect(window, &client); GetWindowRect(log, &edit);
    MapWindowPoints(nullptr, window, reinterpret_cast<POINT*>(&edit), 2);
    const auto margin = MulDiv(20, static_cast<int>(GetDpiForWindow(window)), 96);
    require(edit.left == margin && edit.top == margin && client.right - edit.right == margin && client.bottom - edit.bottom == margin, "20 DIP margins changed");
}
void checkColor(HWND window, State state) {
    UpdateWindow(window);
    auto dc = GetDC(window);
    const auto actual = GetPixel(dc, 3, 3);
    ReleaseDC(window, dc);
    require(actual == color(state), "incorrect visible status color");
}
void capture(HWND window, const wchar_t* name) {
    wchar_t destination[32768]{};
    if (!GetEnvironmentVariableW(L"PHPVPN_PACKAGE_CAPTURE_DIR", destination, 32768)) return;
    const std::filesystem::path directory(destination);
    std::filesystem::create_directories(directory);
    RECT rectangle{}; GetWindowRect(window, &rectangle);
    const int width = rectangle.right - rectangle.left, height = rectangle.bottom - rectangle.top;
    const auto source = GetWindowDC(window), dc = CreateCompatibleDC(source);
    const auto bitmap = CreateCompatibleBitmap(source, width, height);
    const auto previous = SelectObject(dc, bitmap);
    require(PrintWindow(window, dc, 2) != FALSE, "window capture failed");
    SelectObject(dc, previous);
    Gdiplus::GdiplusStartupInput startup;
    ULONG_PTR token = 0;
    require(Gdiplus::GdiplusStartup(&token, &startup, nullptr) == Gdiplus::Ok, "capture startup failed");
    {
        Gdiplus::Bitmap image(bitmap, nullptr);
        CLSID png{};
        CLSIDFromString(L"{557cf406-1a04-11d3-9a73-0000f81ef32e}", &png);
        require(image.Save((directory / name).c_str(), &png) == Gdiplus::Ok, "capture save failed");
    }
    Gdiplus::GdiplusShutdown(token);
    DeleteObject(bitmap); DeleteDC(dc); ReleaseDC(window, source);
}
}
int main() {
    const auto directory = std::filesystem::temp_directory_path() / (L"phpvpn UI fixture " + std::to_wstring(GetCurrentProcessId()));
    try {
        std::filesystem::create_directories(directory);
        for (const auto mode : {Mode::install, Mode::uninstall}) {
            for (const auto code : {0, 37}) {
                {
                    std::ofstream script(directory / scriptName(mode), std::ios::binary);
                    script << "[Console]::OutputEncoding = New-Object System.Text.UTF8Encoding($false)\n"
                              "[Console]::WriteLine('EARLY'); Start-Sleep -Milliseconds 800; [Console]::Error.WriteLine('STDERR');\n"
                              "Write-Host 'HOST-LF'; [Console]::Write('SPLIT-CR'); [Console]::Write(\"`r\"); [Console]::Out.Flush(); "
                              "Start-Sleep -Milliseconds 800; [Console]::Write(\"`nSPLIT-LF`n\");\n"
                              "[Console]::Write((('x' * 300 + \"`r`n\") * 1000)); [Console]::WriteLine('FINAL'); exit " << code;
                }
                PackageWindow window(mode);
                require(window.open(GetModuleHandleW(nullptr), directory), "open failed");
                require(!window.open(GetModuleHandleW(nullptr), directory), "double start accepted");
                const auto root = window.handle(), log = window.logHandle();
                require(text(root) == title(mode, State::running), "running title");
                require(SendMessageW(root, WM_GETICON, ICON_BIG, 0) && SendMessageW(root, WM_GETICON, ICON_SMALL, 0), "window icons missing");
                const auto logStyle = GetWindowLongPtrW(log, GWL_STYLE);
                require((logStyle & (ES_READONLY | ES_MULTILINE | ES_AUTOVSCROLL | ES_AUTOHSCROLL | WS_VSCROLL | WS_HSCROLL)) ==
                    (ES_READONLY | ES_MULTILINE | ES_AUTOVSCROLL | ES_AUTOHSCROLL | WS_VSCROLL | WS_HSCROLL), "log control styles");
                checkGeometry(root, log); checkColor(root, State::running);
                RECT minimum{0, 0, MulDiv(520, static_cast<int>(GetDpiForWindow(root)), 96), MulDiv(360, static_cast<int>(GetDpiForWindow(root)), 96)};
                AdjustWindowRectExForDpi(&minimum, static_cast<DWORD>(GetWindowLongPtrW(root, GWL_STYLE)), FALSE, 0, GetDpiForWindow(root));
                MINMAXINFO tracking{}; tracking.ptMaxTrackSize = {123456, 123456};
                SendMessageW(root, WM_GETMINMAXINFO, 0, reinterpret_cast<LPARAM>(&tracking));
                require(tracking.ptMinTrackSize.x == minimum.right - minimum.left && tracking.ptMinTrackSize.y == minimum.bottom - minimum.top, "minimum client size changed");
                require(tracking.ptMaxTrackSize.x == 123456 && tracking.ptMaxTrackSize.y == 123456, "custom maximum size imposed");
                IAccessible* accessible = nullptr;
                require(SUCCEEDED(AccessibleObjectFromWindow(log, static_cast<DWORD>(OBJID_CLIENT), IID_IAccessible, reinterpret_cast<void**>(&accessible))), "accessible log missing");
                VARIANT child{}; child.vt = VT_I4; child.lVal = CHILDID_SELF;
                BSTR accessibleName = nullptr;
                accessible->get_accName(child, &accessibleName);
                require(accessibleName && std::wstring(accessibleName) == L"Operation output", "log accessible name");
                SysFreeString(accessibleName); accessible->Release();
                MONITORINFO monitor{sizeof(monitor)};
                POINT cursor{}; GetCursorPos(&cursor);
                GetMonitorInfoW(MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST), &monitor);
                RECT bounds{}; GetWindowRect(root, &bounds);
                require(abs((bounds.left + bounds.right) - (monitor.rcWork.left + monitor.rcWork.right)) <= 2 &&
                    abs((bounds.top + bounds.bottom) - (monitor.rcWork.top + monitor.rcWork.bottom)) <= 2, "not centered on cursor monitor");
                require(until([&] { return text(log).find(L"EARLY") != std::wstring::npos; }), "no live early output");
                require(window.state() == State::running, "early output arrived after completion");
                const auto beforeTyping = text(log);
                SendMessageW(log, WM_CHAR, L'Q', 0);
                require(text(log) == beforeTyping, "log accepted user edits");
                if (mode == Mode::install && !code) capture(root, L"running.png");
                SendMessageW(root, WM_CLOSE, 0, 0);
                require(IsWindow(root), "closed during operation");
                require(until([&] { return text(log).find(L"SPLIT-CR") != std::wstring::npos; }), "split CR was not displayed live");
                require(text(log).find(L"SPLIT-LF") == std::wstring::npos, "split LF arrived before the first batch was displayed");
                SetWindowPos(root, nullptr, 0, 0, 870, 670, SWP_NOMOVE | SWP_NOZORDER);
                checkGeometry(root, log);
                ShowWindow(root, SW_MAXIMIZE); pump(); checkGeometry(root, log);
                require(IsZoomed(root), "cannot maximize");
                ShowWindow(root, SW_RESTORE);
                const auto expected = code ? State::failed : State::succeeded;
                require(until([&] { return window.state() != State::running; }), "operation did not finish");
                require(window.state() == expected && text(root) == title(mode, expected), "wrong final state/title");
                checkColor(root, expected);
                if (mode == Mode::install) capture(root, code ? L"failed.png" : L"succeeded.png");
                const auto output = text(log);
                require(output.size() > 300000 && output.find(L"STDERR") != output.npos && output.find(L"FINAL") != output.npos, "large output was lost");
                require(output.find(L"HOST-LF\r\nSPLIT-CR\r\nSPLIT-LF\r\n") != output.npos,
                    "PowerShell LF or split CRLF did not become physical lines");
                require(std::count(output.begin(), output.end(), L'x') == 300001, "large log character count");
                const auto hardBreaks = std::count(output.begin(), output.end(), L'\n');
                require(hardBreaks >= 1000 && hardBreaks < 1020, "visual wrapping changed hard line breaks");
                SetWindowPos(root, nullptr, 0, 0, 600, 670, SWP_NOMOVE | SWP_NOZORDER);
                const auto narrowLines = SendMessageW(log, EM_GETLINECOUNT, 0, 0);
                SendMessageW(log, EM_SETSEL, 0, 0);
                SendMessageW(log, EM_SCROLLCARET, 0, 0);
                SCROLLINFO horizontal{sizeof(horizontal), SIF_RANGE | SIF_PAGE | SIF_POS};
                require(GetScrollInfo(log, SB_HORZ, &horizontal) && horizontal.nMax > static_cast<int>(horizontal.nPage),
                    "horizontal log range missing");
                const auto leftOffset = horizontal.nPos;
                SendMessageW(log, EM_LINESCROLL, 10, 0);
                require(GetScrollInfo(log, SB_HORZ, &horizontal) && horizontal.nPos > leftOffset,
                    "horizontal log scrolling failed");
                SetWindowPos(root, nullptr, 0, 0, 1100, 670, SWP_NOMOVE | SWP_NOZORDER);
                const auto wideLines = SendMessageW(log, EM_GETLINECOUNT, 0, 0);
                require(narrowLines == wideLines && wideLines >= 1000 && wideLines < 1020,
                    "long physical lines changed on resize");
                require(text(log) == output, "resizing changed log text");
                SendMessageW(log, EM_SETSEL, 0, 0);
                SendMessageW(log, EM_SCROLLCARET, 0, 0);
                const auto firstLine = SendMessageW(log, EM_GETFIRSTVISIBLELINE, 0, 0);
                SendMessageW(log, EM_LINESCROLL, 0, 10);
                require(SendMessageW(log, EM_GETFIRSTVISIBLELINE, 0, 0) > firstLine, "vertical log scrolling failed");
                std::cout << "Physical log lines narrow=" << narrowLines << " wide=" << wideLines << " DPI=" << GetDpiForWindow(root) << "\n";
                require(IsWindowVisible(root), "window closed after completion");
                SendMessageW(root, WM_APP + 41, 0, 0); // stale start is harmless
                pump(); require(text(log) == output, "late start changed output");
                SendMessageW(root, WM_CLOSE, 0, 0);
                require(!window.handle(), "manual close failed");
                std::cout << "PASS: mode=" << (mode == Mode::install ? "install" : "uninstall") << " exit=" << code << " live output, colors, titles, margins, resize, large log, lifetime\n";
            }
        }
        std::filesystem::remove_all(directory);
        {
            PackageWindow window(Mode::install);
            require(window.open(GetModuleHandleW(nullptr), directory), "missing script window did not open");
            require(until([&] { return window.state() != State::running; }), "missing script did not finish");
            require(window.state() == State::failed, "missing script was successful");
            require(text(window.logHandle()).find(L"script is missing") != std::wstring::npos, "missing script diagnostic absent");
            checkColor(window.handle(), State::failed);
            SendMessageW(window.handle(), WM_CLOSE, 0, 0);
        }
        std::cout << "5 native GUI scenarios passed. Production UAC and physical multi-DPI transitions require manual validation.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        std::error_code ignored; std::filesystem::remove_all(directory, ignored);
        return 1;
    }
}
