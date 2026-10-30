// Password SHA window of the macOS tray (tray_UX.md UX11), the counterpart of the
// Win32 PasswordShaWindow and apps/tray-linux/password_sha_window. Non-modal child
// of Communication contracts with the immutable ContractTarget captured at opening;
// several children may exist, also for the same remote IP. The portable
// PasswordKeyModel computes SHA-256 over the exact UTF-8 of the password (the input
// is passed as the exact UTF-16 of the field, unpaired surrogates included); the
// password never leaves this window, only the resulting key is applied.
#pragma once
#include <functional>
#include <string>
#include <string_view>

#include "contracts_view.h"
#include "core/presentation/contracts_model.h"
#include "core/presentation/password_key_model.h"
#include "mac_ui.h"

namespace phpvpn::tray {
class PasswordShaWindow {
public:
    using Apply = std::function<bool(const ContractTarget&, std::string_view key)>;
    PasswordShaWindow(CryptoProvider& crypto, ContractTarget target, Apply apply,
                      std::function<void(const ContractTarget&)> closed = {})
        : crypto_(crypto), target_(std::move(target)), apply_(std::move(apply)), closed_(std::move(closed)) {}
    ~PasswordShaWindow();
    PasswordShaWindow(const PasswordShaWindow&) = delete;
    PasswordShaWindow& operator=(const PasswordShaWindow&) = delete;
    bool open(NSWindow* parent);
    NSWindow* window() const { return window_; }
    // Closes and reports the normal close to the parent (focus returns there).
    void close();
    // Terminal close without the focus callback (save, session end).
    void dismiss();
    Ipv4Address remoteIp() const { return target_.remoteIp(); }
    void setTargetEnabled(bool enabled);

private:
    bool update(std::u16string_view text);
    bool preflight(NSString* proposed);
    void refreshStatus();
    void randomPassword();
    void usePairDefault();
    void confirm();
    void setInput(std::u16string_view text);
    void destroy();

    CryptoProvider& crypto_;
    const ContractTarget target_;
    Apply apply_;
    std::function<void(const ContractTarget&)> closed_;
    PasswordKeyModel model_;
    PVWindow* window_ = nil;
    PVStackView* root_ = nil;
    NSTextField* input_ = nil;
    NSTextField* hash_ = nil;
    NSTextField* notice_ = nil;
    NSButton* ok_ = nil;
    bool targetEnabled_ = true;
    mac::Deferred deferred_;
};
}  // namespace phpvpn::tray
