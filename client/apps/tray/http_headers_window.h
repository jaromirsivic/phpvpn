#pragma once
#include "ui_resources.h"
#include "http_headers_layout.h"
#include "core/base/result.h"
#include <functional>

namespace phpvpn::tray {
// Owned modal window on the application's existing message pump. No IPC or disk access.
class HttpHeadersWindow {
public:
    ~HttpHeadersWindow() { close(false); }
    bool open(HINSTANCE instance, HWND owner, const std::string& value,
        std::function<Status(std::string)> accept, std::function<void()> closed);
    void close(bool notify = true);
    bool translate(MSG& message);
    HWND handle() const { return window_; }
private:
    static LRESULT CALLBACK procedure(HWND, UINT, WPARAM, LPARAM);
    void command(int id);
    void updateFonts(int dpi = 0);
    void layout();
    HWND window_ = nullptr, owner_ = nullptr, field_ = nullptr;
    HINSTANCE instance_ = nullptr;
    UiResources resources_;
    HttpHeadersLayout geometry_{};
    bool restoreOwner_ = false;
    std::function<Status(std::string)> accept_;
    std::function<void()> closed_;
};
}
