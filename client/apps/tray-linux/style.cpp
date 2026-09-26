#include "style.h"
#include "gtk_util.h"

#include <cstdio>

#include "indicator_model.h"

namespace phpvpn::tray::style {
namespace {
void fontRule(std::string& css, const char* selector, ui::Font role) {
    const auto& spec = ui::fonts[static_cast<std::size_t>(role)];
    char line[256];
    std::snprintf(line, sizeof(line), ".%s { font-size: %dpx; font-weight: %d;%s }\n", selector, spec.height,
                  spec.weight, spec.mono ? " font-family: monospace;" : "");
    css += line;
}
}  // namespace

std::string hex(ui::Color color) {
    char text[8];
    std::snprintf(text, sizeof(text), "#%06X", static_cast<unsigned>(ui::colors[static_cast<std::size_t>(color)] & 0xFFFFFF));
    return text;
}

std::string buildCss() {
    using ui::Color;
    std::string css;
    auto rule = [&css](const std::string& text) { css += text; css += '\n'; };
    rule(std::string(".") + kWindow + " { background-color: " + hex(Color::background) + "; color: " + hex(Color::text) + "; }");
    rule(std::string(".") + kWindow + " label { color: " + hex(Color::text) + "; }");
    rule(std::string(".") + kCard + " { background-color: " + hex(Color::surface) + "; border: 1px solid " +
         hex(Color::border) + "; border-radius: " + std::to_string(ui::cardRadius) + "px; padding: " +
         std::to_string(ui::cardPadding) + "px; }");
    rule(std::string(".") + kCard + "." + kCompactCard + " { padding: " + std::to_string(ui::relatedGap) + "px; }");
    fontRule(css, kHeading, ui::Font::heading);
    fontRule(css, kEyebrow, ui::Font::subtitle);
    fontRule(css, kSubtitle, ui::Font::subtitle);
    fontRule(css, kCardTitle, ui::Font::card);
    fontRule(css, kLabel, ui::Font::label);
    fontRule(css, kValue, ui::Font::value);
    fontRule(css, kStrongValue, ui::Font::strongValue);
    fontRule(css, kMono, ui::Font::mono);
    fontRule(css, kHint, ui::Font::hint);
    rule(std::string(".") + kWindow + " label." + kEyebrow + ", ." + kWindow + " label." + kHint + " { color: " +
         hex(Color::secondaryText) + "; }");
    rule(std::string("entry.") + kReadOnly + " { background-color: " + hex(Color::disabledBackground) + "; color: " +
         hex(Color::secondaryText) + "; min-height: 36px; border-radius: 4px; }");
    rule(std::string(".") + kSeparator + " { background-color: " + hex(Color::separator) + "; min-height: 1px; }");
    rule(std::string(".") + kPanel + " { border-radius: " + std::to_string(ui::cardRadius) + "px; padding: " +
         std::to_string(ui::cardPadding) + "px; border: 1px solid; }");
    const struct { const char* name; Color text, background, border; } panels[]{
        {kPanelSuccess, Color::success, Color::successBackground, Color::successBorder},
        {kPanelError, Color::error, Color::errorBackground, Color::errorBorder},
        {kPanelWarning, Color::warning, Color::warningBackground, Color::warning}};
    for (const auto& panel : panels) {
        rule(std::string(".") + panel.name + " { background-color: " + hex(panel.background) + "; border-color: " +
             hex(panel.border) + "; }");
        rule(std::string(".") + kWindow + " ." + panel.name + " label { color: " + hex(panel.text) + "; }");
    }
    const struct { const char* name; Color color; } checks[]{
        {kCheckSuccess, Color::success}, {kCheckFailed, Color::error}, {kCheckPending, Color::warning}};
    for (const auto& check : checks)
        rule(std::string(".") + kWindow + " label." + check.name + " { color: " + hex(check.color) + "; }");
    const std::string button = "min-height: " + std::to_string(ui::buttonHeight) + "px; min-width: " +
        std::to_string(ui::buttonMinWidth) + "px; padding: 0 " + std::to_string(ui::buttonPadding) +
        "px; border-radius: 4px; background-image: none; box-shadow: none; text-shadow: none;";
    const struct { const char* name; Color text, normal, hover, pressed, border; } buttons[]{
        {kPrimaryButton, Color::primaryText, Color::primary, Color::primaryHover, Color::primaryPressed, Color::primary},
        {kSecondaryButton, Color::text, Color::surface, Color::buttonNeutralHover, Color::buttonNeutralPressed, Color::border},
        {kErrorButton, Color::primaryText, Color::error, Color::buttonResetHover, Color::buttonResetPressed, Color::error}};
    for (const auto& b : buttons) {
        const std::string selector = std::string("button.") + b.name;
        rule(selector + " { " + button + " color: " + hex(b.text) + "; background-color: " + hex(b.normal) +
             "; border: 1px solid " + hex(b.border) + "; }");
        rule(selector + " label { color: " + hex(b.text) + "; }");
        rule(selector + ":hover { background-color: " + hex(b.hover) + "; }");
        rule(selector + ":active { background-color: " + hex(b.pressed) + "; }");
        rule(selector + ":disabled { background-color: " + hex(Color::disabledBackground) + "; color: " +
             hex(Color::disabledText) + "; }");
        // The label rule above would keep white text on the light disabled fill.
        rule(selector + ":disabled label { color: " + hex(Color::disabledText) + "; }");
        // Inside a result panel the more specific panel label colour would otherwise
        // paint the button text in the panel colour (red on the red Error detail button).
        const std::string inPanel = std::string(".") + kWindow + " ." + kPanel + " " + selector;
        rule(inPanel + " label { color: " + hex(b.text) + "; }");
        rule(inPanel + ":disabled label { color: " + hex(Color::disabledText) + "; }");
    }
    // tray_UX.md UX03: visible focus of at least 2 DIP.
    rule(std::string(".") + kWindow + " button:focus, ." + kWindow + " entry:focus { outline-color: " +
         hex(Color::focus) + "; outline-style: solid; outline-width: 2px; outline-offset: 1px; }");
    return css;
}

void install() {
    static bool installed = false;
    if (installed) return;
    GdkScreen* screen = gdk_screen_get_default();
    if (!screen) return;
    GtkCssProvider* provider = gtk_css_provider_new();
    const auto css = buildCss();
    gtk_css_provider_load_from_data(provider, css.c_str(), static_cast<gssize>(css.size()), nullptr);
    gtk_style_context_add_provider_for_screen(screen, GTK_STYLE_PROVIDER(provider),
                                              GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(provider);
    installed = true;
}

void addClass(GtkWidget* widget, const char* name) { gtk_style_context_add_class(gtk_widget_get_style_context(widget), name); }
void removeClass(GtkWidget* widget, const char* name) { gtk_style_context_remove_class(gtk_widget_get_style_context(widget), name); }
void setClass(GtkWidget* widget, const char* name, bool enabled) {
    if (enabled) addClass(widget, name);
    else removeClass(widget, name);
}

void prepareWindow(GtkWindow* window, const char* title, Size clientDip) {
    install();
    gtk_window_set_title(window, title);
    gtk_window_set_icon_name(window, std::string(kApplicationIconName).c_str());
    gtk_window_set_default_size(window, clientDip.width, clientDip.height);
    addClass(GTK_WIDGET(window), kWindow);
}
}  // namespace phpvpn::tray::style
