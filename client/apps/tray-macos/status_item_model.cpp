#include "status_item_model.h"

#include "macos_text.h"
#include "ui_style.h"

namespace phpvpn::tray {
std::string_view statusIconName(TrayIconState state) {
    switch (state) {
        case TrayIconState::connecting: return "phpvpn-connecting";
        case TrayIconState::connected: return "phpvpn-connected";
        case TrayIconState::disconnected: break;
    }
    return "phpvpn-disconnected";
}

bool connectedOrConnecting(const TrayStatus& status) {
    return status.state == "connected" || status.state == "connecting" || status.state == "recovering";
}

IndicatorView indicatorView(const TrayStatus& snapshot, bool unavailable, bool disconnectInProgress,
                            bool localConnect) {
    IndicatorView view;
    view.icon = selectTrayIcon(snapshot, unavailable, disconnectInProgress || localConnect);
    view.iconName = std::string(statusIconName(view.icon));
    // Same precedence as the Windows tooltip selection in updateIcon.
    const wchar_t* text = ui::disconnectedTip;
    if (unavailable) text = ui::unavailableTip;
    else if (disconnectInProgress) text = ui::connectingTip;
    else if (view.icon == TrayIconState::connected) text = ui::connectedTip;
    else if (snapshot.state == "recovering") text = ui::recoveringTip;
    else if (view.icon == TrayIconState::connecting) text = ui::connectingTip;
    else if (snapshot.state == "error") text = ui::errorTip;
    view.statusText = utf8(text);
    view.toggleDisconnects = connectedOrConnecting(snapshot);
    return view;
}

std::string menuLabel(MenuAction action, bool toggleDisconnects) {
    if (action == MenuAction::toggle && toggleDisconnects) return "Disconnect";
    // UX04 only changes the letter case of "contracts" against the shared model.
    if (action == MenuAction::contracts) return "Communication contracts";
    for (const auto& item : configurationMenu)
        if (item.action == action) return utf8(item.title);
    return {};
}

std::vector<IndicatorMenuEntry> controlWindowActions(const IndicatorView& view) {
    std::vector<IndicatorMenuEntry> entries;
    for (const auto& item : configurationMenu) {
        IndicatorMenuEntry entry;
        if (item.action == MenuAction::separator) {
            entry.kind = IndicatorMenuEntry::Kind::separator;
        } else {
            entry.kind = IndicatorMenuEntry::Kind::action;
            entry.action = item.action;
            entry.label = menuLabel(item.action, view.toggleDisconnects);
            entry.enabled = true;
        }
        entries.push_back(std::move(entry));
    }
    return entries;
}

std::vector<IndicatorMenuEntry> statusItemMenu(const IndicatorView& view) {
    std::vector<IndicatorMenuEntry> entries;
    IndicatorMenuEntry status;
    status.kind = IndicatorMenuEntry::Kind::status;
    status.label = view.statusText;
    entries.push_back(std::move(status));
    entries.push_back(IndicatorMenuEntry{});
    auto actions = controlWindowActions(view);
    entries.insert(entries.end(), actions.begin(), actions.end());
    return entries;
}
}  // namespace phpvpn::tray
