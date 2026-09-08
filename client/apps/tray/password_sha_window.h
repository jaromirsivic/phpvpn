#pragma once
#include "contracts_edit.h"
#include "core/presentation/contracts_model.h"
#include "core/presentation/password_key_model.h"
#include "ui_resources.h"
#include <functional>

namespace phpvpn::tray {
class PasswordShaWindow {
public:
    using Apply = std::function<bool(const ContractTarget&, std::string_view)>;
    PasswordShaWindow(CryptoProvider& crypto, ContractTarget target, Apply apply, std::function<void(const ContractTarget&)> closed = {})
        : crypto_(crypto), target_(std::move(target)), apply_(std::move(apply)), closed_(std::move(closed)) {}
    ~PasswordShaWindow() { close(); }
    bool open(HINSTANCE instance, HWND parent);
    bool translate(MSG& message) const;
    HWND window() const { return window_; }
    void close();
private:
    static LRESULT CALLBACK procedure(HWND, UINT, WPARAM, LPARAM);
    static LRESULT CALLBACK bodyProcedure(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);
    bool update(std::wstring_view text);
    void layout();
    void resources();
    CryptoProvider& crypto_;
    const ContractTarget target_;
    Apply apply_;
    std::function<void(const ContractTarget&)> closed_;
    PasswordKeyModel model_;
    ContractsEdit inputAdapter_;
    UiResources resources_;
    HINSTANCE instance_ = nullptr;
    HWND window_ = nullptr, parent_ = nullptr, body_ = nullptr, input_ = nullptr, hash_ = nullptr, ok_ = nullptr;
    int scroll_ = 0;
    std::array<HWND, 6> labels_{};
};
} // namespace phpvpn::tray
