// StatusNotifierItem indicator through libayatana-appindicator3 (plan 4.12).
// The icon, status text and menu come from indicator_model; this adapter only
// publishes them and forwards menu activations.
#pragma once
#include <gtk/gtk.h>

#include <array>
#include <functional>
#include <string>
#include <vector>

#include "indicator_model.h"

typedef struct _AppIndicator AppIndicator;

namespace phpvpn::tray {
class Indicator {
public:
    explicit Indicator(std::function<void(MenuAction)> onAction) : onAction_(std::move(onAction)) {}
    ~Indicator() { destroy(); }
    Indicator(const Indicator&) = delete;
    Indicator& operator=(const Indicator&) = delete;
    // iconThemePath: extra lookup directory for a non-installed run, may be empty.
    bool create(const std::string& iconThemePath, const IndicatorView& initial);
    // Publishes only real changes of icon, status text and toggle label.
    void update(const IndicatorView& view);
    void destroy();
    bool created() const { return indicator_ != nullptr; }

private:
    struct ItemBinding { Indicator* owner; MenuAction action; };
    void publish(const IndicatorView& view, bool force);
    std::function<void(MenuAction)> onAction_;
    AppIndicator* indicator_ = nullptr;
    GtkWidget* menu_ = nullptr;
    GtkWidget* statusItem_ = nullptr;
    GtkWidget* toggleItem_ = nullptr;
    std::vector<ItemBinding*> bindings_;
    IndicatorChangeGate gate_;
    IndicatorView published_;
};
}  // namespace phpvpn::tray
