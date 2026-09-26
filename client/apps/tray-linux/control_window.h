// Fallback control window (plan 4.12): status text, state shield and one button per
// menu action in the UX04 order. It replaces the indicator when the desktop has no
// StatusNotifierWatcher and is also shown by the application launcher
// (phpvpn-tray --show-window). Closing it only hides it; the tray keeps running.
#pragma once
#include <gtk/gtk.h>

#include <functional>
#include <vector>

#include "indicator_model.h"
#include "progress_model.h"

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
    GtkWidget* shieldArea_ = nullptr;
    GtkWidget* toggle_ = nullptr;
    std::vector<Binding*> bindings_;
    Shield shield_ = Shield::grey;
    IndicatorView view_;
};
}  // namespace phpvpn::tray
