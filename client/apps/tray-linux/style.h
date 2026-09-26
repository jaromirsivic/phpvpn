// GTK styling of the Linux tray: one GtkCssProvider generated from the portable
// palette and font roles of ui_style.h (tray_UX.md UX01), and cairo drawing of the
// state shields. Pixel parity with the Win32 layout is not required; content,
// order, texts, colors and behaviour are (plan 4.12 "Linuxove rozdily").
#pragma once
#include <gtk/gtk.h>

#include <string>

#include "progress_model.h"
#include "ui_style.h"

namespace phpvpn::tray::style {
// CSS classes shared by all tray windows (WP10 windows use the same names).
inline constexpr const char kWindow[] = "phpvpn-window";
inline constexpr const char kCard[] = "phpvpn-card";
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

// The Material Symbols shield_lock glyph (Apache License 2.0, see
// client/assets/icons/LICENSE-material-symbols.txt), the same shape as the tray
// icons: grey, orange and green from the palette.
ui::Color shieldColor(Shield shield);
void drawShield(cairo_t* cr, double x, double y, double size, Shield shield);
// A GtkDrawingArea of size x size DIP that paints the shield returned by current.
GtkWidget* newShieldArea(int size, Shield* current);
}  // namespace phpvpn::tray::style
