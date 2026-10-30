// HTTP headers dialog of the Wizard's first step (tray_UX.md "HTTP headers v prvnim
// kroku"), the AppKit counterpart of apps/tray/http_headers_window and
// apps/tray-linux/http_headers_window. One owned window "HTTP headers", shown as a
// resizable sheet of the Wizard (window modal): plain multi-line text with real line
// ends, no wrapping, both scrollbars; red Reset on the left, Cancel and blue OK on
// the right. Reset only reloads the default text, OK validates through accept (the
// shared validator), Cancel/Escape discard the local copy. No IPC or disk access.
#pragma once
#include <functional>
#include <string>

#include "core/base/result.h"
#include "mac_ui.h"

namespace phpvpn::tray {
class HttpHeadersWindow {
public:
    HttpHeadersWindow() = default;
    ~HttpHeadersWindow() { close(false); }
    HttpHeadersWindow(const HttpHeadersWindow&) = delete;
    HttpHeadersWindow& operator=(const HttpHeadersWindow&) = delete;
    // False without owner or when the stored value cannot be displayed.
    bool open(NSWindow* owner, const std::string& value, std::function<Status(std::string)> accept,
              std::function<void()> closed);
    // notify: run the closed callback (focus returns to the HTTP headers button).
    void close(bool notify = true);
    NSWindow* window() const { return window_; }

private:
    enum class Command { reset, cancel, ok };
    void command(Command id);
    PVWindow* window_ = nil;
    PVStackView* root_ = nil;
    NSTextView* field_ = nil;
    std::function<Status(std::string)> accept_;
    std::function<void()> closed_;
    // OK may show the validation error alert: run it after the click.
    mac::Deferred deferred_;
};
}  // namespace phpvpn::tray
