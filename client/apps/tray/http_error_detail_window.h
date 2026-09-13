#pragma once
#include "ui_resources.h"
#include "core/wizard/http_diagnostic.h"

namespace phpvpn::tray {
class HttpErrorDetailWindow {
public:
    ~HttpErrorDetailWindow() { close(); }
    bool open(HINSTANCE instance, HWND owner, const HttpDiagnostic& diagnostic);
    void close();
    bool translate(MSG& message);
    HWND handle() const { return window_; }
private:
    void selectTab(int index);
    void updateFonts();
    void layout();
    static LRESULT CALLBACK procedure(HWND, UINT, WPARAM, LPARAM);
    HINSTANCE instance_ = nullptr;
    HWND window_ = nullptr, tabs_ = nullptr;
    std::array<HWND, 2> editors_{}, headings_{};
    std::array<std::wstring, 2> notes_{};
    UiResources resources_;
    Rect card_{};
    int selected_ = 0;
};
}
