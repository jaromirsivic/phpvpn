// GTK styling of the Linux tray: one GtkCssProvider generated from the portable
// palette and font roles of ui_style.h (tray_UX.md UX01). Pixel parity with the
// Win32 layout is not required; content, order, texts, colors and behaviour are
// (plan 4.12 "Linuxove rozdily").
#pragma once
#include <gtk/gtk.h>

#include <string>

#include "layout.h"
#include "ui_style.h"

namespace phpvpn::tray::style {
// CSS classes shared by all tray windows (WP10 windows use the same names).
inline constexpr const char kWindow[] = "phpvpn-window";
inline constexpr const char kCard[] = "phpvpn-card";
// Together with kCard: the same card with the smaller padding of compact windows.
inline constexpr const char kCompactCard[] = "phpvpn-card-compact";
inline constexpr const char kEyebrow[] = "phpvpn-eyebrow";
inline constexpr const char kHeading[] = "phpvpn-heading";
inline constexpr const char kCardTitle[] = "phpvpn-card-title";
inline constexpr const char kSubtitle[] = "phpvpn-subtitle";
inline constexpr const char kLabel[] = "phpvpn-label";
inline constexpr const char kValue[] = "phpvpn-value";
inline constexpr const char kStrongValue[] = "phpvpn-strong-value";
inline constexpr const char kMono[] = "phpvpn-mono";
inline constexpr const char kHint[] = "phpvpn-hint";
inline constexpr const char kReadOnly[] = "phpvpn-readonly";
inline constexpr const char kSeparator[] = "phpvpn-separator";
inline constexpr const char kPanel[] = "phpvpn-panel";
inline constexpr const char kPanelSuccess[] = "phpvpn-panel-success";
inline constexpr const char kPanelError[] = "phpvpn-panel-error";
inline constexpr const char kPanelWarning[] = "phpvpn-panel-warning";
inline constexpr const char kCheckSuccess[] = "phpvpn-check-success";
inline constexpr const char kCheckFailed[] = "phpvpn-check-failed";
inline constexpr const char kCheckPending[] = "phpvpn-check-pending";
inline constexpr const char kPrimaryButton[] = "phpvpn-primary";
inline constexpr const char kSecondaryButton[] = "phpvpn-secondary";
inline constexpr const char kErrorButton[] = "phpvpn-error";

// "#RRGGBB" of a palette token.
std::string hex(ui::Color color);
// The complete stylesheet, generated from ui_style.h only.
std::string buildCss();
// Installs the stylesheet for the default screen once. Safe to call repeatedly.
void install();

void addClass(GtkWidget* widget, const char* name);
void removeClass(GtkWidget* widget, const char* name);
void setClass(GtkWidget* widget, const char* name, bool enabled);

// Common window setup: application icon, background class and default size in DIP.
void prepareWindow(GtkWindow* window, const char* title, Size clientDip);
}  // namespace phpvpn::tray::style
