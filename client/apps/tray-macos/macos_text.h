// UTF-8 presentation texts of the macOS tray. The shared English catalogue stays in
// apps/tray (ui_style.h, menu_model.h, reconnect_controller.h, wizard_text.h); this
// header only converts it to UTF-8 and adds the few macOS specific texts (the
// configuration path and the launchd hint). GUI-free, used by the unit tests.
#pragma once
#include <string>
#include <string_view>

#include "core/base/utf8.h"
#include "menu_model.h"

namespace phpvpn::tray {
// The shared catalogue is fixed valid text, so a failed conversion is a programming error
// that must stay visible rather than crash the tray: it yields an empty text.
inline std::string utf8(std::wstring_view text) {
    auto converted = toUtf8(text);
    return converted ? std::move(converted).value() : std::string{};
}

// macOS configuration path shown in the configuration error texts, the counterpart of
// the Windows expectedConfigurationPath(). The service owns the file; the tray never
// touches it (tray_UX.md UX13).
inline constexpr std::string_view kMacConfigurationPath = "/Library/Application Support/phpvpn/config.json";
inline std::string expectedConfigurationPath() { return std::string(kMacConfigurationPath); }

// Added below "The PHPVPN service did not complete the command." The tray never
// starts or registers the service itself; launchctl is the administrator's step.
inline constexpr std::string_view kServiceStartHint =
    "If the PHPVPN service is not running, an administrator can start it in Terminal with:\n"
    "sudo launchctl bootstrap system /Library/LaunchDaemons/com.pilandrome.phpvpn.plist";

inline constexpr std::string_view kStatusItemAccessibilityName = "PHPVPN";

// Shown while a window has not been registered with the application yet.
inline std::string unavailableWindowText(MenuAction action) {
    std::string_view name = "This window";
    switch (action) {
        case MenuAction::wizard: name = "Configuration Wizard"; break;
        case MenuAction::contracts: name = "Communication contracts"; break;
        case MenuAction::edit: name = "Edit Config"; break;
        case MenuAction::info: name = "Server information"; break;
        case MenuAction::statistics: name = "Statistics"; break;
        case MenuAction::toggle: case MenuAction::separator: case MenuAction::about: case MenuAction::exit: break;
    }
    return std::string(name) + " is not available in this build of the PHPVPN tray.";
}
// NSAlert has a heading and a body: the version is the heading (as the Windows
// TaskDialog), licence and copyright the body.
inline std::string aboutHeadingText() { return utf8(aboutHeading); }
inline std::string aboutBodyText() { return utf8(aboutText); }
inline constexpr std::string_view kErrorDetailUnavailableText =
    "The HTTP error detail is not available in this build of the PHPVPN tray.";
}  // namespace phpvpn::tray
