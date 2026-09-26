// org.freedesktop.Notifications over GDBus. Implements the portable
// InformationNoticePresenter, so the installation notice texts are exactly the
// Windows texts of core/presentation/installation_notice.h.
#pragma once
#include <gio/gio.h>

#include <string_view>

#include "core/presentation/installation_notice.h"

namespace phpvpn::tray {
class DesktopNotifications final : public InformationNoticePresenter {
public:
    DesktopNotifications() = default;
    ~DesktopNotifications() override;
    DesktopNotifications(const DesktopNotifications&) = delete;
    DesktopNotifications& operator=(const DesktopNotifications&) = delete;
    // Uses the session bus of the application. Without it every notice fails.
    void attach(GDBusConnection* sessionBus);
    void detach();
    // True when the Notify call was handed to the session bus. The daemon owns
    // visibility, duration and do-not-disturb, as the Windows shell does; the
    // reply is only checked for logging.
    bool showInformation(std::string_view title, std::string_view body) override;

private:
    GDBusConnection* bus_ = nullptr;
    GCancellable* cancellable_ = nullptr;
};
}  // namespace phpvpn::tray
