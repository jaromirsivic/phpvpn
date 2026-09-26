// HTTP error detail window of the GTK tray, the counterpart of
// apps/tray/http_error_detail_window (client.md "HTTP detail neuspesneho Wizard probe").
// Owned, non-modal window "Error detail" with exactly the tabs Request and Response,
// each a note and a read-only selectable monospace text without wrapping. No footer;
// it closes with the title bar X. Opened by the Wizard and by the Connect progress
// window (TrayWindowHooks::openHttpErrorDetail).
#pragma once
#include <gtk/gtk.h>

#include <array>

#include "core/wizard/http_diagnostic.h"

namespace phpvpn::tray {
class HttpErrorDetailWindow {
public:
    HttpErrorDetailWindow() = default;
    ~HttpErrorDetailWindow() { close(); }
    HttpErrorDetailWindow(const HttpErrorDetailWindow&) = delete;
    HttpErrorDetailWindow& operator=(const HttpErrorDetailWindow&) = delete;
    // An open window is only activated. False without owner or with an invalid
    // or oversized diagnostic.
    bool open(GtkWindow* owner, const HttpDiagnostic& diagnostic);
    void close();
    GtkWindow* window() const { return window_ ? GTK_WINDOW(window_) : nullptr; }
    bool active() const { return window_ && gtk_window_is_active(GTK_WINDOW(window_)); }

private:
    void onDestroyed();
    GtkWidget* window_ = nullptr;
    std::array<GtkTextBuffer*, 2> buffers_{};
};
}  // namespace phpvpn::tray
