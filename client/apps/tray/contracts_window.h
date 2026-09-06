#pragma once
#include <windows.h>

namespace phpvpn::tray {
// One empty, closable window per tray instance. Callers own the one-time
// configuration check. Showing after confirmed Finish performs no disk/IPC I/O.
class ContractsWindow {
public:
    ContractsWindow() = default;
    ContractsWindow(const ContractsWindow&) = delete;
    ContractsWindow& operator=(const ContractsWindow&) = delete;
    ~ContractsWindow();
    bool show(HINSTANCE instance);
    bool isOpen() const { return window_ != nullptr; }
    void close();
private:
    static LRESULT CALLBACK procedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    HWND window_ = nullptr;
};
} // namespace phpvpn::tray
