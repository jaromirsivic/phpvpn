#include "style.h"
#include "gtk_util.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <string_view>

#include "indicator_model.h"

namespace phpvpn::tray::style {
namespace {
// Material Symbols Outlined `shield_lock`, 24px grade 0 weight 400, the glyph of
// client/tools/make_icons.py. The viewBox is "0 -960 960 960".
constexpr std::string_view kShieldLockPath =
    "M480-80q-139-35-229.5-159.5T160-516v-244l320-120 320 120v244q0 152-90.5 "
    "276.5T480-80Zm0-84q104-33 172-132t68-220v-189l-240-90-240 90v189q0 121 68 "
    "220t172 132Zm0-316Zm-80 160h160q17 0 28.5-11.5T600-360v-120q0-17-11.5-28.5T560-520v-40q0-33-23.5-56.5T480-640q-33 "
    "0-56.5 23.5T400-560v40q-17 0-28.5 11.5T360-480v120q0 17 11.5 28.5T400-320Zm40-200v-40q0-17 "
    "11.5-28.5T480-600q17 0 28.5 11.5T520-560v40h-80Z";

void rgb(cairo_t* cr, ui::Color color) {
    const auto value = ui::colors[static_cast<std::size_t>(color)];
    cairo_set_source_rgb(cr, ((value >> 16) & 0xFF) / 255.0, ((value >> 8) & 0xFF) / 255.0, (value & 0xFF) / 255.0);
}

// Minimal SVG path interpreter for the commands the glyph uses (M L H V Q T C Z in
// absolute and relative form). Quadratic segments become cubic cairo curves.
class PathReader {
public:
    explicit PathReader(std::string_view path) : path_(path) {}
    void trace(cairo_t* cr) {
        char command = 0;
        double x = 0, y = 0, startX = 0, startY = 0, controlX = 0, controlY = 0;
        bool quadratic = false;
        while (skip(), position_ < path_.size()) {
            if (std::isalpha(static_cast<unsigned char>(path_[position_]))) command = path_[position_++];
            else if (!command) return;
            const bool relative = std::islower(static_cast<unsigned char>(command)) != 0;
            const double baseX = relative ? x : 0, baseY = relative ? y : 0;
            bool nextQuadratic = false;
            switch (std::toupper(static_cast<unsigned char>(command))) {
                case 'M': {
                    double px = 0, py = 0;
                    if (!number(px) || !number(py)) return;
                    x = startX = baseX + px; y = startY = baseY + py;
                    cairo_move_to(cr, x, y);
                    command = relative ? 'l' : 'L';  // implicit lineto after moveto
                    break;
                }
                case 'L': {
                    double px = 0, py = 0;
                    if (!number(px) || !number(py)) return;
                    x = baseX + px; y = baseY + py;
                    cairo_line_to(cr, x, y);
                    break;
                }
                case 'H': {
                    double px = 0;
                    if (!number(px)) return;
                    x = baseX + px;
                    cairo_line_to(cr, x, y);
                    break;
                }
                case 'V': {
                    double py = 0;
                    if (!number(py)) return;
                    y = baseY + py;
                    cairo_line_to(cr, x, y);
                    break;
                }
                case 'Q': case 'T': {
                    double qx = 0, qy = 0, px = 0, py = 0;
                    if (std::toupper(static_cast<unsigned char>(command)) == 'Q') {
                        if (!number(qx) || !number(qy)) return;
                        qx += baseX; qy += baseY;
                    } else {
                        qx = quadratic ? 2 * x - controlX : x;
                        qy = quadratic ? 2 * y - controlY : y;
                    }
                    if (!number(px) || !number(py)) return;
                    px += baseX; py += baseY;
                    cairo_curve_to(cr, x + 2.0 / 3.0 * (qx - x), y + 2.0 / 3.0 * (qy - y),
                                   px + 2.0 / 3.0 * (qx - px), py + 2.0 / 3.0 * (qy - py), px, py);
                    controlX = qx; controlY = qy; x = px; y = py;
                    nextQuadratic = true;
                    break;
                }
                case 'C': {
                    double c[6]{};
                    for (auto& value : c) if (!number(value)) return;
                    cairo_curve_to(cr, baseX + c[0], baseY + c[1], baseX + c[2], baseY + c[3], baseX + c[4], baseY + c[5]);
                    x = baseX + c[4]; y = baseY + c[5];
                    break;
                }
                case 'Z':
                    cairo_close_path(cr);
                    x = startX; y = startY;
                    command = 0;
                    break;
                default: return;
            }
            quadratic = nextQuadratic;
        }
    }

private:
    void skip() {
        while (position_ < path_.size() && (path_[position_] == ' ' || path_[position_] == ',')) ++position_;
    }
    bool number(double& value) {
        skip();
        const std::size_t begin = position_;
        if (position_ < path_.size() && (path_[position_] == '-' || path_[position_] == '+')) ++position_;
        bool dot = false, digits = false;
        while (position_ < path_.size()) {
            const char c = path_[position_];
            if (std::isdigit(static_cast<unsigned char>(c))) { digits = true; ++position_; }
            else if (c == '.' && !dot) { dot = true; ++position_; }
            else break;
        }
        if (!digits) { position_ = begin; return false; }
        value = std::strtod(std::string(path_.substr(begin, position_ - begin)).c_str(), nullptr);
        return true;
    }
    std::string_view path_;
    std::size_t position_ = 0;
};

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

ui::Color shieldColor(Shield shield) {
    switch (shield) {
        case Shield::green: return ui::Color::success;
        case Shield::orange: return ui::Color::warning;
        case Shield::grey: break;
    }
    return ui::Color::disabledText;
}

void drawShield(cairo_t* cr, double x, double y, double size, Shield shield) {
    cairo_save(cr);
    cairo_translate(cr, x, y);
    cairo_scale(cr, size / 960.0, size / 960.0);
    cairo_translate(cr, 0, 960.0);
    cairo_new_path(cr);
    PathReader(kShieldLockPath).trace(cr);
    cairo_set_fill_rule(cr, CAIRO_FILL_RULE_WINDING);
    rgb(cr, shieldColor(shield));
    cairo_fill(cr);
    cairo_restore(cr);
}

GtkWidget* newShieldArea(int size, Shield* current) {
    GtkWidget* area = gtk_drawing_area_new();
    gtk_widget_set_size_request(area, size, size);
    gtk_widget_set_valign(area, GTK_ALIGN_CENTER);
    g_signal_connect(area, "draw", gtkCallback([](GtkWidget* widget, cairo_t* cr, gpointer data) -> gboolean {
        const int width = gtk_widget_get_allocated_width(widget), height = gtk_widget_get_allocated_height(widget);
        const double side = width < height ? width : height;
        drawShield(cr, (width - side) / 2.0, (height - side) / 2.0, side, *static_cast<Shield*>(data));
        return FALSE;
    }), current);
    return area;
}
}  // namespace phpvpn::tray::style
