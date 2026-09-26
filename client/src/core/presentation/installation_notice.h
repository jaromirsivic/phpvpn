#pragma once
#include <string_view>

namespace phpvpn {
// UTF-8 and platform-independent: future Linux/macOS trays can implement this
// with their native notification service without depending on HWND or Shell.
// Success means the platform accepted the request, not that the user saw it.
class InformationNoticePresenter {
public:
    virtual ~InformationNoticePresenter() = default;
    virtual bool showInformation(std::string_view title, std::string_view body) = 0;
};

inline constexpr std::string_view kInstallationNoticeTitle = "PHPVPN installed";
inline constexpr std::string_view kInstallationNoticeText =
    "PHPVPN client is installed. To get started, open Configuration Wizard or Edit Config "
    "to set up your connection, then choose Connect when you're ready.";

inline bool showInstallationNotice(InformationNoticePresenter& presenter) {
    return presenter.showInformation(kInstallationNoticeTitle, kInstallationNoticeText);
}
} // namespace phpvpn
