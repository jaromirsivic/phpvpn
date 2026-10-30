// HTTP error detail window of the macOS tray, the counterpart of
// apps/tray/http_error_detail_window and apps/tray-linux/http_error_detail_window
// (client.md "HTTP detail neuspesneho Wizard probe"). Owned, non-modal window
// "Error detail" with exactly the tabs Request and Response, each a note and a
// read-only selectable monospace text without wrapping. No footer; it closes with
// the title bar close button or Escape. Opened by the Wizard and by the Connect
// progress window (TrayWindowHooks::openHttpErrorDetail).
#pragma once
#include <array>

#include "core/wizard/http_diagnostic.h"
#include "mac_ui.h"

namespace phpvpn::tray {
class HttpErrorDetailWindow {
public:
    HttpErrorDetailWindow() = default;
    ~HttpErrorDetailWindow() { close(); }
    HttpErrorDetailWindow(const HttpErrorDetailWindow&) = delete;
    HttpErrorDetailWindow& operator=(const HttpErrorDetailWindow&) = delete;
    // An open window is only activated. False without owner or with an invalid or
    // oversized diagnostic.
    bool open(NSWindow* owner, const HttpDiagnostic& diagnostic);
    void close();
    NSWindow* window() const { return window_; }
    bool active() const { return window_ && window_.keyWindow; }

private:
    PVWindow* window_ = nil;
    std::array<NSTextView*, 2> texts_{};
};
}  // namespace phpvpn::tray
