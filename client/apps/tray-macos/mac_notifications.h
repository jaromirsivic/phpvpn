// User notifications of the macOS tray (UserNotifications framework). Implements the
// portable InformationNoticePresenter, so the installation notice texts are exactly
// the Windows texts of core/presentation/installation_notice.h. The first notice asks
// for the notification permission; when it is denied, unavailable (an unbundled
// development run) or the request fails, the same text is shown in an NSAlert.
#pragma once
#include <memory>
#include <string_view>

#include "core/presentation/installation_notice.h"
#include "mac_ui.h"

namespace phpvpn::tray {
class Notifications final : public InformationNoticePresenter {
public:
    Notifications();
    ~Notifications() override;
    Notifications(const Notifications&) = delete;
    Notifications& operator=(const Notifications&) = delete;
    // True when the request was handed to the notification center or the fallback
    // alert was scheduled. Visibility, duration and Focus modes belong to macOS.
    bool showInformation(std::string_view title, std::string_view body) override;
    // Session end: no further fallback alert.
    void detach();

private:
    id delegate_ = nil;
    std::shared_ptr<bool> alive_;
};
}  // namespace phpvpn::tray
