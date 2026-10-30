#include "mac_statistics_window.h"

#include <algorithm>
#include <memory>
#include <string>

#include "mac_tray_app.h"
#include "macos_text.h"

namespace phpvpn::tray {
namespace {
constexpr std::string_view kStatisticsWindowTitle = "PHPVPN Statistics";
constexpr std::string_view kStatisticsHeading = "Statistics";
constexpr std::string_view kStatisticsDescription = "Read-only global service counters for the current VPN connection.";
constexpr std::string_view kStatisticsCardTitles[3]{"Data polling", "Serialized data", "IPv4 traffic"};
constexpr int kCardGap = ui::sectionGap;

// Card of a field in the 3 / 3 / 4 order of statisticsFields.
constexpr std::size_t statisticsCard(std::size_t index) { return index < 3 ? 0 : index < 6 ? 1 : 2; }
// Horizontal separator under a row, exactly as StatisticsWindow::paint draws them.
constexpr bool statisticsSeparatorAfter(std::size_t index, bool narrow) {
    switch (index) {
        case 0: case 1: case 3: case 4: case 6: case 7: return true;
        case 8: return narrow;
        default: return false;
    }
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
    mac::presentWindow(window_);
}

void StatisticsWindow::onSnapshot(const TrayStatus& snapshot, bool unavailable) {
    if (!window_) return;
    model_.update(snapshot, unavailable);
    refresh();
}

void StatisticsWindow::endSession() { close(); }

void StatisticsWindow::close() {
    durationTimer_.stop();
    if (!window_) return;
    PVWindow* window = window_;
    window_ = nil;
    mac::closeWindow(window);
    root_ = nil;
    scroller_ = nil;
    content_ = nil;
    header_ = nil;
    status_ = nil;
    freshness_ = nil;
    top_ = nil;
    trafficLines_ = {};
    rows_ = {};
    narrow_ = false;
}

void StatisticsWindow::build() {
    window_ = mac::makeWindow(kStatisticsWindowTitle, ui::statisticsClient, Size{480, 320});
    root_ = mac::makeRoot(window_);
    content_ = mac::makeStack(true, 0, NSEdgeInsetsMake(ui::contentMargin, ui::contentMargin, ui::contentMargin,
                                                        ui::contentMargin));
    scroller_ = [[PVScrollView alloc] initWithStack:content_];
    [root_ addView:scroller_ item:mac::flexibleItem()];

    header_ = mac::makeStack(false, ui::fieldGap);
    header_.wrapsWhenNarrow = YES;
    [header_ addView:mac::makeLabel(kStatisticsHeading, ui::Font::heading) item:mac::natural()];
    status_ = mac::makeLabel("(Disconnected)", ui::Font::card, ui::Color::error);
    [header_ addView:status_ item:mac::natural()];
    [content_ addView:header_];
    [content_ addView:mac::makeLabel(kStatisticsDescription, ui::Font::subtitle, ui::Color::secondaryText, true)
                 item:mac::withGap(mac::StackItem{}, ui::fieldGap)];
    freshness_ = mac::makeLabel("", ui::Font::hint, ui::Color::warning, true);
    [content_ addView:freshness_ item:mac::withGap(mac::StackItem{}, ui::sectionGap)];

    // Two top cards side by side (equal widths), the IPv4 card across the width.
    top_ = mac::makeStack(false, kCardGap);
    top_.homogeneous = YES;
    [content_ addView:top_ item:mac::withGap(mac::StackItem{}, ui::relatedGap)];
    std::array<PVStackView*, 3> cards{};
    for (std::size_t card = 0; card < cards.size(); ++card) {
        cards[card] = mac::makeCard(ui::cardPadding, 0);
        [cards[card] addView:mac::makeLabel(kStatisticsCardTitles[card], ui::Font::card, ui::Color::text, true)];
        mac::StackItem item;
        item.fill = true;
        item.top = true;
        if (card < 2) [top_ addView:cards[card] item:item];
        else [content_ addView:cards[card] item:mac::withGap(mac::StackItem{}, kCardGap)];
    }
    // IPv4 traffic: a 2 x 2 grid with sent packets and bytes in the top line and the
    // received values in the bottom line; column separator between the cells.
    for (std::size_t line = 0; line < trafficLines_.size(); ++line) {
        trafficLines_[line] = mac::makeStack(false, kCardGap);
        trafficLines_[line].homogeneous = YES;
        trafficLines_[line].dividerColor = mac::color(ui::Color::separator);
        [cards[2] addView:trafficLines_[line] item:mac::withGap(mac::StackItem{}, line == 0 ? 16 : 0)];
    }

    // Visual, tab and accessible order: statisticsFields (3 / 3 / 4, sent before received).
    for (std::size_t i = 0; i < rows_.size(); ++i) {
        auto& row = rows_[i];
        const auto labelText = utf8(statisticsFields[i].label);
        row.box = mac::makeStack(true, 0);
        row.line = mac::makeStack(false, ui::relatedGap);
        row.line.wrapsWhenNarrow = YES;
        row.label = mac::makeLabel(labelText, ui::Font::label);
        // Native read-only field: focusable, selectable and copyable, never editable.
        row.value = mac::makeReadOnlyField("-", ui::Font::counter, labelText);
        row.value.bezeled = NO;
        row.value.drawsBackground = NO;
        row.value.alignment = NSTextAlignmentRight;
        mac::StackItem labelItem = mac::flexibleItem();
        labelItem.fill = false;
        [row.line addView:row.label item:labelItem];
        [row.line addView:row.value item:mac::trailingItem()];
        [row.box addView:row.line];
        row.separator = mac::makeSeparator();
        [row.box addView:row.separator item:mac::withGap(mac::fixedHeight(1), ui::fieldGap)];
        row.separator.hidden = !statisticsSeparatorAfter(i, false);
        const auto card = statisticsCard(i);
        mac::StackItem item;
        item.gapBefore = (i == 0 || i == 3) ? 16 : ui::fieldGap;
        if (card < 2) {
            [cards[card] addView:row.box item:item];
            continue;
        }
        // Wide: lines (sent, received) of two cells. Narrow: one column in the same order.
        const std::size_t offset = i - 6;
        item.gapBefore = 0;
        item.top = true;
        [trafficLines_[offset / 2] addView:row.box item:item];
    }
    [window_ setCloseHandler:[this] { close(); }];
    // Escape and X close only this window; Enter has no mutating action.
    [window_ setKeyHandler:[this](NSEvent* event) {
        if (mac::isEscape(event)) {
            close();
            return true;
        }
        return mac::isReturn(event);
    }];
    [window_ setResizeHandler:[this] { relayout(); }];
    // Local duration text only, no IPC.
    durationTimer_.start(1000, [this] { refresh(); });
    relayout();
}

void StatisticsWindow::refresh() {
    if (!window_) return;
    for (std::size_t i = 0; i < rows_.size(); ++i) {
        // All ten values change together from one complete snapshot.
        mac::setFieldText(rows_[i].value, utf8(model_.value(i)));
    }
    mac::setLabelText(status_, utf8(model_.connectionStatus()));
    mac::setTextColor(status_, model_.connected() ? ui::Color::success : ui::Color::error);
    const auto freshness = utf8(model_.freshness());
    mac::setLabelText(freshness_, freshness);
    mac::setVisible(freshness_, !freshness.empty());
    [root_ relayout];
}

void StatisticsWindow::relayout() {
    if (!window_) return;
    const CGFloat width = window_.contentView.bounds.size.width;
    const bool narrow = width < ui::statisticsBreakpoint;
    if (narrow != narrow_) {
        narrow_ = narrow;
        // Stacked top cards; the IPv4 lines become one column in the same order and
        // lose their column separator.
        top_.forceVertical = narrow ? YES : NO;
        for (PVStackView* line : trafficLines_) line.forceVertical = narrow ? YES : NO;
        for (std::size_t i = 0; i < rows_.size(); ++i)
            rows_[i].separator.hidden = !statisticsSeparatorAfter(i, narrow);
    }
    const CGFloat margin = std::min<CGFloat>(narrow ? ui::compactMargin : ui::contentMargin, width / 8);
    content_.insets = NSEdgeInsetsMake(margin, margin, margin, margin);
    [root_ relayout];
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
