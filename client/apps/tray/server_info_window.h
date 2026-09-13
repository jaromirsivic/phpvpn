#pragma once
#include "core/presentation/server_info_model.h"
#include "status_reader.h"
#include "ui_resources.h"
#include <commctrl.h>
#include <oleacc.h>
namespace phpvpn::tray {
class ServerInfoWindow {
  public:
    explicit ServerInfoWindow(std::wstring pipe) : reader_(std::move(pipe)) {}
    ~ServerInfoWindow() { endSession(); }
    void open(HINSTANCE instance);
    void endSession();
    HWND window() const { return window_; }
    bool translate(MSG &message);

  private:
    static LRESULT CALLBACK procedure(HWND, UINT, WPARAM, LPARAM);
    static LRESULT CALLBACK tableProcedure(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);
    static LRESULT CALLBACK headerProcedure(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);
    void request();
    void finish();
    void display();
    void layout();
    void fonts();
    void page(int value);
    void selectTab();
    void destroyPager();
    CommandReader reader_;
    HWND window_ = nullptr, heading_ = nullptr, tabs_ = nullptr, table_ = nullptr, raw_ = nullptr, refresh_ = nullptr,
         ok_ = nullptr, notice_ = nullptr;
    HINSTANCE instance_ = nullptr;
    UiResources resources_;
    std::optional<ServerInfoModel> model_;
    std::vector<std::pair<HWND, int>> pager_;
    int page_ = 0;
    Rect tableBounds_{};
    HIMAGELIST rowHeightImages_ = nullptr;
    bool pending_ = false;
    std::uint64_t generation_ = 0, requestGeneration_ = 0;
    IAccPropServices *accessibility_ = nullptr;
    bool comInitialised_ = false;
};
} // namespace phpvpn::tray
