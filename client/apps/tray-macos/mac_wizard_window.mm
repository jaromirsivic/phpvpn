#include "mac_wizard_window.h"

#include <memory>
#include <utility>
#include <vector>

#include "mac_tray_app.h"
#include "macos_text.h"
#include "wizard_text.h"

namespace phpvpn::tray {
namespace {
constexpr Size kWizardMinimum{480, 320};
constexpr CGFloat kSidebarWidth = 230;    // wizard_layout.cpp: fixed 230 DIP step panel
constexpr CGFloat kCompactWidth = 760;    // tray_UX.md UX03: compact sidebar below 760 DIP
constexpr CGFloat kCompactHeight = 64;    // wizard_layout.cpp: compact strip
constexpr CGFloat kPrimaryMinWidth = 200; // computeWizardLayout primary width
constexpr CGFloat kDetailMinWidth = 108;
constexpr CGFloat kHeadersButtonWidth = 160;

std::string text(const wchar_t* value) { return utf8(value); }

NSTextField* label(const std::string& value, ui::Font role = ui::Font::label, ui::Color tone = ui::Color::text) {
    return mac::makeLabel(value, role, tone, true);
}
mac::DialogIcon dialogIcon(WizardNotice notice) {
    switch (notice) {
        case WizardNotice::warning: return mac::DialogIcon::warning;
        case WizardNotice::error: return mac::DialogIcon::error;
        case WizardNotice::information: break;
    }
    return mac::DialogIcon::information;
}
bool enabledAndVisible(NSView* view) {
    if (!view || view.hiddenOrHasHiddenAncestor) return false;
    if ([view isKindOfClass:[NSControl class]]) return ((NSControl*)view).enabled;
    return true;
}
}  // namespace

WizardWindow::WizardWindow(IpcEndpoint endpoint, Dispatcher dispatcher)
    : controller_(std::move(endpoint), std::move(dispatcher), *this) {}

WizardWindow::~WizardWindow() {
    // Windows destructor: one best-effort wizard-close (1 s) while a session is open.
    controller_.closeOnExit();
    if (window_) destroyWindow();
}

// --- construction ------------------------------------------------------------------------
bool WizardWindow::createWindow() {
    if (window_) return true;
    build();
    if (!window_) return false;
    timer_.start(kWizardTickMs, [this] { controller_.tick(); });
    rendered_ = false;
    return true;
}

void WizardWindow::presentWindow() {
    if (window_) mac::presentWindow(window_);
    if (headers_.window()) [headers_.window() makeKeyAndOrderFront:nil];
}

void WizardWindow::build() {
    window_ = mac::makeWindow("Configuration Wizard", ui::wizardClient, kWizardMinimum);
    root_ = mac::makeRoot(window_);
    body_ = mac::makeStack(false, 0);
    [root_ addView:body_ item:mac::flexibleItem()];
    mac::StackItem sidebarItem = mac::fixedWidth(kSidebarWidth);
    sidebarItem.fill = true;
    [body_ addView:buildSidebar() item:sidebarItem];
    divider_ = mac::makeSeparator(true);
    mac::StackItem dividerItem = mac::fixedWidth(1);
    dividerItem.fill = true;
    [body_ addView:divider_ item:dividerItem];

    content_ = mac::makeStack(true, ui::relatedGap,
                              NSEdgeInsetsMake(ui::sectionGap, ui::sectionGap, ui::sectionGap, ui::sectionGap));
    messagePage_ = buildMessagePage();
    urlPage_ = buildUrlPage();
    secretPage_ = buildSecretPage();
    identityPage_ = buildIdentityPage();
    contractsPage_ = buildContractsPage();
    for (PVStackView* step : {messagePage_, urlPage_, secretPage_, identityPage_, contractsPage_}) {
        step.hidden = YES;
        [content_ addView:step];
    }
    status_ = label("", ui::Font::label, ui::Color::secondaryText);
    status_.hidden = YES;
    [content_ addView:status_];
    scroller_ = [[PVScrollView alloc] initWithStack:content_];
    [body_ addView:scroller_ item:mac::flexibleItem()];

    [root_ addView:mac::makeSeparator() item:mac::fixedHeight(1)];
    buildFooter();

    // X and Escape cancel the Wizard's own work (never Disconnect or a commit).
    [window_ setCloseHandler:[this] { controller_.cancel(false); }];
    [window_ setKeyHandler:[this](NSEvent* event) {
        if (mac::isEscape(event)) {
            deferred_.post([this] { controller_.cancel(false); });
            return true;
        }
        // Enter on a footer or HTTP headers button belongs to that button; anywhere
        // else it runs the primary action, and only while that action is enabled.
        if ([window_.firstResponder isKindOfClass:[NSButton class]]) return false;
        if (enabledAndVisible(next_)) deferred_.post([this] { controller_.next(); });
        return true;
    }];
    [window_ setResizeHandler:[this] { placeCompact(); }];
    compact_ = false;
    placeCompact();
}

PVStackView* WizardWindow::buildSidebar() {
    sidebar_ = mac::makeStack(true, 0, NSEdgeInsetsMake(12, 12, 12, 12));
    sidebar_.fillColor = mac::color(ui::Color::background);
    sidebarHeading_ = label(text(wizardText::configure), ui::Font::label, ui::Color::secondaryText);
    [sidebar_ addView:sidebarHeading_];
    for (std::size_t i = 0; i < 4; ++i) {
        stepRows_[i] = mac::makeStack(false, ui::fieldGap, NSEdgeInsetsMake(8, 8, 8, 8));
        stepRows_[i].cornerRadius = 4;
        stepMarkers_[i] = [[PVMarkerView alloc] initWithFrame:NSMakeRect(0, 0, 24, 24)];
        stepMarkers_[i].text = mac::ns(std::to_string(i + 1));
        mac::StackItem markerItem = mac::fixedWidth(24);
        markerItem.height = 24;
        markerItem.top = true;
        [stepRows_[i] addView:stepMarkers_[i] item:markerItem];
        stepTexts_[i] = label("");
        mac::StackItem textItem = mac::flexibleItem();
        textItem.fill = false;
        textItem.top = true;
        [stepRows_[i] addView:stepTexts_[i] item:textItem];
        [sidebar_ addView:stepRows_[i] item:mac::withGap(mac::withMinHeight(mac::StackItem{}, 80), i == 0 ? 24 : 0)];
    }
    // The sidebar is informative only; it is not a navigation control.
    [sidebar_ setAccessibilityLabel:@"Wizard steps"];
    return sidebar_;
}

void WizardWindow::buildFooter() {
    // [Reset] [Back, Error detail, primary]: one row while they fit, otherwise two
    // rows (Windows computeButtonFooterLayout).
    auto footer = mac::makeFooter();
    reset_ = mac::makeButton("Reset", mac::ButtonRole::destructive,
                             [this] { deferred_.post([this] { controller_.cancel(true); }); });
    back_ = mac::makeButton("Back", mac::ButtonRole::secondary, [this] { controller_.back(); });
    detailButton_ = mac::makeButton("Error detail", mac::ButtonRole::destructive, [this] { openErrorDetail(); });
    next_ = mac::makeButton(text(wizardText::verify), mac::ButtonRole::primary,
                            [this] { deferred_.post([this] { controller_.next(); }); });
    [footer.leading addView:reset_ item:mac::withMinWidth(mac::natural(), ui::buttonMinWidth)];
    // Reading and keyboard order: Reset, Back, Error detail, primary.
    [footer.trailing addView:back_ item:mac::withMinWidth(mac::natural(), ui::buttonMinWidth)];
    [footer.trailing addView:detailButton_ item:mac::withMinWidth(mac::natural(), kDetailMinWidth)];
    [footer.trailing addView:next_ item:mac::withMinWidth(mac::natural(), kPrimaryMinWidth)];
    back_.hidden = YES;
    detailButton_.hidden = YES;
    footer.bar.fillColor = mac::color(ui::Color::background);
    [root_ addView:footer.bar];
}

PVStackView* WizardWindow::page() { return mac::makeStack(true, ui::relatedGap); }

WizardWindow::ErrorPanel WizardWindow::errorPanel() {
    ErrorPanel result;
    result.card = mac::makePanel(mac::PanelTone::error);
    [result.card addView:label("Could not complete this action", ui::Font::card, ui::Color::error)];
    result.text = label("", ui::Font::label, ui::Color::error);
    result.text.selectable = YES;
    [result.card addView:result.text];
    result.card.hidden = YES;
    return result;
}

NSTextField* WizardWindow::entry(WizardField field, const char* accessibleName) {
    NSTextField* widget = mac::makeEntry(ui::Font::value, accessibleName);
    __weak NSTextField* weakWidget = widget;
    mac::setTextChanged(widget, [this, field, weakWidget] {
        NSTextField* strongWidget = weakWidget;
        if (updating_ || !window_ || !strongWidget) return;
        auto value = mac::strictUtf8(strongWidget.stringValue);
        // An unpaired surrogate cannot be stored: the draft keeps its last value.
        if (!value) return;
        switch (field) {
            case WizardField::url: controller_.editUrl(std::move(*value)); break;
            case WizardField::secret: controller_.editSecret(std::move(*value)); break;
            case WizardField::ip: controller_.editIp(std::move(*value)); break;
            case WizardField::key: controller_.editKey(std::move(*value)); break;
            default: break;
        }
    });
    return widget;
}

mac::SwitchRow WizardWindow::toggle(WizardField field, const std::string& title) {
    // A visual switch with the checkbox semantics of Windows: the label is part of
    // the click target, Space toggles, the accessible name is the label.
    return mac::makeSwitchRow(title, [this, field](bool active) {
        if (updating_ || !window_) return;
        switch (field) {
            case WizardField::defaultSecret: controller_.setDefaultSecret(active); break;
            case WizardField::automaticIp: controller_.setAutoDetectIp(active); break;
            case WizardField::defaultKey: controller_.setDefaultKey(active); break;
            case WizardField::contracts: controller_.setCreateContracts(active); break;
            default: break;
        }
        // A refused change (locked) restores the model value.
        if (window_) render();
    });
}

PVStackView* WizardWindow::buildMessagePage() {
    PVStackView* box = page();
    messageEyebrow_ = label("", ui::Font::subtitle, ui::Color::secondaryText);
    messageHeading_ = label("", ui::Font::heading);
    messageText_ = label("");
    messageSecond_ = label("");
    gateCard_ = mac::makePanel(mac::PanelTone::warning);
    [gateCard_ addView:label(text(wizardText::gateWarning), ui::Font::label, ui::Color::warning)];
    [gateCard_ addView:label(text(wizardText::gateImpact), ui::Font::label, ui::Color::warning)];
    offline_ = label(text(wizardText::offline));
    messageError_ = errorPanel();
    for (NSView* child : {(NSView*)messageEyebrow_, (NSView*)messageHeading_, (NSView*)messageText_,
                          (NSView*)messageSecond_, (NSView*)gateCard_, (NSView*)offline_, (NSView*)messageError_.card})
        [box addView:child];
    for (NSView* child : {(NSView*)messageEyebrow_, (NSView*)messageSecond_, (NSView*)gateCard_, (NSView*)offline_})
        child.hidden = YES;
    return box;
}

PVStackView* WizardWindow::buildUrlPage() {
    PVStackView* box = page();
    [box addView:label(text(wizardText::urlHeading), ui::Font::heading)];
    [box addView:label(text(wizardText::urlDescription))];
    PVStackView* content = mac::makeCard();
    [content addView:label(text(wizardText::urlLabel))];
    url_ = entry(WizardField::url, "Server URL");
    [content addView:url_];
    [content addView:label(text(wizardText::urlHint), ui::Font::hint, ui::Color::secondaryText)];
    // The tinted example (UX09).
    PVStackView* example = mac::makeStack(true, 0, NSEdgeInsetsMake(6, 8, 6, 8));
    example.fillColor = mac::color(ui::Color::disabledBackground);
    example.cornerRadius = 4;
    [example addView:label(text(wizardText::urlExample), ui::Font::hint, ui::Color::secondaryText)];
    [content addView:example];
    [content addView:mac::makeSeparator() item:mac::withGap(mac::fixedHeight(1), ui::relatedGap)];
    [content addView:label(text(wizardText::headersHeading)) item:mac::withGap(mac::StackItem{}, ui::relatedGap)];
    headersButton_ = mac::makeButton(text(wizardText::headersButton), mac::ButtonRole::primary,
                                     [this] { deferred_.post([this] { openHeaders(); }); });
    [content addView:headersButton_ item:mac::withMinWidth(mac::natural(), kHeadersButtonWidth)];
    [content addView:label(text(wizardText::headersHint), ui::Font::hint, ui::Color::secondaryText)];
    [content addView:mac::makeSeparator() item:mac::withGap(mac::fixedHeight(1), ui::relatedGap)];
    [content addView:label(text(wizardText::urlRules)) item:mac::withGap(mac::StackItem{}, ui::relatedGap)];
    [content addView:label(text(wizardText::urlCheck))];
    [box addView:content];
    urlError_ = errorPanel();
    [box addView:urlError_.card];
    return box;
}

PVStackView* WizardWindow::buildSecretPage() {
    PVStackView* box = page();
    [box addView:label(text(wizardText::secretHeading), ui::Font::heading)];
    [box addView:label(text(wizardText::secretDescription))];
    PVStackView* content = mac::makeCard();
    secretLabel_ = label(text(wizardText::secretLabel));
    secret_ = entry(WizardField::secret, "Server secret");
    secretHint_ = label(text(wizardText::secretHint), ui::Font::hint, ui::Color::secondaryText);
    const auto row = toggle(WizardField::defaultSecret, text(wizardText::defaultSecret));
    defaultSecret_ = row.toggle;
    [content addView:secretLabel_];
    [content addView:secret_];
    [content addView:secretHint_];
    [content addView:row.row];
    [content addView:mac::makeSeparator() item:mac::withGap(mac::fixedHeight(1), ui::relatedGap)];
    [content addView:label(text(wizardText::secretCheck)) item:mac::withGap(mac::StackItem{}, ui::relatedGap)];
    [box addView:content];
    secretError_ = errorPanel();
    [box addView:secretError_.card];
    defaultSecretWarning_ = mac::makePanel(mac::PanelTone::warning);
    [defaultSecretWarning_ addView:label(text(wizardText::defaultSecretWarningHeading), ui::Font::card, ui::Color::warning)];
    [defaultSecretWarning_ addView:label(text(wizardText::defaultSecretWarning), ui::Font::label, ui::Color::warning)];
    defaultSecretWarning_.hidden = YES;
    [box addView:defaultSecretWarning_];
    return box;
}

PVStackView* WizardWindow::buildIdentityPage() {
    PVStackView* box = page();
    [box addView:label(text(wizardText::identityHeading), ui::Font::heading)];
    identityDescription_ = label("");
    [box addView:identityDescription_];
    PVStackView* content = mac::makeCard();
    ip_ = entry(WizardField::ip, "Virtual IP address");
    const auto ipRow = toggle(WizardField::automaticIp, text(wizardText::automaticIp));
    automaticIp_ = ipRow.toggle;
    noSuggestion_ = label(text(wizardText::noSuggestion), ui::Font::label, ui::Color::warning);
    noSuggestion_.hidden = YES;
    key_ = entry(WizardField::key, "Access key");
    const auto keyRow = toggle(WizardField::defaultKey, text(wizardText::defaultKey));
    defaultKey_ = keyRow.toggle;
    [content addView:label(text(wizardText::ipLabel))];
    [content addView:ip_];
    [content addView:ipRow.row];
    [content addView:noSuggestion_];
    [content addView:mac::makeSeparator() item:mac::withGap(mac::fixedHeight(1), ui::relatedGap)];
    [content addView:label(text(wizardText::keyLabel)) item:mac::withGap(mac::StackItem{}, ui::relatedGap)];
    [content addView:key_];
    [content addView:keyRow.row];
    [content addView:label(text(wizardText::keyHint), ui::Font::hint, ui::Color::secondaryText)];
    [box addView:content];
    identityError_ = errorPanel();
    [box addView:identityError_.card];
    defaultKeyWarning_ = mac::makePanel(mac::PanelTone::warning);
    [defaultKeyWarning_ addView:label(text(wizardText::defaultKeyWarningHeading), ui::Font::card, ui::Color::warning)];
    [defaultKeyWarning_ addView:label(text(wizardText::defaultKeyWarning), ui::Font::label, ui::Color::warning)];
    defaultKeyWarning_.hidden = YES;
    [box addView:defaultKeyWarning_];
    return box;
}

PVStackView* WizardWindow::buildContractsPage() {
    PVStackView* box = page();
    [box addView:label(text(wizardText::contractsHeading), ui::Font::heading)];
    [box addView:label(text(wizardText::contractsDescription))];
    PVStackView* content = mac::makeCard();
    const auto row = toggle(WizardField::contracts, text(wizardText::addContracts));
    contracts_ = row.toggle;
    [content addView:row.row];
    [content addView:mac::makeSeparator() item:mac::withGap(mac::fixedHeight(1), ui::relatedGap)];
    // Three columns; they move below each other on a narrow window (UX09 step 4).
    PVStackView* columns = mac::makeStack(false, ui::relatedGap);
    columns.homogeneous = YES;
    columns.wrapsWhenNarrow = YES;
    const char* headings[]{"Existing contracts:", "Missing defaults:", "To add on Finish:"};
    for (std::size_t i = 0; i < 3; ++i) {
        PVStackView* column = mac::makeStack(true, ui::fieldGap);
        [column addView:mac::makeLabel(headings[i], ui::Font::label) item:mac::natural()];
        contractCounts_[i] = mac::makeLabel("0", ui::Font::counter);
        [column addView:contractCounts_[i] item:mac::natural()];
        [columns addView:column];
    }
    [content addView:columns item:mac::withGap(mac::StackItem{}, ui::relatedGap)];
    [box addView:content];
    contractsUnsupported_ = label(text(wizardText::contractsUnsupported));
    contractsUnsupported_.hidden = YES;
    [box addView:contractsUnsupported_];
    contractsHint_ = mac::makePanel(mac::PanelTone::warning);
    [contractsHint_ addView:label(text(wizardText::contractsHint), ui::Font::label, ui::Color::warning)];
    contractsHint_.hidden = YES;
    [box addView:contractsHint_];
    contractsError_ = errorPanel();
    [box addView:contractsError_.card];
    return box;
}

// --- rendering ------------------------------------------------------------------------------
void WizardWindow::updateErrorPanel(const ErrorPanel& panel, const WizardPresentation& p) {
    mac::setVisible(panel.card, p.errorText.has_value());
    mac::setLabelText(panel.text, p.errorText.value_or(std::string{}));
}

void WizardWindow::renderSidebar(const WizardPresentation& p) {
    for (std::size_t i = 0; i < 4; ++i) {
        const auto& item = p.sidebar[i];
        auto color = item.color;
        std::string symbol = item.symbol;
        if (compact_) {
            // Windows compact strip: numbers, a check mark only for other completed steps.
            if (item.active) color = ui::Color::primary;
            symbol = !item.active && item.status == "Completed" ? "✓" : std::to_string(i + 1);
        }
        NSString* marker = mac::ns(symbol);
        if (![stepMarkers_[i].text isEqualToString:marker]) stepMarkers_[i].text = marker;
        stepMarkers_[i].circleColor = mac::color(color);
        mac::setLabelText(stepTexts_[i], item.status.empty() ? item.name : item.name + "\n" + item.status);
        mac::setTextColor(stepTexts_[i], color);
        stepTexts_[i].font = mac::font(item.active ? ui::Font::strongValue : ui::Font::label);
        stepRows_[i].fillColor = item.active && !compact_ ? mac::color(ui::Color::selectedStep) : nil;
        [stepRows_[i] setAccessibilityLabel:mac::ns(std::to_string(i + 1) + " " + item.name +
                                                    (item.status.empty() ? "" : ", " + item.status))];
    }
    [sidebar_ setAccessibilityHelp:mac::ns(p.sidebarText)];
}

void WizardWindow::render() {
    if (!window_) return;
    const auto p = controller_.presentation();
    const bool stepChanged = !rendered_ || renderedStep_ != p.step;
    renderedStep_ = p.step;
    rendered_ = true;
    if (p.step != WizardStep::serverUrl) headers_.close(false);
    updating_ = true;
    renderSidebar(p);
    mac::setVisible(messagePage_, p.step != WizardStep::serverUrl && p.step != WizardStep::serverSecret &&
                                      p.step != WizardStep::identity && p.step != WizardStep::contracts);
    mac::setVisible(urlPage_, p.step == WizardStep::serverUrl);
    mac::setVisible(secretPage_, p.step == WizardStep::serverSecret);
    mac::setVisible(identityPage_, p.step == WizardStep::identity);
    mac::setVisible(contractsPage_, p.step == WizardStep::contracts);

    // Message page.
    mac::setLabelText(messageEyebrow_, p.eyebrow);
    mac::setVisible(messageEyebrow_, !p.eyebrow.empty());
    mac::setLabelText(messageHeading_, p.heading);
    mac::setLabelText(messageText_, p.text);
    mac::setLabelText(messageSecond_, p.secondText);
    mac::setVisible(messageSecond_, !p.secondText.empty());
    mac::setVisible(gateCard_, p.gateCard);
    mac::setVisible(offline_, p.offline && p.step == WizardStep::disconnectGate);
    for (const auto* panel : {&messageError_, &urlError_, &secretError_, &identityError_, &contractsError_})
        updateErrorPanel(*panel, p);

    // Fields keep their control, text and selection; only real changes are written.
    mac::setFieldText(url_, p.url);
    mac::setFieldText(secret_, p.secret);
    mac::setFieldText(ip_, p.ip);
    mac::setFieldText(key_, p.key);
    mac::setSwitchOn(defaultSecret_, p.useDefaultSecret);
    mac::setSwitchOn(automaticIp_, p.autoDetectIp);
    mac::setSwitchOn(defaultKey_, p.useDefaultKey);
    mac::setSwitchOn(contracts_, p.createContracts);
    url_.enabled = p.locked ? NO : YES;
    headersButton_.enabled = p.locked ? NO : YES;
    secret_.enabled = p.secretEnabled ? YES : NO;
    defaultSecret_.enabled = p.locked ? NO : YES;
    ip_.enabled = p.ipEnabled ? YES : NO;
    automaticIp_.enabled = p.locked ? NO : YES;
    key_.enabled = p.keyEnabled ? YES : NO;
    defaultKey_.enabled = p.locked ? NO : YES;
    contracts_.enabled = p.contractsEnabled ? YES : NO;
    const std::pair<WizardField, NSTextField*> fields[]{
        {WizardField::url, url_}, {WizardField::secret, secret_}, {WizardField::ip, ip_}, {WizardField::key, key_}};
    for (const auto& [field, widget] : fields) {
        const bool invalid = p.invalidField == field;
        mac::setFieldInvalid(widget, invalid, invalid && p.errorText ? "Invalid. " + *p.errorText : std::string{});
    }
    mac::setLabelText(secretLabel_, p.secretLabel);
    mac::setLabelText(secretHint_, p.secretHint);
    mac::setVisible(defaultSecretWarning_, p.defaultSecretWarning);
    mac::setLabelText(identityDescription_, p.identityDescription);
    mac::setVisible(noSuggestion_, p.noSuggestion);
    mac::setVisible(defaultKeyWarning_, p.defaultKeyWarning);
    for (std::size_t i = 0; i < 3; ++i) mac::setLabelText(contractCounts_[i], std::to_string(p.contractCounts[i]));
    mac::setVisible(contractsUnsupported_, p.contractsUnsupported);
    mac::setVisible(contractsHint_, p.contractsHint);
    mac::setLabelText(status_, p.statusText);
    mac::setVisible(status_, p.locked);

    // Footer.
    mac::setButtonTitle(next_, p.primaryLabel);
    next_.enabled = p.primaryEnabled ? YES : NO;
    mac::setVisible(back_, p.backVisible);
    back_.enabled = p.backEnabled ? YES : NO;
    mac::setVisible(detailButton_, p.detailVisible);
    detailButton_.enabled = p.detailEnabled ? YES : NO;
    reset_.enabled = p.resetEnabled ? YES : NO;
    updating_ = false;
    [root_ relayout];

    if (stepChanged) [scroller_.contentView scrollToPoint:NSZeroPoint];
    if (stepChanged) [scroller_ reflectScrolledClipView:scroller_.contentView];
    // Focus: preserved in the owned windows and on an unchanged step, otherwise the
    // first enabled field of the step (Windows GetNextDlgTabItem, then primary).
    if (ownedWindowActive()) return;
    NSResponder* focus = window_.firstResponder;
    bool usable = false;
    if ([focus isKindOfClass:[NSView class]]) {
        NSView* view = (NSView*)focus;
        // The field editor stands for its text field.
        if ([view isKindOfClass:[NSTextView class]] && ((NSTextView*)view).fieldEditor)
            view = (NSView*)((NSTextView*)view).delegate;
        usable = [view isKindOfClass:[NSView class]] && enabledAndVisible(view);
    }
    if (!stepChanged && usable) return;
    if (NSView* first = firstFocusable()) [window_ makeFirstResponder:first];
}

bool WizardWindow::ownedWindowActive() const { return headers_.window() != nil || detail_.active(); }

NSView* WizardWindow::firstFocusable() const {
    std::vector<NSView*> order;
    switch (renderedStep_) {
        case WizardStep::serverUrl: order = {url_, headersButton_}; break;
        case WizardStep::serverSecret: order = {secret_, defaultSecret_}; break;
        case WizardStep::identity: order = {ip_, automaticIp_, key_, defaultKey_}; break;
        case WizardStep::contracts: order = {contracts_}; break;
        // tray_UX.md UX09: an accidental Enter must not confirm the global Disconnect,
        // so the gate starts on the non-destructive Reset.
        case WizardStep::disconnectGate: order = {reset_}; break;
        default: break;
    }
    order.push_back(next_);
    order.push_back(reset_);
    for (NSView* view : order)
        if (enabledAndVisible(view)) return view;
    return nil;
}

void WizardWindow::refreshDiagnostic() {
    if (!window_) return;
    mac::setVisible(detailButton_, controller_.model().httpDiagnostic().has_value());
    [root_ relayout];
}

NSView* WizardWindow::widgetFor(WizardField field) const {
    switch (field) {
        case WizardField::url: return url_;
        case WizardField::secret: return secret_;
        case WizardField::defaultSecret: return defaultSecret_;
        case WizardField::ip: return ip_;
        case WizardField::automaticIp: return automaticIp_;
        case WizardField::key: return key_;
        case WizardField::defaultKey: return defaultKey_;
        case WizardField::contracts: return contracts_;
        case WizardField::headers: return headersButton_;
        case WizardField::none: break;
    }
    return nil;
}

void WizardWindow::setFieldText(WizardField field, const std::string& value) {
    NSView* widget = widgetFor(field);
    if (!window_ || ![widget isKindOfClass:[NSTextField class]]) return;
    updating_ = true;
    mac::setFieldText((NSTextField*)widget, value);
    updating_ = false;
}

void WizardWindow::focusField(WizardField field, WizardField fallback) {
    if (!window_) return;
    NSView* widget = widgetFor(field);
    if (!enabledAndVisible(widget)) widget = widgetFor(fallback);
    if (enabledAndVisible(widget)) [window_ makeFirstResponder:widget];
}

void WizardWindow::notify(std::string_view message, std::string_view title, WizardNotice icon) {
    mac::showMessage(message, title, dialogIcon(icon));
}

bool WizardWindow::confirm(std::string_view question, std::string_view title) {
    return mac::confirm(question, title, false);
}

// --- owned windows ---------------------------------------------------------------------------
void WizardWindow::openHeaders() {
    if (!window_ || !controller_.acceptsInput() || controller_.presentation().locked) return;
    const bool opened = headers_.open(
        window_, controller_.model().fields().httpHeaders,
        [this](std::string value) { return controller_.acceptHttpHeaders(std::move(value)); },
        [this] {
            if (window_ && headersButton_) [window_ makeFirstResponder:headersButton_];
        });
    if (!opened) mac::showError("The HTTP headers editor could not be opened.", "Configuration Wizard");
}

void WizardWindow::openErrorDetail() {
    if (!window_) return;
    if (const auto& diagnostic = controller_.model().httpDiagnostic()) detail_.open(window_, *diagnostic);
}

// --- layout and teardown -----------------------------------------------------------------------
void WizardWindow::placeCompact() {
    if (!window_) return;
    const bool compact = window_.contentView.bounds.size.width < kCompactWidth;
    if (compact != compact_) {
        compact_ = compact;
        // Below 760 DIP the four steps become a strip of numbers above the content.
        body_.vertical = compact ? YES : NO;
        divider_.vertical = compact ? NO : YES;
        mac::StackItem sidebarItem = compact ? mac::fixedHeight(kCompactHeight) : mac::fixedWidth(kSidebarWidth);
        sidebarItem.fill = true;
        [body_ setItem:sidebarItem forView:sidebar_];
        mac::StackItem dividerItem = compact ? mac::fixedHeight(1) : mac::fixedWidth(1);
        dividerItem.fill = true;
        [body_ setItem:dividerItem forView:divider_];
        sidebar_.vertical = compact ? NO : YES;
        sidebar_.homogeneous = compact ? YES : NO;
        mac::setVisible(sidebarHeading_, !compact);
        for (std::size_t i = 0; i < 4; ++i) {
            mac::setVisible(stepTexts_[i], !compact);
            mac::StackItem rowItem = compact ? mac::StackItem{} : mac::withMinHeight(mac::StackItem{}, 80);
            if (!compact && i == 0) rowItem.gapBefore = 24;
            [sidebar_ setItem:rowItem forView:stepRows_[i]];
        }
    }
    render();
}

void WizardWindow::destroyWindow() {
    if (!window_) return;
    headers_.close(false);
    detail_.close();
    timer_.stop();
    // Entry texts (including the server secret) leave the controls first.
    updating_ = true;
    for (NSTextField* field : {url_, secret_, ip_, key_})
        if (field) field.stringValue = @"";
    updating_ = false;
    PVWindow* window = window_;
    window_ = nil;
    mac::closeWindow(window);
    clearPointers();
}

void WizardWindow::clearPointers() {
    root_ = body_ = sidebar_ = nil;
    sidebarHeading_ = nil;
    divider_ = nil;
    scroller_ = nil;
    content_ = messagePage_ = urlPage_ = secretPage_ = identityPage_ = contractsPage_ = nil;
    status_ = nil;
    reset_ = back_ = detailButton_ = next_ = nil;
    messageEyebrow_ = messageHeading_ = messageText_ = messageSecond_ = offline_ = nil;
    gateCard_ = nil;
    messageError_ = urlError_ = secretError_ = identityError_ = contractsError_ = ErrorPanel{};
    url_ = secretLabel_ = secret_ = secretHint_ = identityDescription_ = ip_ = noSuggestion_ = key_ = nil;
    contractsUnsupported_ = nil;
    headersButton_ = nil;
    defaultSecret_ = automaticIp_ = defaultKey_ = contracts_ = nil;
    defaultSecretWarning_ = defaultKeyWarning_ = contractsHint_ = nil;
    stepRows_ = {};
    stepMarkers_ = {};
    stepTexts_ = {};
    contractCounts_ = {};
    rendered_ = false;
    compact_ = false;
}

// --- application wiring ----------------------------------------------------------------------------
namespace {
struct WizardWindows {
    WizardWindows(const IpcEndpoint& endpoint, const Dispatcher& dispatcher) : wizard(endpoint, dispatcher) {}
    WizardWindow wizard;
    // The Connect progress window's own HTTP error detail (Windows ProgressWindow::errorDetail_).
    HttpErrorDetailWindow connectDetail;
};
std::unique_ptr<WizardWindows>& installedWindows() {
    static std::unique_ptr<WizardWindows> windows;
    return windows;
}
}  // namespace

void installWizardWindows(TrayApp& app) {
    auto& windows = installedWindows();
    windows = std::make_unique<WizardWindows>(app.endpoint(), app.dispatcher());
    auto* installed = windows.get();
    app.hooks().openWizard = [installed] { return installed->wizard.open(); };
    app.hooks().openHttpErrorDetail = [installed](NSWindow* parent, const HttpDiagnostic& diagnostic) {
        if (!installed->connectDetail.open(parent, diagnostic))
            mac::showMessage(kErrorDetailUnavailableText, "PHPVPN");
    };
    app.hooks().closeHttpErrorDetail = [installed] { installed->connectDetail.close(); };
    // First participant: its approvedDiscard veto is checked before any other window.
    app.registerParticipant({&installed->wizard.controller(), [installed] { return installed->wizard.window(); }});
}
}  // namespace phpvpn::tray
