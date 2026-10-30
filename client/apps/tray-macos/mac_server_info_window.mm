#include "mac_server_info_window.h"

#include <algorithm>
#include <memory>
#include <vector>

#include "mac_tray_app.h"

using phpvpn::tray::ServerInfoTableRow;

// View-based table data of the current page.
@interface PVServerInfoTable : NSObject <NSTableViewDataSource, NSTableViewDelegate>
- (void)setRows:(std::vector<ServerInfoTableRow>)rows;
@end

@implementation PVServerInfoTable {
    std::vector<ServerInfoTableRow> _rows;
}

- (void)setRows:(std::vector<ServerInfoTableRow>)rows {
    _rows = std::move(rows);
}

- (NSInteger)numberOfRowsInTableView:(NSTableView*)tableView {
    (void)tableView;
    return static_cast<NSInteger>(_rows.size());
}

- (NSView*)tableView:(NSTableView*)tableView viewForTableColumn:(NSTableColumn*)tableColumn row:(NSInteger)row {
    using namespace phpvpn::tray;
    if (row < 0 || static_cast<std::size_t>(row) >= _rows.size() || !tableColumn) return nil;
    NSString* identifier = tableColumn.identifier;
    NSTableCellView* cell = [tableView makeViewWithIdentifier:identifier owner:self];
    if (!cell) {
        cell = [[NSTableCellView alloc] initWithFrame:NSMakeRect(0, 0, tableColumn.width, ui::rowHeight)];
        cell.identifier = identifier;
        NSTextField* text = mac::makeLabel("", ui::Font::label);
        text.lineBreakMode = NSLineBreakByTruncatingTail;
        const CGFloat height = mac::naturalSize(text).height;
        text.frame = NSMakeRect(12, (ui::rowHeight - height) / 2, std::max<CGFloat>(0, tableColumn.width - 24), height);
        text.autoresizingMask = NSViewWidthSizable | NSViewMinYMargin | NSViewMaxYMargin;
        [cell addSubview:text];
        cell.textField = text;
    }
    const auto& data = _rows[static_cast<std::size_t>(row)];
    const std::string& value = [identifier isEqualToString:@"contract"] ? data.contract
                             : [identifier isEqualToString:@"lastSeen"] ? data.lastSeen
                                                                        : data.status;
    cell.textField.stringValue = mac::ns(value);
    cell.textField.textColor = mac::color(ui::Color::text);
    return cell;
}

- (void)tableView:(NSTableView*)tableView didAddRowView:(NSTableRowView*)rowView forRow:(NSInteger)row {
    using namespace phpvpn::tray;
    (void)tableView;
    if (row < 0 || static_cast<std::size_t>(row) >= _rows.size()) return;
    // Valid serverInfoValid, invalid contractsChanged, offline unchanged (UX06).
    const auto state = _rows[static_cast<std::size_t>(row)].state;
    rowView.backgroundColor = state == phpvpn::ServerClientState::valid ? mac::color(ui::Color::serverInfoValid)
                            : state == phpvpn::ServerClientState::invalid ? mac::color(ui::Color::contractsChanged)
                                                                          : mac::color(ui::Color::surface);
}
@end

namespace phpvpn::tray {
namespace {
constexpr CGFloat kColumnWidths[3]{280, 210, 420};
NSString* const kColumnIdentifiers[3]{@"contract", @"lastSeen", @"status"};
}  // namespace

ServerInfoWindow::ServerInfoWindow(IpcEndpoint endpoint, Dispatcher dispatcher)
    : channel_(std::move(endpoint), std::move(dispatcher)) {}

ServerInfoWindow::~ServerInfoWindow() { endSession(); }

void ServerInfoWindow::open() {
    if (window_) {
        // Re-choosing Show Info never requests; it only switches Auto refresh off.
        session_.stopAutoRefresh();
        mac::setSwitchOn(autoRefresh_, false);
        syncAutoTimer();
        if (session_.model()) mac::presentWindow(window_);
        return;
    }
    session_.reset();
    build();
    request();
}

void ServerInfoWindow::endSession() { close(); }

void ServerInfoWindow::close() {
    channel_.abandon();
    session_.reset();
    autoTimer_.stop();
    if (!window_) return;
    PVWindow* window = window_;
    window_ = nil;
    if (raw_) mac::setTextViewString(raw_, "");
    table_.dataSource = nil;
    table_.delegate = nil;
    mac::closeWindow(window);
    root_ = nil;
    count_ = nil;
    tabs_ = nil;
    table_ = nil;
    tableData_ = nil;
    pagerScroll_ = nil;
    pager_ = nil;
    raw_ = nil;
    notice_ = nil;
    refresh_ = nil;
    autoRefresh_ = nil;
}

void ServerInfoWindow::build() {
    window_ = mac::makeWindow(kServerInfoTitle, ui::editorClient, Size{320, 480});
    root_ = mac::makeRoot(window_);
    PVStackView* content = mac::makeStack(true, ui::fieldGap,
                                          NSEdgeInsetsMake(ui::compactMargin, ui::compactMargin, 0, ui::compactMargin));
    [root_ addView:content item:mac::flexibleItem()];

    // Heading, with the bracketed count in the success colour; the count wraps below.
    PVStackView* header = mac::makeStack(false, ui::fieldGap);
    header.wrapsWhenNarrow = YES;
    [header addView:mac::makeLabel(kServerInfoTitle, ui::Font::heading) item:mac::natural()];
    count_ = mac::makeLabel("", ui::Font::heading, ui::Color::success);
    [header addView:count_ item:mac::natural()];
    [content addView:header];

    tabs_ = [[NSTabView alloc] initWithFrame:NSMakeRect(0, 0, 400, 300)];
    tabs_.font = mac::font(ui::Font::label);
    [content addView:tabs_ item:mac::flexibleItem()];

    // Who is online: table and pager. Paging and tab switches are local only.
    PVStackView* who = mac::makeStack(true, ui::fieldGap, NSEdgeInsetsMake(ui::fieldGap, 0, 0, 0));
    who.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    table_ = [[NSTableView alloc] initWithFrame:NSMakeRect(0, 0, 400, 200)];
    tableData_ = [[PVServerInfoTable alloc] init];
    for (std::size_t column = 0; column < 3; ++column) {
        NSTableColumn* view = [[NSTableColumn alloc] initWithIdentifier:kColumnIdentifiers[column]];
        view.title = mac::ns(kServerInfoColumns[column]);
        view.width = kColumnWidths[column];
        view.minWidth = 80;
        view.resizingMask = NSTableColumnUserResizingMask | NSTableColumnAutoresizingMask;
        [table_ addTableColumn:view];
    }
    table_.style = NSTableViewStyleFullWidth;
    table_.rowHeight = ui::rowHeight;
    table_.gridStyleMask = NSTableViewSolidHorizontalGridLineMask;
    table_.usesAlternatingRowBackgroundColors = NO;
    table_.allowsMultipleSelection = NO;
    table_.allowsEmptySelection = YES;
    table_.columnAutoresizingStyle = NSTableViewLastColumnOnlyAutoresizingStyle;
    table_.dataSource = tableData_;
    table_.delegate = tableData_;
    [table_ setAccessibilityLabel:mac::ns(kServerInfoWhoTab)];
    NSScrollView* tableScroll = [[NSScrollView alloc] initWithFrame:NSMakeRect(0, 0, 400, 200)];
    tableScroll.documentView = table_;
    tableScroll.hasVerticalScroller = YES;
    tableScroll.hasHorizontalScroller = YES;
    tableScroll.autohidesScrollers = YES;
    tableScroll.borderType = NSBezelBorder;
    [who addView:tableScroll item:mac::flexibleItem()];
    // Up to 13 pager buttons must not pin the 320 DIP minimum width; on a narrow
    // window the pager scrolls horizontally and every page stays reachable.
    pager_ = mac::makeStack(false, ui::spacing);
    pagerScroll_ = [[NSScrollView alloc] initWithFrame:NSMakeRect(0, 0, 400, 40)];
    pagerScroll_.documentView = pager_;
    pagerScroll_.hasHorizontalScroller = YES;
    pagerScroll_.hasVerticalScroller = NO;
    pagerScroll_.autohidesScrollers = YES;
    pagerScroll_.drawsBackground = NO;
    pagerScroll_.borderType = NSNoBorder;
    [who addView:pagerScroll_ item:mac::fixedHeight(44)];
    NSTabViewItem* whoItem = [[NSTabViewItem alloc] initWithIdentifier:@"who"];
    whoItem.label = mac::ns(kServerInfoWhoTab);
    whoItem.view = who;
    [tabs_ addTabViewItem:whoItem];

    // Raw server information: the exact authenticated plaintext, read-only and
    // selectable, both scrollbars, no wrapping or reformatting.
    const auto raw = mac::makeTextScroll(false, true, kServerInfoRawTab);
    raw_ = raw.text;
    raw.scroll.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    NSTabViewItem* rawItem = [[NSTabViewItem alloc] initWithIdentifier:@"raw"];
    rawItem.label = mac::ns(kServerInfoRawTab);
    rawItem.view = raw.scroll;
    [tabs_ addTabViewItem:rawItem];

    notice_ = mac::makeLabel("", ui::Font::value, ui::Color::text, true);
    notice_.hidden = YES;
    [content addView:notice_];

    // Footer: the indivisible Refresh + Auto refresh pair leading, OK trailing; OK
    // wraps below the pair on a narrow window.
    auto footer = mac::makeFooter();
    refresh_ = mac::makeButton(kServerInfoRefresh, mac::ButtonRole::primary, [this] {
        // A request that cannot be sent may show an error; never inside the click.
        deferred_.post([this] { request(); });
    });
    [footer.leading addView:refresh_ item:mac::withMinWidth(mac::natural(), 104)];
    auto toggle = mac::makeSwitchRow(kServerInfoAutoRefresh, [this](bool on) {
        session_.setAutoRefresh(on, monotonicMs());
        syncAutoTimer();
    });
    autoRefresh_ = toggle.toggle;
    [footer.leading addView:toggle.row item:mac::natural()];
    [footer.trailing addView:mac::makeButton(kServerInfoOk, mac::ButtonRole::primary, [this] { close(); })
                        item:mac::withMinWidth(mac::natural(), ui::buttonMinWidth)];
    [root_ addView:mac::makeSeparator() item:mac::withGap(mac::fixedHeight(1), ui::fieldGap)];
    [root_ addView:footer.bar];

    [window_ setCloseHandler:[this] { close(); }];
    [window_ setKeyHandler:[this](NSEvent* event) {
        if (mac::isEscape(event)) {
            close();
            return true;
        }
        // Enter activates nothing in the raw text and closes the window everywhere else,
        // except on a focused button or switch (Space activates those).
        NSResponder* focus = window_.firstResponder;
        if (focus == raw_ || [focus isKindOfClass:[NSButton class]] || [focus isKindOfClass:[NSSwitch class]])
            return false;
        close();
        return true;
    }];
    [window_ setResizeHandler:[this] { [root_ relayout]; }];
    [root_ relayout];
}

void ServerInfoWindow::request() {
    if (!window_ || session_.pending()) return;
    const auto token = session_.begin();
    if (!token) return;
    syncAutoTimer();
    refresh_.enabled = NO;
    setNotice(kServerInfoLoading);
    const bool sent = channel_.request(IpcCommand::get_server_info, {}, [this, value = *token](std::string& response) {
        finish(value, response);
    });
    if (sent) return;
    const auto failure = session_.notSent(monotonicMs());
    refresh_.enabled = YES;
    setNotice(failure.notice);
    if (failure.fatal) {
        const auto generation = session_.generation();
        mac::showError(*failure.fatal, "PHPVPN");
        if (session_.generation() == generation) close();
        return;
    }
    syncAutoTimer();
}

void ServerInfoWindow::finish(std::uint64_t token, std::string& response) {
    if (!window_) return;
    const auto result = session_.finish(token, response, selectedRow(), monotonicMs());
    if (result.outcome == ServerInfoSession::Outcome::ignored) return;
    refresh_.enabled = YES;
    syncAutoTimer();
    if (result.outcome == ServerInfoSession::Outcome::fatal) {
        const auto generation = session_.generation();
        mac::showError(result.text, "PHPVPN");
        if (session_.generation() == generation) close();
        return;
    }
    if (result.outcome == ServerInfoSession::Outcome::retained) {
        setNotice(result.text);
        return;
    }
    setNotice("");
    if (result.rawChanged) setRaw();
    display();
    if (result.selectedRow) selectRow(*result.selectedRow);
    if (result.outcome == ServerInfoSession::Outcome::first) {
        mac::presentWindow(window_);
        [window_ makeFirstResponder:tabs_];  // the first tab has the focus
    }
}

void ServerInfoWindow::display() {
    if (!session_.model() || !window_) return;
    window_.title = mac::ns(session_.title());
    mac::setLabelText(count_, session_.countText());
    [tableData_ setRows:session_.pageRows()];
    [table_ reloadData];
    rebuildPager();
    [root_ relayout];
}

void ServerInfoWindow::rebuildPager() {
    [pager_ removeAllViews];
    const auto buttons = session_.pager();
    for (const auto& entry : buttons) {
        const int page = entry.page;
        NSButton* button = mac::makeButton(entry.label, entry.current ? mac::ButtonRole::primary : mac::ButtonRole::secondary,
                                           [this, page] {
                                               if (page >= 0) deferred_.post([this, page] { showPage(page); });
                                           });
        button.controlSize = NSControlSizeRegular;
        button.enabled = entry.enabled && entry.page >= 0 ? YES : NO;
        if (!entry.accessibleName.empty()) [button setAccessibilityLabel:mac::ns(entry.accessibleName)];
        [pager_ addView:button item:mac::natural()];
    }
    const CGFloat width = [pager_ naturalWidth];
    const CGFloat height = std::max<CGFloat>([pager_ heightForWidth:width], 1);
    [pager_ setFrameSize:NSMakeSize(width, height)];
    mac::setVisible(pagerScroll_, !buttons.empty());
}

void ServerInfoWindow::showPage(int page) {
    if (!session_.model() || !window_) return;
    session_.setPage(page);
    display();
    [window_ makeFirstResponder:table_];
}

void ServerInfoWindow::setNotice(std::string_view text) {
    if (!notice_) return;
    mac::setLabelText(notice_, text);
    mac::setVisible(notice_, !text.empty());
    [root_ relayout];
}

void ServerInfoWindow::setRaw() {
    const auto& plaintext = session_.model()->snapshot.infoPlaintext;
    // Keep the selection and the visible position across a replacement.
    const NSRange selection = raw_.selectedRange;
    NSClipView* clip = raw_.enclosingScrollView.contentView;
    const NSPoint origin = clip ? clip.bounds.origin : NSZeroPoint;
    NSString* text = mac::ns(plaintext);
    raw_.string = text;
    const NSUInteger length = text.length;
    const NSUInteger start = std::min<NSUInteger>(selection.location, length);
    const NSUInteger end = std::min<NSUInteger>(selection.location + selection.length, length);
    [raw_ setSelectedRange:NSMakeRange(start, end - start)];
    if (clip) {
        [clip scrollToPoint:origin];
        [raw_.enclosingScrollView reflectScrolledClipView:clip];
    }
}

void ServerInfoWindow::syncAutoTimer() {
    autoTimer_.stop();
    const auto deadline = session_.autoDeadline();
    if (!window_ || !deadline) return;
    const auto remaining = std::max<std::int64_t>(1, *deadline - monotonicMs());
    autoTimer_.start(remaining, [this] { onAutoTimer(); }, false);
}

void ServerInfoWindow::onAutoTimer() {
    // The same path as Refresh, on both tabs; an early tick keeps its deadline.
    if (session_.autoDue(monotonicMs())) request();
    else syncAutoTimer();
}

std::optional<std::size_t> ServerInfoWindow::selectedRow() const {
    if (!table_) return std::nullopt;
    const NSInteger row = table_.selectedRow;
    if (row < 0) return std::nullopt;
    return static_cast<std::size_t>(row);
}

void ServerInfoWindow::selectRow(std::size_t row) {
    if (!table_ || row >= static_cast<std::size_t>(table_.numberOfRows)) return;
    const NSInteger index = static_cast<NSInteger>(row);
    [table_ selectRowIndexes:[NSIndexSet indexSetWithIndex:static_cast<NSUInteger>(index)] byExtendingSelection:NO];
    [table_ scrollRowToVisible:index];
}

void installServerInfoWindow(TrayApp& app) {
    auto window = std::make_shared<ServerInfoWindow>(app.endpoint(), app.dispatcher());
    ServerInfoWindow* raw = window.get();
    app.registerParticipant({raw, [raw] { return raw->window(); }});
    app.hooks().openServerInfo = [window] { window->open(); };
}
}  // namespace phpvpn::tray
