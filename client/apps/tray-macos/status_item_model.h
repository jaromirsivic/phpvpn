// GUI-free presentation model of the macOS status item (NSStatusItem): status to
// icon, tooltip text and menu, plus change detection. The rules are the Windows
// TrayApplication::updateIcon/showMenu rules (tray_UX.md "Aplikacni a stavove
// ikony", UX04), as in apps/tray-linux/indicator_model, without the Linux
// StatusNotifierWatcher fallback: the macOS menu bar always hosts the item.
// The menu keeps the documented status line (the tooltip text as the first
// disabled item) followed by a separator; tray_UX.md "macOS (AppKit)".
#pragma once
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "menu_model.h"
#include "status_model.h"

namespace phpvpn::tray {
// Bundle image resources (Resources/<name>.png and <name>@2x.png).
inline constexpr std::string_view kApplicationIconName = "phpvpn";
std::string_view statusIconName(TrayIconState state);

// Connect/Disconnect follows the in-memory VPN state, as the Windows menu does.
bool connectedOrConnecting(const TrayStatus& status);

struct IndicatorView {
    TrayIconState icon = TrayIconState::disconnected;
    std::string iconName;
    // The Windows tooltip text (ui_style.h): status item tooltip and status line.
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
    bool enabled = false;
    bool operator==(const IndicatorMenuEntry&) const = default;
};
// The UX04 order of configurationMenu (every action enabled: menus never cache the
// configuration availability).
std::vector<IndicatorMenuEntry> controlWindowActions(const IndicatorView& view);
// Status line, separator, then exactly controlWindowActions.
std::vector<IndicatorMenuEntry> statusItemMenu(const IndicatorView& view);

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
}  // namespace phpvpn::tray
