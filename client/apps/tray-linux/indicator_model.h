// GTK-free presentation model of the Linux tray indicator: status to icon, status
// text and menu, change detection and the StatusNotifierWatcher fallback policy.
// The rules are the Windows TrayApplication::updateIcon/showMenu rules (tray_UX.md
// "Aplikacni a stavove ikony", UX04) plus the documented Linux deviation: a first
// insensitive status item carrying the tooltip text, followed by a separator.
#pragma once
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "menu_model.h"
#include "status_model.h"

namespace phpvpn::tray {
// Icon theme names installed to share/icons/hicolor by the tray package.
inline constexpr std::string_view kApplicationIconName = "phpvpn";
std::string_view indicatorIconName(TrayIconState state);

// Connect/Disconnect follows the in-memory VPN state, as the Windows menu does.
bool connectedOrConnecting(const TrayStatus& status);

struct IndicatorView {
    TrayIconState icon = TrayIconState::disconnected;
    std::string iconName;
    // The Windows tooltip text (ui_style.h), shown as status item and indicator title.
    std::string statusText;
    // True when the toggle item reads Disconnect.
    bool toggleDisconnects = false;
    bool operator==(const IndicatorView&) const = default;
};
// unavailable: the last read failed. disconnectInProgress: an accepted local
// Disconnect is still being observed. localConnect: a local Connect awaits its reply.
IndicatorView indicatorView(const TrayStatus& snapshot, bool unavailable, bool disconnectInProgress,
                            bool localConnect);

std::string menuLabel(MenuAction action, bool toggleDisconnects);

struct IndicatorMenuEntry {
    enum class Kind { status, separator, action };
    Kind kind = Kind::separator;
    MenuAction action = MenuAction::separator;
    std::string label;
    bool sensitive = false;
    bool operator==(const IndicatorMenuEntry&) const = default;
};
// Status item, separator, then exactly the UX04 order of configurationMenu.
std::vector<IndicatorMenuEntry> indicatorMenu(const IndicatorView& view);
// The fallback control window offers the same actions in the same order.
std::vector<IndicatorMenuEntry> controlWindowActions(const IndicatorView& view);

// Publishes only real changes (tray_UX.md UX02: no update for an identical snapshot).
class IndicatorChangeGate {
public:
    bool changed(const IndicatorView& view) {
        if (last_ && *last_ == view) return false;
        last_ = view;
        return true;
    }
    void reset() { last_.reset(); }
private:
    std::optional<IndicatorView> last_;
};

// Actions of the StatusNotifierWatcher policy, injected so the policy is testable
// without a session bus.
class WatcherPresenceSink {
public:
    virtual ~WatcherPresenceSink() = default;
    virtual void openFallbackWindow() = 0;
    virtual void showWatcherHint() = 0;
};
// org.kde.StatusNotifierWatcher presence. Without a watcher the indicator is
// invisible, so each transition to "absent" opens the fallback control window once;
// the hint notification is shown at most once per tray process. When the watcher
// appears later, the indicator registers itself and nothing else changes.
class WatcherPresence {
public:
    explicit WatcherPresence(WatcherPresenceSink& sink) : sink_(sink) {}
    void appeared() { state_ = State::present; }
    void vanished() {
        if (state_ == State::absent) return;
        state_ = State::absent;
        sink_.openFallbackWindow();
        if (!hintShown_) { hintShown_ = true; sink_.showWatcherHint(); }
    }
    bool present() const { return state_ == State::present; }
    bool known() const { return state_ != State::unknown; }
private:
    enum class State { unknown, present, absent };
    WatcherPresenceSink& sink_;
    State state_ = State::unknown;
    bool hintShown_ = false;
};
}  // namespace phpvpn::tray
