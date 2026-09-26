// HTTP headers dialog of the Wizard's first step, the GTK counterpart of
// apps/tray/http_headers_window (tray_UX.md "HTTP headers v prvnim kroku").
// One owned modal window "HTTP headers": plain multi-line text with real line ends,
// no wrapping, both scrollbars; red Reset on the left, Cancel and blue OK on the
// right. Reset only reloads the default text, OK validates through accept (the
// shared validator), Cancel/Escape/X discard the local copy. No IPC or disk access.
#pragma once
#include <gtk/gtk.h>

#include <functional>
#include <string>

#include "core/base/result.h"
#include "editor_window.h"  // DeferredCalls

namespace phpvpn::tray {
class HttpHeadersWindow {
public:
    HttpHeadersWindow() = default;
    ~HttpHeadersWindow() { close(false); }
    HttpHeadersWindow(const HttpHeadersWindow&) = delete;
    HttpHeadersWindow& operator=(const HttpHeadersWindow&) = delete;
    // False without owner or when the stored value cannot be displayed.
    bool open(GtkWindow* owner, const std::string& value, std::function<Status(std::string)> accept,
              std::function<void()> closed);
    // notify: run the closed callback (focus returns to the HTTP headers button).
    void close(bool notify = true);
    GtkWindow* window() const { return window_ ? GTK_WINDOW(window_) : nullptr; }

private:
    void command(int id);
    void onDestroyed();
    GtkWidget* window_ = nullptr;
    GtkWidget* field_ = nullptr;
    std::function<Status(std::string)> accept_;
    std::function<void()> closed_;
    // OK may show the validation error dialog: run it from an idle callback.
    DeferredCalls deferred_;
};
}  // namespace phpvpn::tray
