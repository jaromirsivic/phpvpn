// Fallback control window (plan 4.12): PHPVPN icon, status text and one button per
// menu action in the UX04 order. It replaces the indicator when the desktop has no
// StatusNotifierWatcher and is also shown by the application launcher
// (phpvpn-tray --show-window). Closing it only hides it; the tray keeps running.
// Button actions run from an idle callback (DeferredCalls), like the indicator menu:
// Exit and other actions may open a modal question, which must not run inside the
// button's "clicked" signal or its AT-SPI click action.
#pragma once
#include <gtk/gtk.h>

#include <functional>
#include <vector>

#include "gtk_util.h"
#include "indicator_model.h"

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
    GtkWindow* window() const { return window_ ? GTK_WINDOW(window_) : nullptr; }
    bool visible() const { return window_ && gtk_widget_get_visible(window_); }

private:
    struct Binding { ControlWindow* owner; MenuAction action; };
    void build();
    std::function<void(MenuAction)> onAction_;
    GtkWidget* window_ = nullptr;
    GtkWidget* status_ = nullptr;
    GtkWidget* toggle_ = nullptr;
    std::vector<Binding*> bindings_;
    IndicatorView view_;
    DeferredCalls deferred_;
};
}  // namespace phpvpn::tray
