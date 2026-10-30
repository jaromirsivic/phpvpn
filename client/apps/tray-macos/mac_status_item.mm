#include "mac_status_item.h"

#include <string>

#include "macos_text.h"

namespace phpvpn::tray {
namespace {
struct Palette {
    std::string_view name;
    std::uint32_t frame, fill, center;
};
// The colours of assets/linux/hicolor/scalable/apps/phpvpn*.svg.
constexpr Palette kPalettes[]{
    {"phpvpn", 0x000099, 0xBBDEFE, 0x007EFE},
    {"phpvpn-disconnected", 0x990000, 0xFEC0C0, 0xFF0000},
    {"phpvpn-connecting", 0x994C00, 0xFFF372, 0xFF7D00},
    {"phpvpn-connected", 0x006600, 0xC6FFB3, 0x00DD00},
};
NSColor* rgb(std::uint32_t value) {
    return [NSColor colorWithSRGBRed:static_cast<CGFloat>((value >> 16) & 0xFF) / 255.0
                               green:static_cast<CGFloat>((value >> 8) & 0xFF) / 255.0
                                blue:static_cast<CGFloat>(value & 0xFF) / 255.0
                               alpha:1.0];
}
}  // namespace

NSImage* stateImage(std::string_view name) {
    NSImage* bundled = [[NSBundle mainBundle] imageForResource:mac::ns(name)];
    if (bundled) {
        NSImage* image = [bundled copy];
        image.size = NSMakeSize(16, 16);
        [image setTemplate:NO];
        return image;
    }
    Palette palette = kPalettes[1];
    for (const auto& candidate : kPalettes)
        if (candidate.name == name) palette = candidate;
    NSColor* frame = rgb(palette.frame);
    NSColor* fill = rgb(palette.fill);
    NSColor* center = rgb(palette.center);
    NSImage* drawn = [NSImage imageWithSize:NSMakeSize(16, 16) flipped:YES drawingHandler:^BOOL(NSRect rect) {
        // The SVG geometry (72 units) scaled to the rectangle.
        const CGFloat unit = rect.size.width / 72.0;
        [frame setFill];
        NSRectFill(rect);
        [fill setFill];
        NSRectFill(NSMakeRect(2.25 * unit, 2.25 * unit, 67.5 * unit, 67.5 * unit));
        [frame setFill];
        NSRectFill(NSMakeRect(28 * unit, 9 * unit, 16 * unit, 54 * unit));
        [center setFill];
        NSRectFill(NSMakeRect(13 * unit, 23 * unit, 46 * unit, 26 * unit));
        [frame setFill];
        NSRectFill(NSMakeRect(30 * unit, 31 * unit, 12 * unit, 10 * unit));
        return YES;
    }];
    [drawn setTemplate:NO];
    return drawn;
}

bool StatusItem::create(const IndicatorView& initial) {
    if (item_) return true;
    item_ = [[NSStatusBar systemStatusBar] statusItemWithLength:NSVariableStatusItemLength];
    if (!item_) return false;
    item_.autosaveName = @"com.pilandrome.phpvpn-tray.status";
    images_ = [NSMutableDictionary dictionary];
    menu_ = [[NSMenu alloc] initWithTitle:@"PHPVPN"];
    menu_.autoenablesItems = NO;
    for (const auto& entry : statusItemMenu(initial)) {
        if (entry.kind == IndicatorMenuEntry::Kind::separator) {
            [menu_ addItem:[NSMenuItem separatorItem]];
            continue;
        }
        NSMenuItem* item = [[NSMenuItem alloc] initWithTitle:mac::ns(entry.label) action:nil keyEquivalent:@""];
        if (entry.kind == IndicatorMenuEntry::Kind::status) {
            // The tooltip text when the menu is open; informative only.
            item.enabled = NO;
            status_ = item;
        } else {
            item.enabled = entry.enabled ? YES : NO;
            const auto action = entry.action;
            mac::setMenuAction(item, [this, action] {
                // The action may open a modal question: run it after the menu closed.
                deferred_.post([this, action] {
                    if (onAction_) onAction_(action);
                });
            });
            if (action == MenuAction::toggle) toggle_ = item;
        }
        [menu_ addItem:item];
    }
    item_.menu = menu_;
    NSStatusBarButton* button = item_.button;
    if (button) button.imagePosition = NSImageOnly;
    gate_.reset();
    update(initial);
    return true;
}

void StatusItem::update(const IndicatorView& view) {
    if (!item_ || !gate_.changed(view)) return;
    publish(view);
}

void StatusItem::publish(const IndicatorView& view) {
    NSString* name = mac::ns(view.iconName);
    NSImage* image = images_[name];
    if (!image) {
        image = stateImage(view.iconName);
        if (image) images_[name] = image;
    }
    NSString* text = mac::ns(view.statusText);
    NSStatusBarButton* button = item_.button;
    if (button) {
        button.image = image;
        button.toolTip = text;
        [button setAccessibilityLabel:text];
    }
    if (status_) status_.title = text;
    if (toggle_) toggle_.title = mac::ns(menuLabel(MenuAction::toggle, view.toggleDisconnects));
}

void StatusItem::destroy() {
    if (!item_) return;
    [menu_ cancelTracking];
    item_.menu = nil;
    [[NSStatusBar systemStatusBar] removeStatusItem:item_];
    item_ = nil;
    menu_ = nil;
    status_ = nil;
    toggle_ = nil;
    images_ = nil;
    gate_.reset();
}
}  // namespace phpvpn::tray
