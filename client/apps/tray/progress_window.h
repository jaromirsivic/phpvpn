#pragma once
#include <windows.h>
#include "poll_schedule.h"
#include "progress_model.h"
#include "status_reader.h"

namespace phpvpn::tray {
class ProgressWindow {
public:
    ~ProgressWindow();
    void open(HINSTANCE instance);
    void onSnapshot(const TrayStatus& snapshot);
private:
    static LRESULT CALLBACK procedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    void paint();
    HWND window_ = nullptr;
    HINSTANCE instance_ = nullptr;
    ProgressModel model_;
    PollSchedule polling_{kProgressIntervalMs};
    StatusReader reader_;
};
}  // namespace phpvpn::tray
