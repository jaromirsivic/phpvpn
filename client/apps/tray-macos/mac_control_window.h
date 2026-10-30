// PHPVPN control window of the macOS tray: application icon, status text and one
// button per menu action in the UX04 order (the Linux fallback control window). It
// is shown by --show-window and by reopening the running application (Finder,
// Launchpad, open -a), and keeps every action reachable when the menu bar hides the
// status item (notch, menu bar overflow). Closing or Escape only hides it; the tray,
// its polling and the status item keep running.
#pragma once
#include <functional>

#include "mac_ui.h"
#include "status_item_model.h"

namespace phpvpn::tray {
class ControlWindow {
public:
    explicit ControlWindow(std::function<void(MenuAction)> onAction) : onAction_(std::move(onAction)) {}
    ~ControlWindow() { destroy(); }
    ControlWindow(const ControlWindow&) = delete;
    ControlWindow& operator=(const ControlWindow&) = delete;
    void show(const IndicatorView& view);
    void update(const IndicatorView& view);
    void destroy();
    NSWindow* window() const { return window_; }
    bool visible() const { return mac::windowVisible(window_); }

private:
    void build();
    void hide();
    std::function<void(MenuAction)> onAction_;
    PVWindow* window_ = nil;
    PVStackView* root_ = nil;
    NSTextField* status_ = nil;
    NSButton* toggle_ = nil;
    IndicatorView view_;
    mac::Deferred deferred_;
};
}  // namespace phpvpn::tray
