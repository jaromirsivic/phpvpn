// NSStatusItem of the macOS tray (menu bar extra), the counterpart of the Windows
// notification area icon and the Linux AppIndicator. The icon, tooltip and menu come
// from status_item_model; this adapter only publishes them and forwards menu
// activations from a deferred main run loop call (never inside menu tracking).
#pragma once
#include <functional>

#include "mac_ui.h"
#include "status_item_model.h"

namespace phpvpn::tray {
class StatusItem {
public:
    explicit StatusItem(std::function<void(MenuAction)> onAction) : onAction_(std::move(onAction)) {}
    ~StatusItem() { destroy(); }
    StatusItem(const StatusItem&) = delete;
    StatusItem& operator=(const StatusItem&) = delete;
    bool create(const IndicatorView& initial);
    // Publishes only real changes of icon, tooltip and toggle label (UX02).
    void update(const IndicatorView& view);
    void destroy();
    bool created() const { return item_ != nil; }
    // The menu bar button, for the CI capture (macos_options.h); nil before create.
    NSView* buttonView() const { return item_ ? item_.button : nil; }

private:
    void publish(const IndicatorView& view);
    std::function<void(MenuAction)> onAction_;
    NSStatusItem* item_ = nil;
    NSMenu* menu_ = nil;
    NSMenuItem* status_ = nil;
    NSMenuItem* toggle_ = nil;
    NSMutableDictionary<NSString*, NSImage*>* images_ = nil;
    IndicatorChangeGate gate_;
    mac::Deferred deferred_;
};

// Coloured state image of the bundle (16 pt, @2x from the 32 px PNG). Not a template
// image: the state colours are the meaning on light and dark menu bars alike. An
// unbundled development run draws the same colours.
NSImage* stateImage(std::string_view name);
}  // namespace phpvpn::tray
