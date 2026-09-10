#pragma once
#include "statistics_layout.h"
#include "statistics_model.h"
#include "status_model.h"
#include "ui_resources.h"
#include <commctrl.h>
#include <windows.h>

namespace phpvpn::tray {
class StatisticsWindow {
  public:
    ~StatisticsWindow();
    void open(HINSTANCE instance, const TrayStatus &snapshot, bool unavailable = false);
    void onSnapshot(const TrayStatus &snapshot, bool unavailable = false);
    bool translate(MSG &message) const;
    HWND window() const { return window_; }

  private:
    static LRESULT CALLBACK procedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    void paint();
    void refresh();
    void layout();
    void updateResources();
    void scroll(int position);
    static LRESULT CALLBACK fieldProcedure(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);
    HWND window_ = nullptr;
    HINSTANCE instance_ = nullptr;
    StatisticsModel model_;
    UiResources resources_;
    StatisticsLayout layout_;
    HWND heading_ = nullptr, connectionStatus_ = nullptr, description_ = nullptr, freshness_ = nullptr;
    std::array<HWND, 3> titles_{};
    std::array<HWND, 10> labels_{}, values_{};
    int scroll_ = 0, scrollMax_ = 0;
    bool layingOut_ = false;
};
} // namespace phpvpn::tray
