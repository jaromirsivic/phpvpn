#pragma once
#include "poll_schedule.h"
#include "progress_details.h"
#include "progress_layout.h"
#include "progress_model.h"
#include "status_reader.h"
#include "ui_resources.h"
#include <commctrl.h>
#include <windows.h>

namespace phpvpn::tray {
class ProgressWindow {
  public:
    explicit ProgressWindow(std::wstring pipeName) : reader_(pipeName), detailsReader_(std::move(pipeName)) {}
    ~ProgressWindow();
    void open(HINSTANCE instance, bool disconnect = false);
    void onDisconnectReply(const TrayStatus& snapshot);
    bool disconnectPending() const {
        return window_ && disconnect_ && model_.view().stage == ProgressStage::connecting;
    }
    void endSession();
    void onSnapshot(const TrayStatus &snapshot);
    bool translate(MSG &message) const;
    HWND window() const { return window_; }
    const ProgressModel &model() const { return model_; }

  private:
    static LRESULT CALLBACK procedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    void paint();
    static LRESULT CALLBACK headingProcedure(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);
    static LRESULT CALLBACK fieldProcedure(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);
    void refresh();
    void layout();
    void updateResources();
    void scroll(int position);
    void reveal(HWND field);
    HWND window_ = nullptr;
    HINSTANCE instance_ = nullptr;
    ProgressModel model_;
    PollSchedule polling_{kProgressIntervalMs};
    StatusReader reader_;
    CommandReader detailsReader_;
    ProgressDetails details_;
    UiResources resources_;
    ProgressLayout layout_;
    std::array<HWND, 2> labels_{}, fields_{};
    std::array<HWND, 3> checks_{}, checkLabels_{}, checkIcons_{};
    HWND eyebrow_ = nullptr, heading_ = nullptr, description_ = nullptr;
    HWND detailsHeading_ = nullptr, checksHeading_ = nullptr, panelHeading_ = nullptr, panelAction_ = nullptr,
         countdown_ = nullptr;
    int scroll_ = 0, scrollMax_ = 0;
    bool readPending_ = false, layingOut_ = false;
    bool disconnect_ = false, waitingForReply_ = false;
};
} // namespace phpvpn::tray
