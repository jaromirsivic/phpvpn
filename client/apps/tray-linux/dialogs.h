// Shared modal dialogs of the GTK tray, the counterpart of apps/tray/system_dialog.h.
// Both run a nested main loop like the Win32 MessageBox, so IPC deliveries and
// timers keep running while a dialog is open. Destructive confirmations default to
// No (tray_UX.md UX02).
#pragma once
#include <gtk/gtk.h>

#include <string_view>

namespace phpvpn::tray {
enum class DialogIcon { information, warning, error };

void showMessage(GtkWindow* parent, std::string_view text, std::string_view title, DialogIcon icon);
inline void showError(GtkWindow* parent, std::string_view text, std::string_view title = "PHPVPN") {
    showMessage(parent, text, title, DialogIcon::error);
}
// Yes/No question. defaultYes only for the non-destructive reconnect question.
bool confirm(GtkWindow* parent, std::string_view text, std::string_view title, bool defaultYes = false);
// Closes every dialog opened by this module (terminal session end), answering No.
void closeAllDialogs();
}  // namespace phpvpn::tray
