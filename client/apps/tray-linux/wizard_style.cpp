#include "wizard_style.h"

#include <gtk/gtk.h>

#include "style.h"
#include "ui_style.h"

namespace phpvpn::tray::wizardStyle {
std::string buildCss() {
    using ui::Color;
    using style::hex;
    std::string css;
    auto rule = [&css](const std::string& text) { css += text; css += '\n'; };
    const std::string window = std::string(".") + style::kWindow;
    rule(std::string(".") + kSidebar + " { background-color: " + hex(Color::background) + "; }");
    rule(std::string(".") + kStepActive + " { background-color: " + hex(Color::selectedStep) + "; border-radius: 4px; }");
    rule(window + " label." + kMarker + " { min-width: 24px; min-height: 24px; border-radius: 12px; color: " +
         hex(Color::primaryText) + "; font-weight: 600; }");
    const struct { const char* marker; const char* text; Color color; } states[]{
        {kMarkerPrimary, kTextPrimary, Color::primary}, {kMarkerSuccess, kTextSuccess, Color::success},
        {kMarkerError, kTextError, Color::error}, {kMarkerPending, kTextPending, Color::secondaryText}};
    for (const auto& state : states) {
        rule(window + " label." + state.marker + " { background-color: " + hex(state.color) + "; }");
        rule(window + " label." + state.text + " { color: " + hex(state.color) + "; }");
    }
    // tray_UX.md UX02: an invalid field has a red border (plus the error text).
    rule(std::string("entry.") + kInvalid + " { border-color: " + hex(Color::error) + "; box-shadow: inset 0 0 0 1px " +
         hex(Color::error) + "; }");
    rule(window + " label." + kExample + " { background-color: " + hex(Color::disabledBackground) +
         "; color: " + hex(Color::secondaryText) + "; padding: 6px 8px; border-radius: 4px; }");
    rule(window + " label." + kCounter + " { font-size: " +
         std::to_string(ui::fonts[static_cast<std::size_t>(ui::Font::counter)].height) + "px; font-weight: 600; }");
    rule(std::string("textview.") + kRawText + " text { background-color: " + hex(Color::disabledBackground) + "; color: " +
         hex(Color::text) + "; }");
    rule(std::string(".") + kFooter + " { background-color: " + hex(Color::background) + "; }");
    rule(window + " entry { min-height: " + std::to_string(ui::buttonHeight) + "px; }");
    return css;
}

void install() {
    static bool installed = false;
    if (installed) return;
    style::install();
    GdkScreen* screen = gdk_screen_get_default();
    if (!screen) return;
    GtkCssProvider* provider = gtk_css_provider_new();
    const auto css = buildCss();
    gtk_css_provider_load_from_data(provider, css.c_str(), static_cast<gssize>(css.size()), nullptr);
    // One step above the shared tray stylesheet so the Wizard states win.
    gtk_style_context_add_provider_for_screen(screen, GTK_STYLE_PROVIDER(provider),
                                              GTK_STYLE_PROVIDER_PRIORITY_APPLICATION + 1);
    g_object_unref(provider);
    installed = true;
}
}  // namespace phpvpn::tray::wizardStyle
