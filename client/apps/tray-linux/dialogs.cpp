#include "dialogs.h"

#include <algorithm>
#include <string>
#include <vector>

#include "indicator_model.h"
#include "style.h"

namespace phpvpn::tray {
namespace {
std::vector<GtkWidget*>& openDialogs() {
    static std::vector<GtkWidget*> dialogs;
    return dialogs;
}
GtkMessageType messageType(DialogIcon icon) {
    switch (icon) {
        case DialogIcon::warning: return GTK_MESSAGE_WARNING;
        case DialogIcon::error: return GTK_MESSAGE_ERROR;
        case DialogIcon::information: break;
    }
    return GTK_MESSAGE_INFO;
}
// A destroyed parent must not be used as transient-for target.
GtkWindow* usableParent(GtkWindow* parent) {
    return parent && gtk_widget_get_visible(GTK_WIDGET(parent)) ? parent : nullptr;
}
int run(GtkWidget* dialog, std::string_view title) {
    const std::string text(title);
    gtk_window_set_title(GTK_WINDOW(dialog), text.c_str());
    gtk_window_set_icon_name(GTK_WINDOW(dialog), std::string(kApplicationIconName).c_str());
    gtk_window_set_skip_taskbar_hint(GTK_WINDOW(dialog), FALSE);
    openDialogs().push_back(dialog);
    const int response = gtk_dialog_run(GTK_DIALOG(dialog));
    auto& dialogs = openDialogs();
    dialogs.erase(std::remove(dialogs.begin(), dialogs.end(), dialog), dialogs.end());
    gtk_widget_destroy(dialog);
    return response;
}
}  // namespace

void showMessage(GtkWindow* parent, std::string_view text, std::string_view title, DialogIcon icon) {
    style::install();
    const std::string body(text);
    GtkWidget* dialog = gtk_message_dialog_new(usableParent(parent), GTK_DIALOG_MODAL, messageType(icon), GTK_BUTTONS_OK,
                                               "%s", body.c_str());
    run(dialog, title);
}

bool confirm(GtkWindow* parent, std::string_view text, std::string_view title, bool defaultYes) {
    style::install();
    const std::string body(text);
    GtkWidget* dialog = gtk_message_dialog_new(usableParent(parent), GTK_DIALOG_MODAL, GTK_MESSAGE_QUESTION,
                                               GTK_BUTTONS_YES_NO, "%s", body.c_str());
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), defaultYes ? GTK_RESPONSE_YES : GTK_RESPONSE_NO);
    if (GtkWidget* focus = gtk_dialog_get_widget_for_response(GTK_DIALOG(dialog), defaultYes ? GTK_RESPONSE_YES : GTK_RESPONSE_NO))
        gtk_widget_grab_focus(focus);
    return run(dialog, title) == GTK_RESPONSE_YES;
}

void closeAllDialogs() {
    // gtk_dialog_response ends the nested loop; run() then destroys the dialog.
    const auto dialogs = openDialogs();
    for (auto* dialog : dialogs) gtk_dialog_response(GTK_DIALOG(dialog), GTK_RESPONSE_NO);
}
}  // namespace phpvpn::tray
