#include "mac_contracts_window.h"

#include <algorithm>

#include "core/base/secure_buffer.h"
#include "core/presentation/table_pagination.h"
#include "mac_tray_app.h"

namespace phpvpn::tray {
namespace {
// Column geometry of the Win32 table in DIP (tray_UX.md UX10): Contract 250,
// Enabled 80, Key flexible (at least 260) and Actions 116 (the 104 DIP Change
// button), 12 DIP cell padding. The table is at least 446 + 260 DIP wide; a
// narrower window scrolls the header and the rows horizontally together.
constexpr CGFloat kContractWidth = 250, kEnabledWidth = 80, kFixedWidth = 446;
constexpr CGFloat kActionsWidth = kFixedWidth - kContractWidth - kEnabledWidth;
constexpr CGFloat kMinKeyWidth = 260, kCellPadding = 12, kChangeWidth = 104;
constexpr CGFloat kMinTableWidth = kFixedWidth + kMinKeyWidth;

void wipeJson(JsonValue& value) {
    if (value.isString()) {
        auto& text = const_cast<std::string&>(value.stringValue());
        secureWipe(text.data(), text.size());
    } else if (value.isObject()) {
        for (auto& entry : value.objectValue()) wipeJson(entry.second);
    } else if (value.isArray()) {
        for (auto& item : const_cast<JsonArray&>(value.arrayValue())) wipeJson(item);
    }
}

// A table cell: the content with the horizontal cell padding.
PVStackView* cell(NSView* content, CGFloat width, bool center) {
    PVStackView* box = mac::makeStack(false, 0, NSEdgeInsetsMake(0, kCellPadding, 0, kCellPadding));
    mac::StackItem item = center ? mac::natural() : mac::flexibleItem();
    item.fill = false;
    [box addView:content item:item];
    (void)width;
    return box;
}
}  // namespace

ContractsWindow::ContractsWindow(TrayApp& app, CryptoFactory cryptoFactory)
    : app_(app), cryptoFactory_(std::move(cryptoFactory)), writer_(app.endpoint(), app.dispatcher()),
      checker_(app.endpoint(), app.dispatcher()) {}

ContractsWindow::~ContractsWindow() {
    // Owned by the TrayApp hooks: the application has already ended the session.
    endSession();
}

void installContractsWindow(TrayApp& app) {
    auto contracts = std::make_shared<ContractsWindow>(app);
    ContractsWindow* raw = contracts.get();
    app.registerParticipant({raw, [raw] { return raw->window(); }});
    app.hooks().activateContracts = [raw] { return raw->activate(); };
    app.hooks().openContracts = [contracts](const std::string& document, const std::string& path,
                                            const std::string& revision) {
        return contracts->open(document, path, revision);
    };
    // The saved configuration applies to the next connection (UX12).
    TrayApp* owner = &app;
    app.addStatusListener([raw, owner](const TrayStatus&, bool) { raw->setConnectionActive(owner->connectedOrConnectingNow()); });
}

bool ContractsWindow::activate() {
    if (!window_) return false;
    mac::presentWindow(window_);
    return true;
}

int ContractsWindow::pageCount() const {
    return machine_.model() ? tablePageCount(machine_.model()->rows().size()) : 1;
}

bool ContractsWindow::open(std::string_view document, std::string path, std::string revision) {
    if (window_) {
        activate();
        return true;
    }
    if (ending_ || !cryptoFactory_) return false;
    auto crypto = cryptoFactory_();
    if (!crypto) return false;
    crypto_ = std::move(crypto).value();
    if (!machine_.load(*crypto_, document, std::move(path), std::move(revision))) {
        crypto_.reset();
        return false;
    }
    page_ = 0;
    tableSensitive_ = true;
    localError_.clear();
    savedSnapshot_.reset();
    connectionActive_ = app_.connectedOrConnectingNow();
    build();
    if (!createRows()) localError_ = std::string(kContractsPageFailed);
    createPager();
    refresh();
    mac::presentWindow(window_);
    if (!rows_.empty()) [window_ makeFirstResponder:rows_.front().toggle];
    return true;
}

void ContractsWindow::build() {
    // The Windows default client is 1180 x 680 DIP; makeWindow clamps it to the screen.
    window_ = mac::makeWindow(kContractsTitle, ui::contractsClient, Size{480, 320});
    root_ = mac::makeRoot(window_);
    PVStackView* body = mac::makeStack(true, 0, NSEdgeInsetsMake(ui::sectionGap, ui::contentMargin, 0, ui::contentMargin));
    [root_ addView:body item:mac::flexibleItem()];

    // Heading and description on the left, own virtual IP on the right; they stack
    // when the window is narrow.
    PVStackView* top = mac::makeStack(false, ui::sectionGap);
    top.wrapsWhenNarrow = YES;
    PVStackView* texts = mac::makeStack(true, ui::spacing);
    [texts addView:mac::makeLabel(kContractsTitle, ui::Font::heading)];
    [texts addView:mac::makeLabel(kContractsDescription, ui::Font::label, ui::Color::text, true)];
    mac::StackItem textsItem = mac::flexibleItem();
    textsItem.fill = false;
    textsItem.top = true;
    textsItem.minWidth = 280;
    [top addView:texts item:textsItem];
    PVStackView* identity = mac::makeStack(true, ui::spacing);
    [identity addView:mac::makeLabel(kContractsLocalLabel, ui::Font::label) item:mac::natural()];
    NSTextField* local = mac::makeLabel(machine_.model()->localIp().toString(), ui::Font::strongValue);
    local.selectable = YES;
    [identity addView:local item:mac::natural()];
    mac::StackItem identityItem = mac::trailingItem();
    identityItem.top = true;
    [top addView:identity item:identityItem];
    [body addView:top];

    notice_ = mac::makeLabel(kContractsNextConnection, ui::Font::label, ui::Color::warning, true);
    notice_.hidden = YES;
    [body addView:notice_ item:mac::withGap(mac::StackItem{}, ui::relatedGap)];

    // Table: fixed header, vertically scrolled rows, both following one horizontal
    // offset so the header and the rows move together (UX02).
    header_ = mac::makeStack(false, 0);
    header_.fillColor = mac::color(ui::Color::contractsHeader);
    const char* const* names = kContractsColumns;
    const CGFloat widths[]{kContractWidth, kEnabledWidth, 0, kActionsWidth};
    for (int column = 0; column < 4; ++column) {
        NSTextField* name = mac::makeLabel(names[column], ui::Font::contractsHeader, ui::Color::primaryText);
        PVStackView* box = cell(name, widths[column], false);
        mac::StackItem item = column == 2 ? mac::flexibleItem() : mac::fixedWidth(widths[column]);
        if (column == 2) item.minWidth = kMinKeyWidth;
        item.fill = false;
        [header_ addView:box item:item];
    }
    headerScroll_ = [[NSScrollView alloc] initWithFrame:NSMakeRect(0, 0, 400, ui::headerHeight)];
    headerScroll_.hasHorizontalScroller = NO;
    headerScroll_.hasVerticalScroller = NO;
    headerScroll_.drawsBackground = NO;
    headerScroll_.borderType = NSNoBorder;
    headerScroll_.documentView = header_;
    [body addView:headerScroll_ item:mac::withGap(mac::fixedHeight(ui::headerHeight), ui::relatedGap)];

    rowsBox_ = mac::makeStack(true, 0);
    rowsBox_.fillColor = mac::color(ui::Color::surface);
    [rowsBox_ setAccessibilityLabel:@"Communication contracts table"];
    rowsScroll_ = [[NSScrollView alloc] initWithFrame:NSMakeRect(0, 0, 400, 300)];
    rowsScroll_.hasVerticalScroller = YES;
    rowsScroll_.hasHorizontalScroller = YES;
    rowsScroll_.autohidesScrollers = YES;
    rowsScroll_.borderType = NSLineBorder;
    rowsScroll_.documentView = rowsBox_;
    rowsScroll_.contentView.postsBoundsChangedNotifications = YES;
    NSScrollView* headerScroll = headerScroll_;
    NSClipView* rowsClip = rowsScroll_.contentView;
    boundsObserver_ = [[NSNotificationCenter defaultCenter]
        addObserverForName:NSViewBoundsDidChangeNotification
                    object:rowsClip
                     queue:nil
                usingBlock:^(NSNotification* notification) {
                    (void)notification;
                    // The header follows the horizontal offset of the rows.
                    [headerScroll.contentView scrollToPoint:NSMakePoint(rowsClip.bounds.origin.x, 0)];
                    [headerScroll reflectScrolledClipView:headerScroll.contentView];
                }];
    [body addView:rowsScroll_ item:mac::withMinHeight(mac::flexibleItem(), 112)];

    pager_ = mac::makeStack(false, ui::spacing);
    [pager_ setAccessibilityLabel:@"Contract pages"];
    mac::StackItem pagerItem = mac::natural();
    pagerItem.gapBefore = ui::fieldGap;
    [body addView:pager_ item:pagerItem];

    // The message area sits directly above the footer; a click focuses the first
    // blocking contract.
    error_ = mac::makeLabel("", ui::Font::label, ui::Color::text, true);
    error_.hidden = YES;
    mac::setClickAction(error_, [this] {
        if (machine_.model())
            if (auto focus = machine_.model()->firstBlockingFocus()) focusContract(focus->remoteIp, 1);
    });
    PVStackView* errorBox = mac::makeStack(true, 0, NSEdgeInsetsMake(ui::fieldGap, ui::contentMargin, ui::fieldGap,
                                                                     ui::contentMargin));
    [errorBox addView:error_];
    [root_ addView:errorBox];

    [root_ addView:mac::makeSeparator() item:mac::fixedHeight(1)];
    // Wizard footer geometry: check and repeat leading, Cancel and Save trailing.
    auto footer = mac::makeFooter();
    check_ = mac::makeButton(kContractsCheck, mac::ButtonRole::secondary, [this] { deferred_.post([this] { check(); }); });
    repeat_ = mac::makeButton(kContractsRepeat, mac::ButtonRole::secondary,
                              [this] { deferred_.post([this] { save(true); }); });
    cancel_ = mac::makeButton("Cancel", mac::ButtonRole::secondary, [this] { deferred_.post([this] { close(); }); });
    save_ = mac::makeButton("Save", mac::ButtonRole::primary, [this] { deferred_.post([this] { save(false); }); });
    check_.hidden = YES;
    repeat_.hidden = YES;
    [footer.leading addView:check_ item:mac::withMinWidth(mac::natural(), ui::buttonMinWidth)];
    [footer.leading addView:repeat_ item:mac::withMinWidth(mac::natural(), ui::buttonMinWidth)];
    [footer.trailing addView:cancel_ item:mac::withMinWidth(mac::natural(), 104)];
    [footer.trailing addView:save_ item:mac::withMinWidth(mac::natural(), 180)];
    [root_ addView:footer.bar];

    [window_ setCloseHandler:[this] { close(); }];
    [window_ setKeyHandler:[this](NSEvent* event) {
        if (mac::isEscape(event)) {
            deferred_.post([this] { close(); });
            return true;
        }
        // Only Save is the default action (Enter outside another button).
        if ([window_.firstResponder isKindOfClass:[NSButton class]]) return false;
        if (save_.enabled && !save_.hidden) deferred_.post([this] { save(false); });
        return true;
    }];
    [window_ setResizeHandler:[this] {
        [root_ relayout];
        layoutTable();
    }];
}

bool ContractsWindow::createRows() {
    auto* model = machine_.model();
    if (!model || !rowsBox_) return false;
    changing_ = true;
    const auto sources = model->rows();
    const auto start = static_cast<std::size_t>(page_) * kTablePageSize;
    const auto count = start < sources.size()
                           ? std::min(sources.size() - start, static_cast<std::size_t>(kTablePageSize))
                           : 0;
    rows_.reserve(count);
    bool complete = true;
    for (const auto& source : sources.subspan(start, count)) {
        auto target = model->target(source.remoteIp());
        if (!target) {
            complete = false;
            continue;
        }
        Row row;
        row.remote = source.remoteIp();
        row.identity = target->contractId();
        row.box = mac::makeStack(false, 0);
        row.box.fillColor = mac::color(ui::Color::surface);
        const auto remote = row.remote;

        NSTextField* name = mac::makeLabel(row.identity, ui::Font::label);
        name.selectable = YES;
        name.refusesFirstResponder = YES;
        [row.box addView:cell(name, kContractWidth, false) item:mac::fixedWidth(kContractWidth)];

        row.toggle = [NSButton checkboxWithTitle:@"" target:nil action:nil];
        [row.toggle setAccessibilityLabel:mac::ns(row.identity + " Enabled")];
        mac::setAction(row.toggle, [this, remote] { rowEvent(remote, 0); });
        PVStackView* toggleBox = mac::makeStack(false, 0);
        mac::StackItem toggleItem = mac::natural();
        [toggleBox addView:row.toggle item:toggleItem];
        toggleBox.insets = NSEdgeInsetsMake(0, (kEnabledWidth - mac::naturalSize(row.toggle).width) / 2, 0, 0);
        [row.box addView:toggleBox item:mac::fixedWidth(kEnabledWidth)];

        row.key = mac::makeEntry(ui::Font::mono, row.identity + " Key");
        // Every user edit is checked as a whole before the field changes (bound and
        // Key budget); an unsupported or over-budget insertion is rejected whole.
        mac::setEditCheck(row.key, [this, remote](NSString* proposed) {
            auto text = mac::utf16(proposed);
            const bool accepted = text.size() <= kContractsKeyBound && acceptKey(remote, text);
            secureWipe(text.data(), text.size() * sizeof(char16_t));
            return accepted;
        });
        mac::setTextChanged(row.key, [this, remote] { rowEvent(remote, 1); });
        PVStackView* keyBox = cell(row.key, kMinKeyWidth, false);
        mac::StackItem keyItem = mac::flexibleItem();
        keyItem.fill = false;
        keyItem.minWidth = kMinKeyWidth;
        [row.box addView:keyBox item:keyItem];

        row.change = mac::makeButton(kContractsChange, mac::ButtonRole::secondary,
                                     [this, remote] { deferred_.post([this, remote] { rowEvent(remote, 2); }); });
        row.change.controlSize = NSControlSizeRegular;
        [row.change setAccessibilityLabel:mac::ns(row.identity + " Change")];
        PVStackView* actions = mac::makeStack(false, 0, NSEdgeInsetsMake(0, ui::fieldGap, 0, 0));
        [actions addView:row.change item:mac::fixedWidth(kChangeWidth)];
        [row.box addView:actions item:mac::fixedWidth(kActionsWidth)];

        [rowsBox_ addView:row.box item:mac::fixedHeight(ui::rowHeight)];
        [rowsBox_ addView:mac::makeSeparator() item:mac::fixedHeight(1)];
        rows_.push_back(row);
        syncRow(rows_.back());
    }
    changing_ = false;
    layoutTable();
    return complete;
}

void ContractsWindow::destroyRows() {
    const bool wasChanging = changing_;
    changing_ = true;
    for (auto& row : rows_) {
        // Clear the native Key text before the control goes away (UX13).
        if (row.key) row.key.stringValue = @"";
    }
    [rowsBox_ removeAllViews];
    rows_.clear();
    changing_ = wasChanging;
}

void ContractsWindow::layoutTable() {
    if (!rowsScroll_ || !rowsBox_ || !header_) return;
    const NSSize content = rowsScroll_.contentSize;
    const CGFloat width = std::max<CGFloat>(content.width, kMinTableWidth);
    const CGFloat height = std::max<CGFloat>([rowsBox_ heightForWidth:width], content.height);
    [rowsBox_ setFrameSize:NSMakeSize(width, height)];
    [header_ setFrameSize:NSMakeSize(width, ui::headerHeight)];
    [headerScroll_.contentView scrollToPoint:NSMakePoint(rowsScroll_.contentView.bounds.origin.x, 0)];
    [headerScroll_ reflectScrolledClipView:headerScroll_.contentView];
}

void ContractsWindow::createPager() {
    if (!pager_) return;
    [pager_ removeAllViews];
    const auto items = contractsPagerItems(pageCount(), page_);
    for (const auto& item : items) {
        if (item.page < 0) {
            NSTextField* ellipsis = mac::makeLabel(item.label, ui::Font::label);
            [pager_ addView:ellipsis item:mac::natural()];
            continue;
        }
        const int page = item.page;
        NSButton* button = mac::makeButton(item.label, item.current ? mac::ButtonRole::primary : mac::ButtonRole::secondary,
                                           [this, page] {
                                               // The button is destroyed by the page change; run it afterwards.
                                               deferred_.post([this, page] { changePage(page); });
                                           });
        button.controlSize = NSControlSizeRegular;
        button.enabled = item.sensitive && tableSensitive_ ? YES : NO;
        [button setAccessibilityLabel:mac::ns(item.accessibleName)];
        [pager_ addView:button item:mac::withMinWidth(mac::natural(), 36)];
    }
    mac::setVisible(pager_, !items.empty());
    [root_ relayout];
}

void ContractsWindow::changePage(int page) {
    auto* model = machine_.model();
    if (!window_ || ending_ || closing_ || !model || model->frozen() || page < 0 || page >= pageCount()) return;
    if (page != page_) {
        const int previous = page_;
        [window_ makeFirstResponder:cancel_];
        destroyRows();
        page_ = page;
        if (!createRows()) {
            destroyRows();
            page_ = previous;
            if (!createRows()) destroyRows();
            localError_ = std::string(kContractsPageFailed);
        }
        createPager();
        [rowsScroll_.contentView scrollToPoint:NSMakePoint(rowsScroll_.contentView.bounds.origin.x, 0)];
        [rowsScroll_ reflectScrolledClipView:rowsScroll_.contentView];
        refresh();
    }
    if (!rows_.empty()) [window_ makeFirstResponder:rows_.front().toggle];
    else [window_ makeFirstResponder:save_];
}

void ContractsWindow::focusContract(Ipv4Address remote, int column) {
    auto* model = machine_.model();
    if (!model || !window_) return;
    const auto sources = model->rows();
    for (std::size_t i = 0; i < sources.size(); ++i) {
        if (sources[i].remoteIp() != remote) continue;
        changePage(static_cast<int>(i / kTablePageSize));
        if (Row* row = findRow(remote)) {
            NSView* controls[]{row->toggle, row->key, row->change};
            NSView* target = controls[std::clamp(column, 0, 2)];
            [window_ makeFirstResponder:target];
            [target scrollRectToVisible:target.bounds];
        }
        break;
    }
}

ContractsWindow::Row* ContractsWindow::findRow(Ipv4Address remote) {
    for (auto& row : rows_)
        if (row.remote == remote) return &row;
    return nullptr;
}

void ContractsWindow::syncRow(Row& row) {
    const auto* source = machine_.model() ? machine_.model()->row(row.remote) : nullptr;
    if (!source) return;
    const bool wasChanging = changing_;
    changing_ = true;
    mac::setFieldText(row.key, source->keyText());
    const NSControlStateValue state = source->enabled() ? NSControlStateValueOn : NSControlStateValueOff;
    if (row.toggle.state != state) row.toggle.state = state;
    changing_ = wasChanging;
}

bool ContractsWindow::acceptKey(Ipv4Address remote, std::u16string_view text) {
    if (changing_) return true;
    auto* model = machine_.model();
    if (!model || model->frozen()) return false;
    const auto* source = model->row(remote);
    if (!source || !source->enabled()) return false;
    std::string utf8;
    if (!contractsKeyTextSupported(text, utf8)) {
        localError_ = std::string(kContractsUnsupportedKey);
        refresh();
        return false;
    }
    const auto accepted = model->setKey(remote, utf8);
    secureWipe(utf8.data(), utf8.size());
    if (!accepted) {
        localError_ = std::string(kContractsKeyBudget);
        refresh();
        return false;
    }
    localError_.clear();
    return true;
}

void ContractsWindow::rowEvent(Ipv4Address remote, int column) {
    auto* model = machine_.model();
    Row* row = findRow(remote);
    if (ending_ || closing_ || !model || !row || changing_) return;
    if (savedSnapshot_ || model->frozen()) {
        // A frozen or saved draft never changes; restore the visual state.
        if (column != 2) syncRow(*row);
        return;
    }
    if (column == 0) {
        (void)model->setEnabled(remote, row->toggle.state == NSControlStateValueOn);
        machine_.changed();
        syncRow(*row);
        refresh();
        return;
    }
    if (column == 1) {
        auto text = mac::utf16(row->key.stringValue);
        acceptKey(remote, text);
        secureWipe(text.data(), text.size() * sizeof(char16_t));
        machine_.changed();
        refresh();
        return;
    }
    openPasswordSha(remote);
}

void ContractsWindow::openPasswordSha(Ipv4Address remote) {
    auto* model = machine_.model();
    const auto* source = model ? model->row(remote) : nullptr;
    if (!source || !source->enabled() || !crypto_) return;
    auto target = model->target(remote);
    if (!target) return;
    std::erase_if(children_, [](const auto& child) { return !child->window(); });
    auto child = std::make_unique<PasswordShaWindow>(
        *crypto_, *target,
        [this](const ContractTarget& captured, std::string_view key) {
            auto* current = machine_.model();
            if (ending_ || closing_ || !current) return false;
            const auto* row = current->row(captured.remoteIp());
            if (!row || !row->enabled()) return false;
            if (!current->applyPasswordKey(captured, key)) return false;
            machine_.changed();
            if (Row* visible = findRow(captured.remoteIp())) syncRow(*visible);
            localError_.clear();
            refresh();
            return true;
        },
        [this](const ContractTarget& closed) {
            auto* current = machine_.model();
            if (ending_ || closing_ || !window_ || !current || current->frozen()) return;
            const auto* row = current->row(closed.remoteIp());
            mac::presentWindow(window_);
            focusContract(closed.remoteIp(), row && row->enabled() ? 2 : 1);
        });
    if (child->open(window_)) children_.push_back(std::move(child));
    else {
        localError_ = std::string(kContractsPasswordFailed);
        refresh();
    }
}

void ContractsWindow::save(bool repeat) {
    auto* model = machine_.model();
    if (!window_ || savedSnapshot_ || !model || contractsSnapshot().saving) return;
    if (!repeat && !model->dirty()) return;
    if (repeat) {
        if (!machine_.canRepeat(checker_.busy())) return;
        const auto before = contractsSnapshot();
        if (!mac::confirm(kContractsRepeatQuestion, kContractsRepeat) || !window_ || contractsSnapshot() != before) return;
    }
    if (!machine_.prepare(repeat, checker_.busy())) {
        refresh();
        return;
    }
    // The main OK freezes the candidate first, then closes its children without
    // applying their unconfirmed changes (UX03).
    closeChildren();
    machine_.dispatch(static_cast<std::uint64_t>(monotonicMs()),
                      [this](const SaveOperation& operation, const std::string& payload) {
                          return writer_.requestSave(operation, payload,
                                                     [this](const SaveResult& result) { onSaveResult(result); });
                      });
    refresh();
}

void ContractsWindow::check() {
    if (!window_ || !machine_.model()) return;
    machine_.check(checker_.busy(), [this](std::uint64_t generation) {
        WizardRequest request;
        request.command = WizardCommand::get_config_document;
        return checker_.requestWizard(request,
                                      [this, generation](WizardResult& result) { onCheckResult(generation, result); });
    });
    refresh();
}

void ContractsWindow::onSaveResult(const SaveResult& result) {
    if (!machine_.saving()) return;
    handle(machine_.saveFinished(result));
}

void ContractsWindow::onCheckResult(std::uint64_t checkGeneration, WizardResult& result) {
    struct Wipe {
        WizardResult& result;
        ~Wipe() {
            if (result.reply) wipeJson(result.reply.value().result);
        }
    } wipe{result};
    handle(machine_.checkFinished(checkGeneration, result.reply));
}

void ContractsWindow::handle(ContractsEvent event) {
    if (!window_) return;
    if (event == ContractsEvent::saved) {
        savedSnapshot_ = contractsSnapshot();
        refresh();
        // The tray verifies the connection, asks the shared reconnect question and
        // finally discards exactly this saved snapshot.
        app_.requestReconnectAfterSave(this, lifecycleSnapshot());
        return;
    }
    if (event == ContractsEvent::matched) {
        const auto before = contractsSnapshot();
        mac::showMessage(kContractsMatched, kContractsTitle, mac::DialogIcon::information);
        if (window_ && contractsSnapshot() == before) closeWindow();
        return;
    }
    if (event != ContractsEvent::none) refresh();
}

void ContractsWindow::refresh() {
    auto* model = machine_.model();
    if (!window_ || !save_ || !model) return;
    const auto sources = model->rows();
    const auto changes = static_cast<std::size_t>(
        std::count_if(sources.begin(), sources.end(), [](const auto& row) { return row.dirty(); }));
    mac::setButtonTitle(save_, contractsSaveLabel(changes));
    const auto snapshot = contractsSnapshot();
    const auto footer = contractsFooter(snapshot, savedSnapshot_.has_value(), changes, model->canPrepareSave(),
                                        machine_.canRepeat(checker_.busy()));
    if (footer.tableSensitive != tableSensitive_) {
        // Saving, saveUnknown and a saved draft freeze the table and its pages.
        tableSensitive_ = footer.tableSensitive;
        createPager();
    }
    mac::setVisible(save_, footer.saveVisible);
    save_.enabled = footer.saveEnabled ? YES : NO;
    mac::setVisible(check_, footer.checkVisible);
    check_.enabled = footer.checkEnabled ? YES : NO;
    mac::setVisible(repeat_, footer.repeatVisible);
    cancel_.enabled = snapshot.saving ? NO : YES;
    mac::setCloseEnabled(window_, !snapshot.saving);
    const auto message = contractsMessage(localError_, model->firstBlockingFocus(), machine_.notice());
    mac::setLabelText(error_, message.text);
    error_.font = mac::font(message.validation ? ui::Font::strongValue : ui::Font::label);
    mac::setTextColor(error_, message.validation ? ui::Color::error : ui::Color::text);
    mac::setVisible(error_, !message.text.empty());
    mac::setVisible(notice_, connectionActive_);
    const bool frozen = snapshot.saving || snapshot.unknown || !footer.tableSensitive;
    for (auto& row : rows_) {
        const auto* source = model->row(row.remote);
        if (!source) continue;
        row.toggle.enabled = frozen ? NO : YES;
        // A disabled contract keeps its Key read-only but selectable (UX02).
        row.key.editable = source->enabled() && !frozen ? YES : NO;
        row.key.selectable = YES;
        row.change.enabled = source->enabled() && !frozen ? YES : NO;
        const bool dirty = source->dirty();
        if (dirty != row.lastDirty) {
            row.lastDirty = dirty;
            row.box.fillColor = mac::color(dirty ? ui::Color::contractsChanged : ui::Color::surface);
            row.key.backgroundColor = mac::color(dirty ? ui::Color::contractsChanged : ui::Color::surface);
        }
        const bool valid = !source->keyErrorVisible();
        if (valid != row.lastValid) {
            row.lastValid = valid;
            mac::setFieldInvalid(row.key, !valid, kContractsInvalidKeyDescription);
        }
    }
    for (const auto& child : children_) {
        if (!child->window()) continue;
        const auto* source = model->row(child->remoteIp());
        child->setTargetEnabled(source && source->enabled() && !snapshot.saving && !snapshot.unknown);
    }
    [root_ relayout];
    layoutTable();
}

void ContractsWindow::setConnectionActive(bool active) {
    if (connectionActive_ == active) return;
    connectionActive_ = active;
    refresh();
}

ContractsLifecycleSnapshot ContractsWindow::contractsSnapshot() const {
    auto snapshot = machine_.snapshot(checker_.busy());
    snapshot.open = window_ != nil;
    return snapshot;
}

LifecycleSnapshot ContractsWindow::lifecycleSnapshot() const {
    const auto current = contractsSnapshot();
    // Any change of the detailed snapshot (generations, deadline, checking) or of
    // the saved state is a new lifecycle generation.
    if (current != lastSnapshot_ || savedSnapshot_.has_value() != lastSaved_) {
        lastSnapshot_ = current;
        lastSaved_ = savedSnapshot_.has_value();
        ++revision_;
    }
    LifecycleSnapshot snapshot;
    snapshot.open = current.open;
    snapshot.dirty = current.open && current.dirty;
    snapshot.unknown = current.open && current.unknown;
    snapshot.busy = current.open && current.saving;
    snapshot.generation = revision_;
    return snapshot;
}

bool ContractsWindow::approvedDiscard(const LifecycleSnapshot& approved) {
    if (!window_ || approved.busy || lifecycleSnapshot() != approved) return false;
    closeWindow();
    return true;
}

void ContractsWindow::close() {
    if (!window_ || closing_) return;
    const auto before = lifecycleSnapshot();
    if (before.busy) return;
    if ((before.unknown || before.dirty) &&
        !mac::confirm(before.unknown ? kContractsUnknownClose : kContractsDiscard, kContractsTitle))
        return;
    approvedDiscard(before);
}

void ContractsWindow::closeChildren() {
    for (auto& child : children_) child->dismiss();
    children_.clear();
}

void ContractsWindow::closeWindow() {
    closing_ = true;
    savedSnapshot_.reset();
    closeChildren();
    if (boundsObserver_) [[NSNotificationCenter defaultCenter] removeObserver:boundsObserver_];
    boundsObserver_ = nil;
    if (window_) {
        destroyRows();
        PVWindow* window = window_;
        window_ = nil;
        mac::closeWindow(window);
    }
    root_ = nil;
    notice_ = nil;
    headerScroll_ = nil;
    header_ = nil;
    rowsScroll_ = nil;
    rowsBox_ = nil;
    pager_ = nil;
    error_ = nil;
    check_ = nil;
    repeat_ = nil;
    cancel_ = nil;
    save_ = nil;
    // A window closed by any path discards the draft; late replies are revoked.
    writer_.abandon();
    checker_.abandon();
    machine_.close();
    crypto_.reset();
    closing_ = false;
}

void ContractsWindow::endSession() {
    if (ending_) return;
    ending_ = true;
    closeWindow();
}
}  // namespace phpvpn::tray
