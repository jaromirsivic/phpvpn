// Extra CSS of the Configuration Wizard and its owned windows (WP10a), generated
// from the portable palette of ui_style.h on top of the shared tray stylesheet
// (style.h): sidebar step markers, the invalid field state, tinted read-only text
// and the Wizard cards. Installed once per screen.
#pragma once
#include <string>

namespace phpvpn::tray::wizardStyle {
inline constexpr const char kSidebar[] = "phpvpn-wizard-sidebar";
inline constexpr const char kStepActive[] = "phpvpn-wizard-step-active";
inline constexpr const char kMarker[] = "phpvpn-wizard-marker";
inline constexpr const char kMarkerPrimary[] = "phpvpn-wizard-marker-primary";
inline constexpr const char kMarkerSuccess[] = "phpvpn-wizard-marker-success";
inline constexpr const char kMarkerError[] = "phpvpn-wizard-marker-error";
inline constexpr const char kMarkerPending[] = "phpvpn-wizard-marker-pending";
inline constexpr const char kTextPrimary[] = "phpvpn-wizard-text-primary";
inline constexpr const char kTextSuccess[] = "phpvpn-wizard-text-success";
inline constexpr const char kTextError[] = "phpvpn-wizard-text-error";
inline constexpr const char kTextPending[] = "phpvpn-wizard-text-pending";
inline constexpr const char kInvalid[] = "phpvpn-invalid";
inline constexpr const char kExample[] = "phpvpn-wizard-example";
inline constexpr const char kCounter[] = "phpvpn-wizard-counter";
inline constexpr const char kRawText[] = "phpvpn-raw-text";
inline constexpr const char kFooter[] = "phpvpn-wizard-footer";

std::string buildCss();
void install();
}  // namespace phpvpn::tray::wizardStyle
