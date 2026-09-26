// Password SHA window of the GTK tray (tray_UX.md UX11), the counterpart of the
// Win32 PasswordShaWindow (apps/tray/password_sha_window.cpp). Non-modal child of
// Communication contracts with the immutable ContractTarget captured at opening;
// several children may exist, also for the same remote IP. The portable
// PasswordKeyModel computes SHA-256 over the exact UTF-8 of the password; the
// password never leaves this window, only the resulting key is applied.
#pragma once
#include <gtk/gtk.h>

#include <functional>
#include <string>
#include <string_view>

#include "contracts_view.h"
#include "core/presentation/contracts_model.h"
#include "core/presentation/password_key_model.h"

namespace phpvpn::tray {
// GtkEntry adapter of the Win32 ContractsEdit subclass: every insertion (typing,
// drop, input method, accessibility insert) and every paste over the selection is
// preflighted with contractsEditAccept before the field changes, so an over-budget
// or unsupported insertion is rejected as a whole. A non-editable field refuses all
// insertions. While *bypass is true (programmatic set) nothing is checked.
void attachContractsEdit(GtkWidget* entry, std::size_t bound, ContractsEditCheck check, const bool* bypass);

class PasswordShaWindow {
public:
    using Apply = std::function<bool(const ContractTarget&, std::string_view key)>;
    PasswordShaWindow(CryptoProvider& crypto, ContractTarget target, Apply apply,
                      std::function<void(const ContractTarget&)> closed = {})
        : crypto_(crypto), target_(std::move(target)), apply_(std::move(apply)), closed_(std::move(closed)) {}
    ~PasswordShaWindow();
    PasswordShaWindow(const PasswordShaWindow&) = delete;
    PasswordShaWindow& operator=(const PasswordShaWindow&) = delete;
    bool open(GtkWindow* parent);
    GtkWindow* window() const { return window_ ? GTK_WINDOW(window_) : nullptr; }
    // Closes and reports the normal close to the parent (focus returns there).
    void close();
    // Terminal close without the focus callback (save, session end).
    void dismiss();
    Ipv4Address remoteIp() const { return target_.remoteIp(); }
    void setTargetEnabled(bool enabled);

private:
    bool update(std::u16string_view text);
    void refreshStatus();
    void randomPassword();
    void usePairDefault();
    void confirm();
    void setInput(std::u16string_view text);
    void onDestroyed();

    CryptoProvider& crypto_;
    const ContractTarget target_;
    Apply apply_;
    std::function<void(const ContractTarget&)> closed_;
    PasswordKeyModel model_;
    GtkWidget* window_ = nullptr;
    GtkWidget* input_ = nullptr;
    GtkWidget* hash_ = nullptr;
    GtkWidget* notice_ = nullptr;
    GtkWidget* ok_ = nullptr;
    bool targetEnabled_ = true;
    bool bypass_ = false;
};
}  // namespace phpvpn::tray
