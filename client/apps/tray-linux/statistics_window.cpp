#include "statistics_window.h"
#include "gtk_util.h"

#include <algorithm>
#include <memory>
#include <string>

#include "style.h"
#include "tray_app.h"
#include "ui_style.h"

namespace phpvpn::tray {
namespace {
constexpr char kCounterClass[] = "phpvpn-statistics-counter";
constexpr char kDescriptionClass[] = "phpvpn-statistics-description";
constexpr char kFreshnessClass[] = "phpvpn-statistics-freshness";
constexpr int kCardGap = ui::sectionGap;
// style.h: .phpvpn-card padding (ui::cardPadding) plus its 1 px border.
constexpr int kCardInset = ui::cardPadding + 1;
// Horizontal padding of the counter field (CSS 0 4px) plus room for the caret.
constexpr int kValuePadding = 12;

void installCss() {
    static bool installed = false;
    GdkScreen* screen = gdk_screen_get_default();
    if (installed || !screen) return;
    using ui::Color;
    using style::hex;
    const std::string w = std::string(".") + style::kWindow;
    std::string css;
    css += w + " label." + kDescriptionClass + " { color: " + hex(Color::secondaryText) + "; }\n";
    css += w + " label." + kFreshnessClass + " { color: " + hex(Color::warning) + "; }\n";
    const auto& counter = ui::fonts[static_cast<std::size_t>(ui::Font::counter)];
    css += "entry." + std::string(kCounterClass) + " { font-size: " + std::to_string(counter.height) +
           "px; font-weight: " + std::to_string(counter.weight) + "; color: " + hex(Color::text) +
           "; background-color: transparent; background-image: none; border: none; box-shadow: none; min-height: " +
           std::to_string(ui::buttonHeight) + "px; padding: 0 4px; }\n";
    css += w + " entry." + kCounterClass + ":focus { outline-color: " + hex(Color::focus) +
           "; outline-style: solid; outline-width: 2px; outline-offset: -2px; }\n";
    GtkCssProvider* provider = gtk_css_provider_new();
    g_signal_connect(provider, "parsing-error", gtkCallback([](GtkCssProvider*, GtkCssSection*, GError* error, gpointer) {
        g_warning("PHPVPN statistics stylesheet: %s", error ? error->message : "parse error");
    }), nullptr);
    gtk_css_provider_load_from_data(provider, css.c_str(), static_cast<gssize>(css.size()), nullptr);
    gtk_style_context_add_provider_for_screen(screen, GTK_STYLE_PROVIDER(provider),
                                              GTK_STYLE_PROVIDER_PRIORITY_APPLICATION + 1);
    g_object_unref(provider);
    installed = true;
}

GtkWidget* label(const char* text, const char* cssClass, bool wrap) {
    GtkWidget* widget = gtk_label_new(text);
    gtk_label_set_xalign(GTK_LABEL(widget), 0.0f);
    if (wrap) {
        gtk_label_set_line_wrap(GTK_LABEL(widget), TRUE);
        gtk_label_set_line_wrap_mode(GTK_LABEL(widget), PANGO_WRAP_WORD_CHAR);
    }
    if (cssClass) style::addClass(widget, cssClass);
    return widget;
}

int naturalWidth(GtkWidget* widget) {
    int minimum = 0, natural = 0;
    gtk_widget_get_preferred_width(widget, &minimum, &natural);
    return natural;
}

// Pixel width of text in the widget's own font (the measured counter width).
int textWidth(GtkWidget* widget, const char* text) {
    PangoLayout* layout = gtk_widget_create_pango_layout(widget, text);
    int width = 0, height = 0;
    pango_layout_get_pixel_size(layout, &width, &height);
    g_object_unref(layout);
    return width;
}

void orient(GtkWidget* box, bool vertical) {
    gtk_orientable_set_orientation(GTK_ORIENTABLE(box), vertical ? GTK_ORIENTATION_VERTICAL : GTK_ORIENTATION_HORIZONTAL);
}

void setMargins(GtkWidget* widget, int margin) {
    gtk_widget_set_margin_top(widget, margin);
    gtk_widget_set_margin_bottom(widget, margin);
    gtk_widget_set_margin_start(widget, margin);
    gtk_widget_set_margin_end(widget, margin);
}
}  // namespace

StatisticsWindow::~StatisticsWindow() { endSession(); }

void StatisticsWindow::open(const TrayStatus& snapshot, bool unavailable) {
    if (!window_) {
        model_ = StatisticsModel{};
        build();
    }
    model_.update(snapshot, unavailable);
    refresh();
    gtk_window_present(GTK_WINDOW(window_));
}

void StatisticsWindow::onSnapshot(const TrayStatus& snapshot, bool unavailable) {
    if (!window_) return;
    model_.update(snapshot, unavailable);
    refresh();
}

void StatisticsWindow::endSession() {
    if (window_) gtk_widget_destroy(window_);  // onDestroyed clears the pointers
}

void StatisticsWindow::onDestroyed() {
    if (durationTimer_) g_source_remove(durationTimer_);
    if (placement_) g_source_remove(placement_);
    durationTimer_ = placement_ = 0;
    window_ = scroller_ = content_ = header_ = heading_ = status_ = freshness_ = top_ = nullptr;
    cards_ = {};
    trafficLines_ = {};
    dividers_ = {};
    rows_ = {};
    layoutWidth_ = -1;
    narrow_ = headerWrapped_ = false;
}

void StatisticsWindow::build() {
    installCss();
    window_ = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    style::prepareWindow(GTK_WINDOW(window_), std::string(kStatisticsWindowTitle).c_str(), ui::statisticsClient);
    gtk_widget_set_size_request(window_, 640, 480);
    // Escape and X close only this window; Enter has no mutating action.
    g_signal_connect(window_, "key-press-event", gtkCallback([](GtkWidget* widget, GdkEventKey* event, gpointer) -> gboolean {
        if (event->keyval != GDK_KEY_Escape) return FALSE;
        gtk_widget_destroy(widget);
        return TRUE;
    }), nullptr);
    g_signal_connect(window_, "set-focus", gtkCallback([](GtkWindow*, GtkWidget* focus, gpointer data) {
        static_cast<StatisticsWindow*>(data)->scrollToFocus(focus);
    }), this);
    g_signal_connect(window_, "destroy", gtkCallback([](GtkWidget*, gpointer data) {
        static_cast<StatisticsWindow*>(data)->onDestroyed();
    }), this);

    scroller_ = gtk_scrolled_window_new(nullptr, nullptr);
    // No horizontal page scroll: values have a one-character minimum width, so the
    // content never pins the window; relayout() stacks cards and long values (UX07).
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroller_), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(window_), scroller_);
    g_signal_connect(scroller_, "size-allocate", gtkCallback([](GtkWidget*, GdkRectangle*, gpointer data) {
        auto* self = static_cast<StatisticsWindow*>(data);
        // Re-attaching during allocation would recurse into layout; defer it.
        if (self->placement_) return;
        self->placement_ = g_idle_add([](gpointer raw) -> gboolean {
            auto* owner = static_cast<StatisticsWindow*>(raw);
            owner->placement_ = 0;
            owner->relayout();
            return G_SOURCE_REMOVE;
        }, self);
    }), this);
    content_ = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    setMargins(content_, ui::contentMargin);
    gtk_container_add(GTK_CONTAINER(scroller_), content_);

    header_ = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, ui::fieldGap);
    heading_ = label(std::string(kStatisticsHeading).c_str(), style::kHeading, false);
    status_ = label(std::string(kStatisticsDisconnected).c_str(), style::kCardTitle, false);
    gtk_widget_set_valign(status_, GTK_ALIGN_CENTER);
    gtk_box_pack_start(GTK_BOX(header_), heading_, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(header_), status_, FALSE, FALSE, 0);
    gtk_widget_set_margin_bottom(header_, ui::fieldGap);
    gtk_box_pack_start(GTK_BOX(content_), header_, FALSE, FALSE, 0);
    GtkWidget* description = label(std::string(kStatisticsDescription).c_str(), style::kSubtitle, true);
    style::addClass(description, kDescriptionClass);
    gtk_widget_set_margin_bottom(description, ui::sectionGap);
    gtk_box_pack_start(GTK_BOX(content_), description, FALSE, FALSE, 0);
    freshness_ = label("", style::kHint, true);
    style::addClass(freshness_, kFreshnessClass);
    gtk_widget_set_margin_bottom(freshness_, ui::relatedGap);
    gtk_widget_set_no_show_all(freshness_, TRUE);
    gtk_box_pack_start(GTK_BOX(content_), freshness_, FALSE, FALSE, 0);

    // Two top cards side by side (equal widths), the IPv4 card across the width.
    top_ = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, kCardGap);
    gtk_box_set_homogeneous(GTK_BOX(top_), TRUE);
    gtk_widget_set_margin_bottom(top_, kCardGap);
    gtk_box_pack_start(GTK_BOX(content_), top_, FALSE, FALSE, 0);
    for (std::size_t card = 0; card < cards_.size(); ++card) {
        cards_[card] = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
        style::addClass(cards_[card], style::kCard);
        gtk_widget_set_hexpand(cards_[card], TRUE);
        GtkWidget* title = label(std::string(kStatisticsCardTitles[card]).c_str(), style::kCardTitle, true);
        gtk_widget_set_margin_bottom(title, 16);
        gtk_box_pack_start(GTK_BOX(cards_[card]), title, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(card < 2 ? top_ : content_), cards_[card], card < 2, card < 2, 0);
    }
    // Equal IPv4 columns: every cell requests the widest natural width.
    GtkSizeGroup* columns = gtk_size_group_new(GTK_SIZE_GROUP_HORIZONTAL);
    for (std::size_t line = 0; line < trafficLines_.size(); ++line) {
        trafficLines_[line] = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, kCardGap / 2);
        gtk_box_pack_start(GTK_BOX(cards_[2]), trafficLines_[line], FALSE, FALSE, 0);
        dividers_[line] = gtk_separator_new(GTK_ORIENTATION_VERTICAL);
        style::addClass(dividers_[line], style::kSeparator);
        gtk_widget_set_no_show_all(dividers_[line], TRUE);
    }

    // Visual, tab and accessible order: statisticsFields (3 / 3 / 4, sent before received).
    for (std::size_t i = 0; i < rows_.size(); ++i) {
        auto& row = rows_[i];
        row.box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
        gtk_widget_set_hexpand(row.box, TRUE);
        row.line = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, ui::relatedGap);
        row.label = label(statisticsLabel(i).c_str(), style::kLabel, false);
        gtk_widget_set_valign(row.label, GTK_ALIGN_CENTER);
        // Native read-only field: focusable, selectable and copyable, never editable.
        row.value = gtk_entry_new();
        gtk_editable_set_editable(GTK_EDITABLE(row.value), FALSE);
        gtk_entry_set_has_frame(GTK_ENTRY(row.value), FALSE);
        gtk_entry_set_alignment(GTK_ENTRY(row.value), 1.0f);
        gtk_entry_set_text(GTK_ENTRY(row.value), "-");
        gtk_entry_set_width_chars(GTK_ENTRY(row.value), 1);
        style::addClass(row.value, kCounterClass);
        atk_object_set_name(gtk_widget_get_accessible(row.value), statisticsLabel(i).c_str());
        gtk_widget_set_halign(row.value, GTK_ALIGN_END);
        gtk_box_pack_start(GTK_BOX(row.line), row.label, TRUE, TRUE, 0);
        gtk_box_pack_start(GTK_BOX(row.line), row.value, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(row.box), row.line, FALSE, FALSE, 0);
        row.separator = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
        style::addClass(row.separator, style::kSeparator);
        gtk_widget_set_margin_top(row.separator, ui::fieldGap);
        gtk_widget_set_margin_bottom(row.separator, ui::fieldGap);
        gtk_widget_set_no_show_all(row.separator, TRUE);
        gtk_widget_set_visible(row.separator, statisticsSeparatorAfter(i, false));
        gtk_box_pack_start(GTK_BOX(row.box), row.separator, FALSE, FALSE, 0);
        const auto card = statisticsCard(i);
        if (card < 2) {
            gtk_box_pack_start(GTK_BOX(cards_[card]), row.box, FALSE, FALSE, 0);
            continue;
        }
        const auto cell = statisticsTrafficCell(i, false);
        const auto line = static_cast<std::size_t>(cell.row);
        if (cell.column == 1) gtk_box_pack_start(GTK_BOX(trafficLines_[line]), dividers_[line], FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(trafficLines_[line]), row.box, TRUE, TRUE, 0);
        gtk_size_group_add_widget(columns, row.box);
    }
    g_object_unref(columns);
    for (auto* divider : dividers_) gtk_widget_set_visible(divider, TRUE);
    gtk_widget_show_all(scroller_);
    // Local duration text only, no IPC.
    durationTimer_ = g_timeout_add_seconds(1, [](gpointer data) -> gboolean {
        static_cast<StatisticsWindow*>(data)->refresh();
        return G_SOURCE_CONTINUE;
    }, this);
}

void StatisticsWindow::refresh() {
    if (!window_) return;
    const auto texts = statisticsTexts(model_);
    bool changed = false;
    for (std::size_t i = 0; i < rows_.size(); ++i) {
        GtkEntry* entry = GTK_ENTRY(rows_[i].value);
        if (texts.values[i] == gtk_entry_get_text(entry)) continue;
        // All ten values change together from one complete snapshot.
        gtk_entry_set_text(entry, texts.values[i].c_str());
        // Natural width follows the text; the minimum stays one character, so a
        // short allocation scrolls the field horizontally instead of clipping the
        // card. relayout() stacks the row before that happens.
        gtk_entry_set_max_width_chars(entry, std::max<int>(1, static_cast<int>(g_utf8_strlen(texts.values[i].c_str(), -1))));
        changed = true;
    }
    if (texts.connectionStatus != gtk_label_get_text(GTK_LABEL(status_))) {
        gtk_label_set_text(GTK_LABEL(status_), texts.connectionStatus.c_str());
        changed = true;
    }
    style::setClass(status_, style::kCheckSuccess, texts.connected);
    style::setClass(status_, style::kCheckFailed, !texts.connected);
    if (texts.freshness != gtk_label_get_text(GTK_LABEL(freshness_))) {
        gtk_label_set_text(GTK_LABEL(freshness_), texts.freshness.c_str());
        gtk_widget_set_visible(freshness_, !texts.freshness.empty());
        changed = true;
    }
    if (changed) {
        layoutWidth_ = -1;
        relayout();
    }
}

void StatisticsWindow::relayout() {
    if (!scroller_) return;
    const int width = gtk_widget_get_allocated_width(scroller_);
    if (width <= 1 || width == layoutWidth_) return;
    layoutWidth_ = width;
    const bool narrow = statisticsNarrow(width);
    const int margin = std::min(narrow ? ui::compactMargin : ui::contentMargin, width / 8);
    setMargins(content_, margin);
    const int inner = std::max(1, width - 2 * margin);
    if (narrow != narrow_) {
        narrow_ = narrow;
        // Stacked top cards; the IPv4 lines become one column in the same order
        // (statisticsTrafficCell) and lose their column divider.
        orient(top_, narrow);
        gtk_box_set_homogeneous(GTK_BOX(top_), !narrow);
        for (std::size_t line = 0; line < trafficLines_.size(); ++line) {
            orient(trafficLines_[line], narrow);
            gtk_box_set_spacing(GTK_BOX(trafficLines_[line]), narrow ? 0 : kCardGap / 2);
            gtk_widget_set_visible(dividers_[line], !narrow);
        }
        for (std::size_t i = 0; i < rows_.size(); ++i)
            gtk_widget_set_visible(rows_[i].separator, statisticsSeparatorAfter(i, narrow));
    }
    const int status = std::max(naturalWidth(status_), textWidth(status_, std::string(kStatisticsDisconnected).c_str()));
    const bool wrapped = naturalWidth(heading_) + ui::fieldGap + status > inner;
    if (wrapped != headerWrapped_) {
        headerWrapped_ = wrapped;
        orient(header_, wrapped);
    }
    const int topAvailable = (narrow ? inner : (inner - kCardGap) / 2) - 2 * kCardInset;
    const int trafficAvailable = narrow ? inner - 2 * kCardInset : (inner - 2 * kCardInset - kCardGap) / 2;
    for (std::size_t i = 0; i < rows_.size(); ++i) {
        auto& row = rows_[i];
        const int available = std::max(1, statisticsCard(i) < 2 ? topAvailable : trafficAvailable);
        const int value = textWidth(row.value, gtk_entry_get_text(GTK_ENTRY(row.value))) + kValuePadding;
        const bool stacked = statisticsStacked(value, naturalWidth(row.label), available);
        if (stacked == row.stacked) continue;
        row.stacked = stacked;
        // A long value moves under its label and keeps horizontal scrolling.
        orient(row.line, stacked);
        gtk_widget_set_halign(row.value, stacked ? GTK_ALIGN_FILL : GTK_ALIGN_END);
    }
}

void StatisticsWindow::scrollToFocus(GtkWidget* focus) {
    // A focused value is scrolled into view (revealScrollPosition on Windows).
    if (!focus || !content_ || !gtk_widget_is_ancestor(focus, content_)) return;
    int x = 0, y = 0;
    if (!gtk_widget_translate_coordinates(focus, content_, 0, 0, &x, &y)) return;
    GtkAdjustment* adjustment = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(scroller_));
    const double value = gtk_adjustment_get_value(adjustment), page = gtk_adjustment_get_page_size(adjustment);
    const int height = gtk_widget_get_allocated_height(focus);
    if (y < value) gtk_adjustment_set_value(adjustment, std::max(0, y - ui::fieldGap));
    else if (y + height > value + page) gtk_adjustment_set_value(adjustment, y + height + ui::fieldGap - page);
}

void installStatisticsWindow(TrayApp& app) {
    auto window = std::make_shared<StatisticsWindow>();
    StatisticsWindow* raw = window.get();
    app.registerParticipant({raw, [raw] { return raw->window(); }});
    app.hooks().openStatistics = [window](const TrayStatus& snapshot, bool unavailable) {
        window->open(snapshot, unavailable);
    };
    app.addStatusListener([window](const TrayStatus& snapshot, bool unavailable) {
        window->onSnapshot(snapshot, unavailable);
    });
}
}  // namespace phpvpn::tray
