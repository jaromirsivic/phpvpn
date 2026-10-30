#include "mac_control_window.h"

#include <algorithm>

namespace phpvpn::tray {
namespace {
constexpr CGFloat kControlWidth = 420;
constexpr CGFloat kApplicationIconSize = 48;
}  // namespace

void ControlWindow::build() {
    window_ = mac::makeWindow("PHPVPN", Size{static_cast<int>(kControlWidth), 480}, Size{320, 200});
    root_ = mac::makeRoot(window_);
    PVStackView* content = mac::makeStack(true, ui::fieldGap,
                                          NSEdgeInsetsMake(ui::compactMargin, ui::compactMargin, ui::compactMargin,
                                                           ui::compactMargin));
    PVScrollView* scroller = [[PVScrollView alloc] initWithStack:content];
    [root_ addView:scroller item:mac::flexibleItem()];

    // The PHPVPN application icon, not a state symbol: the state is the text next to it.
    PVStackView* card = mac::makeCard(ui::relatedGap, ui::relatedGap);
    card.vertical = NO;
    NSImageView* icon = [NSImageView imageViewWithImage:[NSApp applicationIconImage]];
    icon.imageScaling = NSImageScaleProportionallyUpOrDown;
    mac::StackItem iconItem = mac::fixedWidth(kApplicationIconSize);
    iconItem.height = kApplicationIconSize;
    [card addView:icon item:iconItem];
    status_ = mac::makeLabel("", ui::Font::card, ui::Color::text, true);
    mac::StackItem statusItem = mac::flexibleItem();
    statusItem.fill = false;
    [card addView:status_ item:statusItem];
    [content addView:card];

    for (const auto& entry : controlWindowActions(view_)) {
        if (entry.kind == IndicatorMenuEntry::Kind::separator) {
            [content addView:mac::makeSeparator() item:mac::fixedHeight(1)];
            continue;
        }
        const auto action = entry.action;
        NSButton* button = mac::makeButton(entry.label, action == MenuAction::toggle ? mac::ButtonRole::primary
                                                                                      : mac::ButtonRole::secondary,
                                           [this, action] {
                                               // Same as the menu: a question may follow.
                                               deferred_.post([this, action] {
                                                   if (onAction_) onAction_(action);
                                               });
                                           });
        if (action == MenuAction::toggle) toggle_ = button;
        [content addView:button];
    }
    [window_ setCloseHandler:[this] { hide(); }];
    [window_ setKeyHandler:[this](NSEvent* event) {
        if (!mac::isEscape(event)) return false;
        hide();
        return true;
    }];
    // The height follows the content, so every button including Exit is visible.
    const CGFloat height = [content heightForWidth:kControlWidth];
    NSScreen* screen = mac::screenForNewWindow();
    const CGFloat available = screen ? screen.visibleFrame.size.height - 40 : height;
    [window_ setContentSize:NSMakeSize(kControlWidth, std::max<CGFloat>(200, std::min<CGFloat>(height, available)))];
    [window_ center];
}

void ControlWindow::show(const IndicatorView& view) {
    view_ = view;
    if (!window_) build();
    update(view);
    mac::presentWindow(window_);
}

void ControlWindow::update(const IndicatorView& view) {
    view_ = view;
    if (!window_) return;
    mac::setLabelText(status_, view.statusText);
    mac::setButtonTitle(toggle_, menuLabel(MenuAction::toggle, view.toggleDisconnects));
    [root_ relayout];
}

void ControlWindow::hide() {
    if (window_) [window_ orderOut:nil];
}

void ControlWindow::destroy() {
    if (!window_) return;
    mac::closeWindow(window_);
    window_ = nil;
    root_ = nil;
    status_ = nil;
    toggle_ = nil;
}
}  // namespace phpvpn::tray
