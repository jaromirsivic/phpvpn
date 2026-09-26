// UTF-8 presentation texts of the GTK tray. The shared English catalogue stays in
// apps/tray (ui_style.h, menu_model.h, reconnect_controller.h); this header only
// converts it to UTF-8 and adds the few Linux specific texts.
#pragma once
#include <string>
#include <string_view>

#include "core/base/utf8.h"
#include "menu_model.h"
#include "platform/linux/paths_linux.h"

namespace phpvpn::tray {
// The shared catalogue is fixed ASCII, so a failed conversion is a programming error
// that must stay visible rather than crash the tray: it yields an empty text.
inline std::string utf8(std::wstring_view text) {
    auto converted = toUtf8(text);
    return converted ? std::move(converted).value() : std::string{};
}

// Linux has no tray tooltip on every host, so the watcher hint names the exact
// GNOME command. Changing desktop settings stays with the user (plan 4.12).
inline constexpr std::string_view kWatcherHintTitle = "PHPVPN tray icon is not shown";
inline constexpr std::string_view kWatcherHintText =
    "This desktop does not show tray icons, so PHPVPN opened its control window instead. "
    "On GNOME, enable the AppIndicator extension with "
    "\"gnome-extensions enable appindicatorsupport@rgcjonas.gmail.com\" and log in again. "
    "You can reopen the control window from the PHPVPN launcher.";

// Linux configuration path shown in the configuration error texts, the counterpart of
// the Windows expectedConfigurationPath(). The service owns the file; the tray never
// touches it (tray_UX.md UX13).
inline std::string expectedConfigurationPath() { return std::string(kLinuxConfigDirectory) + "/config.json"; }

// Shown while a WP10 window has not been registered with the application yet.
inline std::string unavailableWindowText(MenuAction action) {
    std::string_view name = "This window";
    switch (action) {
        case MenuAction::wizard: name = "Configuration Wizard"; break;
        case MenuAction::contracts: name = "Communication contracts"; break;
        case MenuAction::edit: name = "Edit Config"; break;
        case MenuAction::info: name = "Server information"; break;
        case MenuAction::statistics: name = "Statistics"; break;
        case MenuAction::toggle: case MenuAction::separator: case MenuAction::exit: break;
    }
    return std::string(name) + " is not available in this build of the PHPVPN tray.";
}
inline constexpr std::string_view kErrorDetailUnavailableText =
    "The HTTP error detail is not available in this build of the PHPVPN tray.";
}  // namespace phpvpn::tray
