#pragma once
#include <windows.h>
#include "status_model.h"

namespace phpvpn::tray {
class StatisticsWindow {
public:
    ~StatisticsWindow();
    void open(HINSTANCE instance, const TrayStatus& snapshot);
    void onSnapshot(const TrayStatus& snapshot);
private:
    static LRESULT CALLBACK procedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    void paint();
    HWND window_ = nullptr;
    TrayStatus snapshot_;
};
}  // namespace phpvpn::tray
