#include "mac_progress_window.h"

#include "macos_text.h"

namespace phpvpn::tray {
namespace {
constexpr CGFloat kCountdownBesideWidth = 760;  // tray_UX.md UX05, as computeProgressLayout
const char* outcome(CheckpointResult check) {
    return check == CheckpointResult::success ? "Success" : check == CheckpointResult::failed ? "Failed" : "Pending";
}
const char* glyph(CheckpointResult check) {
    return check == CheckpointResult::success ? "✓" : check == CheckpointResult::failed ? "✗" : "●";
}
ui::Color checkColor(CheckpointResult check) {
    return check == CheckpointResult::success ? ui::Color::success
         : check == CheckpointResult::failed ? ui::Color::error : ui::Color::warning;
}
std::string closingText(unsigned seconds) {
    return "Closing in " + std::to_string(seconds) + (seconds == 1 ? " second" : " seconds");
}
}  // namespace

ProgressWindow::ProgressWindow(IpcEndpoint endpoint, Dispatcher dispatcher)
    : endpoint_(endpoint), dispatcher_(dispatcher),
      reader_(endpoint, dispatcher, [this](const TrayStatus& status) {
          readPending_ = false;
          onSnapshot(status);
      }),
      detailsReader_(endpoint, dispatcher), dismissReader_(endpoint, dispatcher) {}

ProgressWindow::~ProgressWindow() {
    reader_.abandon();
    detailsReader_.abandon();
    if (window_) mac::closeWindow(window_);
}

void ProgressWindow::build() {
    window_ = mac::makeWindow(disconnect_ ? "PHPVPN disconnection" : "PHPVPN connection",
                              disconnect_ ? ui::disconnectClient : ui::connectClient,
                              disconnect_ ? Size{400, 160} : Size{480, 320});
    root_ = mac::makeRoot(window_);
    // The whole content scrolls vertically on a small screen (UX03).
    PVStackView* content = mac::makeStack(true, ui::fieldGap,
                                          NSEdgeInsetsMake(ui::contentMargin, ui::compactMargin, ui::contentMargin,
                                                           ui::compactMargin));
    scroller_ = [[PVScrollView alloc] initWithStack:content];
    [root_ addView:scroller_ item:mac::flexibleItem()];

    // Text only, like the Win32 window: the eyebrow, the check glyphs and the coloured
    // result panel carry the state, no decorative state symbol.
    eyebrow_ = mac::makeLabel("", ui::Font::subtitle, ui::Color::secondaryText, true);
    [content addView:eyebrow_];

    card_ = mac::makeCard(ui::cardPadding, 0);
    [card_ addView:mac::makeLabel("Connection details", ui::Font::card)];
    const char* names[]{"Server URL", "Virtual IP"};
    for (std::size_t i = 0; i < 2; ++i) {
        PVStackView* row = mac::makeStack(false, 16);
        NSTextField* name = mac::makeLabel(names[i], ui::Font::label);
        [row addView:name item:mac::fixedWidth(160)];
        // Read-only but focusable, selectable and copyable (UX05); never editable.
        NSTextField* field = mac::makeReadOnlyField("Unavailable", i == 0 ? ui::Font::mono : ui::Font::value, names[i]);
        mac::StackItem fieldItem = mac::flexibleItem();
        fieldItem.fill = false;
        [row addView:field item:fieldItem];
        [card_ addView:row item:mac::withGap(mac::StackItem{}, i == 0 ? 16 : ui::fieldGap)];
        fields_[i] = field;
    }
    [card_ addView:mac::makeSeparator() item:mac::withGap(mac::fixedHeight(1), 16)];
    [card_ addView:mac::makeLabel("Connection checks", ui::Font::card) item:mac::withGap(mac::StackItem{}, 20)];
    const char* checkNames[]{"Server URL", "Server secret", "Access key", "Communication contracts"};
    for (std::size_t i = 0; i < 4; ++i) {
        PVStackView* row = mac::makeStack(false, ui::relatedGap);
        NSTextField* icon = mac::makeLabel("", ui::Font::value);
        [row addView:icon item:mac::fixedWidth(28)];
        [row addView:mac::makeLabel(checkNames[i], ui::Font::value) item:mac::fixedWidth(228)];
        NSTextField* state = mac::makeLabel("", ui::Font::value);
        mac::StackItem stateItem = mac::flexibleItem();
        stateItem.fill = false;
        [row addView:state item:stateItem];
        [card_ addView:row item:mac::withGap(mac::StackItem{}, i == 0 ? 16 : ui::fieldGap)];
        checkIcons_[i] = icon;
        checks_[i] = state;
    }
    [content addView:card_ item:mac::withGap(mac::StackItem{}, ui::fieldGap)];

    panel_ = mac::makePanel(mac::PanelTone::warning, 0);
    PVStackView* line = mac::makeStack(false, 16);
    panelTexts_ = mac::makeStack(true, ui::spacing);
    panelHeading_ = [PVPaintLabel wrappingLabelWithString:@""];
    panelHeading_.font = mac::font(ui::Font::card);
    panelHeading_.textColor = mac::color(ui::Color::text);
    panelHeading_.editable = NO;
    panelHeading_.selectable = NO;
    panelHeading_.drawsBackground = NO;
    panelHeading_.bezeled = NO;
    [panelHeading_ setPaintHandler:[this] { headingPainted(); }];
    panelAction_ = mac::makeLabel("", ui::Font::label, ui::Color::text, true);
    countdownUnder_ = mac::makeLabel("", ui::Font::value);
    [panelTexts_ addView:panelHeading_];
    [panelTexts_ addView:panelAction_];
    [panelTexts_ addView:countdownUnder_ item:mac::withGap(mac::natural(), ui::relatedGap)];
    mac::StackItem textsItem = mac::flexibleItem();
    textsItem.fill = false;
    [line addView:panelTexts_ item:textsItem];
    countdownSide_ = mac::makeLabel("", ui::Font::value);
    countdownSide_.alignment = NSTextAlignmentRight;
    [line addView:countdownSide_ item:mac::trailingItem()];
    errorDetail_ = mac::makeButton("Error detail", mac::ButtonRole::destructive, [this] {
        // The fallback message is modal; it never runs inside the click action.
        deferred_.post([this] {
            const auto& diagnostic = model_.httpDiagnostic();
            if (!window_ || !diagnostic) return;
            if (openErrorDetail) openErrorDetail(window_, *diagnostic);
            else mac::showMessage(kErrorDetailUnavailableText, "PHPVPN");
        });
    });
    [line addView:errorDetail_ item:mac::trailingItem()];
    [panel_ addView:line];
    [content addView:panel_];

    // X and Escape close only the progress presentation, never Disconnect (UX05).
    [window_ setCloseHandler:[this] { requestClose(); }];
    [window_ setKeyHandler:[this](NSEvent* event) {
        if (mac::isEscape(event)) {
            requestClose();
            return true;
        }
        // Enter alone connects, retries or disconnects nothing.
        return mac::isReturn(event) && ![window_.firstResponder isKindOfClass:[NSButton class]];
    }];
    [window_ setResizeHandler:[this] { placeCountdown(); }];
    countdownBeside_ = true;
    mac::setVisible(countdownUnder_, false);
    mac::setVisible(errorDetail_, false);
}

void ProgressWindow::placeCountdown() {
    if (!window_) return;
    countdownBeside_ = window_.contentView.bounds.size.width >= kCountdownBesideWidth;
    refresh();
}

void ProgressWindow::open(bool disconnect) {
    if (closeErrorDetail) closeErrorDetail();
    diagnosticEpoch_ = 0;
    diagnosticAttemptId_.clear();
    reader_.abandon();
    detailsReader_.abandon();
    readPending_ = false;
    // The Connect and Disconnect presentations have different window sizes.
    if (window_ && disconnect_ != disconnect) destroy();
    disconnect_ = disconnect;
    waitingForReply_ = disconnect;
    model_ = ProgressModel{disconnect};
    details_ = {};
    if (!window_) build();
    window_.title = disconnect_ ? @"PHPVPN disconnection" : @"PHPVPN connection";
    mac::setLabelText(eyebrow_, disconnect_ ? "Disconnecting client from the server."
                                            : "Connecting client to the server.");
    mac::setVisible(card_, !disconnect_);
    countdownBeside_ = window_.contentView.bounds.size.width >= kCountdownBesideWidth;
    refresh();
    polling_.start(monotonicMs());
    timer_.start(kProgressIntervalMs, [this] { tick(); });
    mac::presentWindow(window_);
    // One separate read per local Connect. It cannot change the bootstrap model.
    if (!disconnect_ && !detailsReader_.request(IpcCommand::get_config, {}, [this](std::string& response) {
            details_ = consumeProgressConfig(std::move(response));
            refresh();
        }))
        details_ = {};
    // The first status read is due in 500 ms. The command reply seeds the view.
}

void ProgressWindow::onDisconnectReply(const TrayStatus& snapshot) {
    if (!window_ || !disconnect_ || !waitingForReply_) return;
    waitingForReply_ = false;
    if (!snapshot.valid) {
        model_.onCommandFailure();
        polling_.stop();
        refresh();
        return;
    }
    onSnapshot(snapshot);
}

void ProgressWindow::onDisconnectTimeout() {
    if (!window_ || !disconnect_) return;
    waitingForReply_ = false;
    reader_.abandon();
    readPending_ = false;
    polling_.stop();
    model_.onCommandFailure();
    refresh();
}

void ProgressWindow::endSession() {
    // Revoke deliveries before destroying the window.
    reader_.abandon();
    detailsReader_.abandon();
    destroy();
}

void ProgressWindow::requestClose() {
    if (!window_) return;
    if (disconnectPending()) return;
    model_.onClose();
    destroy();
}

void ProgressWindow::refresh() {
    if (!window_) return;
    const bool diagnostic = model_.httpDiagnostic().has_value();
    mac::setVisible(errorDetail_, diagnostic);
    if (!diagnostic && closeErrorDetail) closeErrorDetail();
    const auto& v = model_.view();
    const bool success = v.stage == ProgressStage::succeeded, failure = v.stage == ProgressStage::failed;
    const auto& error = progressErrorText(v.statusUnavailable ? "status_unavailable" : v.errorCode);
    bool waiting = false;
    bool panelVisible = false;
    if (disconnect_) {
        const bool issue = failure || v.statusUnavailable || !v.errorCode.empty();
        waiting = !success && !issue;
        mac::setLabelText(panelHeading_, success ? "Disconnected" : issue ? "Disconnect not confirmed" : "Disconnecting...");
        mac::setLabelText(panelAction_,
                          success ? "You have been disconnected successfully."
                          : issue ? "The service did not confirm Disconnect. Try Disconnect again or check the service status."
                                  : "Please wait while the VPN disconnects.");
        panelVisible = true;
        mac::setCloseEnabled(window_, success || issue);
    } else {
        mac::setFieldText(fields_[0], details_.available ? details_.serverUrl : "Unavailable");
        mac::setFieldText(fields_[1], details_.available ? details_.virtualIp : "Unavailable");
        const CheckpointResult checks[]{v.serverUrl, v.serverSecret, v.accessKey, v.communicationContracts};
        for (std::size_t i = 0; i < 4; ++i) {
            mac::setLabelText(checks_[i], outcome(checks[i]));
            mac::setLabelText(checkIcons_[i], glyph(checks[i]));
            mac::setTextColor(checks_[i], checkColor(checks[i]));
            mac::setTextColor(checkIcons_[i], checkColor(checks[i]));
        }
        mac::setLabelText(panelHeading_, success ? "Connected successfully"
                                         : failure || v.statusUnavailable ? std::string(error.panel) : std::string{});
        mac::setLabelText(panelAction_, success ? "This window will close automatically."
                                        : failure || v.statusUnavailable ? std::string(error.action) : std::string{});
        panelVisible = success || failure || v.statusUnavailable;
        mac::setCloseEnabled(window_, true);
    }
    const auto countdown = success ? closingText(v.closingSeconds) : std::string{};
    mac::setLabelText(countdownSide_, countdown);
    mac::setLabelText(countdownUnder_, countdown);
    mac::setVisible(countdownSide_, success && countdownBeside_);
    mac::setVisible(countdownUnder_, success && !countdownBeside_);
    mac::setVisible(panel_, panelVisible);
    const auto tone = success ? mac::PanelTone::success : waiting ? mac::PanelTone::warning : mac::PanelTone::error;
    mac::setPanelTone(panel_, tone);
    const auto textColor = success ? ui::Color::success : waiting ? ui::Color::warning : ui::Color::error;
    panelHeading_.textColor = mac::color(textColor);
    mac::setTextColor(panelAction_, textColor);
    mac::setTextColor(countdownSide_, textColor);
    mac::setTextColor(countdownUnder_, textColor);
    [root_ relayout];
}

void ProgressWindow::headingPainted() {
    // The success countdown starts at the first real paint of the visible heading of
    // a window that is on screen and not minimised (UX05); the model ignores repeats.
    if (!window_ || model_.view().stage != ProgressStage::succeeded || panel_.hidden) return;
    if (!window_.visible || window_.miniaturized || !(window_.occlusionState & NSWindowOcclusionStateVisible)) return;
    model_.onSuccessPresented(monotonicMs());
}

void ProgressWindow::onSnapshot(const TrayStatus& snapshot) {
    if (!window_) return;
    model_.onSnapshot(snapshot, monotonicMs());
    if (disconnect_ && disconnectSnapshot) disconnectSnapshot(snapshot);
    if (!window_) return;
    if (model_.httpDiagnostic()) {
        diagnosticEpoch_ = model_.connectionEpoch();
        diagnosticAttemptId_ = model_.connectAttemptId().value_or("");
    }
    if (model_.view().closeRequested) {
        destroy();
        return;
    }
    refresh();
}

void ProgressWindow::tick() {
    if (!window_) return;
    // A layer-backed label may be composited without a new drawRect; a visible
    // success heading on screen counts as presented at the latest on this tick.
    if (model_.view().stage == ProgressStage::succeeded && !model_.successDeadlineMs()) headingPainted();
    const auto before = model_.view().closingSeconds;
    model_.onTick(monotonicMs());
    if (model_.view().closeRequested) {
        destroy();
        return;
    }
    if (before != model_.view().closingSeconds) {
        const auto countdown = closingText(model_.view().closingSeconds);
        mac::setLabelText(countdownSide_, countdown);
        mac::setLabelText(countdownUnder_, countdown);
    }
    if (!waitingForReply_ && polling_.onTimer(monotonicMs()) && !readPending_ &&
        (model_.view().stage == ProgressStage::connecting || model_.httpDiagnostic())) {
        readPending_ = true;
        reader_.request();
    }
}

void ProgressWindow::destroy() {
    if (!window_) return;
    if (!disconnect_ && diagnosticEpoch_)
        dismissReader_.request(IpcCommand::dismiss_connect_diagnostic,
                               IpcProtocol::dismissDiagnosticPayload(diagnosticEpoch_, diagnosticAttemptId_), {});
    diagnosticEpoch_ = 0;
    diagnosticAttemptId_.clear();
    model_.onClose();
    if (closeErrorDetail) closeErrorDetail();
    timer_.stop();
    polling_.stop();
    reader_.abandon();
    detailsReader_.abandon();
    readPending_ = false;
    [panelHeading_ setPaintHandler:std::function<void()>{}];
    PVWindow* window = window_;
    window_ = nil;
    mac::closeWindow(window);
    root_ = nil;
    scroller_ = nil;
    eyebrow_ = nil;
    card_ = nil;
    fields_ = {};
    checks_ = {};
    checkIcons_ = {};
    panel_ = nil;
    panelTexts_ = nil;
    panelHeading_ = nil;
    panelAction_ = nil;
    countdownSide_ = nil;
    countdownUnder_ = nil;
    errorDetail_ = nil;
    details_ = {};
}
}  // namespace phpvpn::tray
