#pragma once

#include "core/crypto/crypto_provider.h"

namespace phpvpn {
enum class PasswordKeyError { none, tooLong, invalidUtf16, unsupportedControl, cryptoFailed };

// Borrows UTF-16 only during update, never retains the password. The view owns
// its input/selection/undo and restores them on rejected edits. No cue text is
// supplied: initialize with update(crypto, u""). Calls must be serialized.
class PasswordKeyModel {
public:
    static constexpr std::size_t kMaxScalars = 255;
    PasswordKeyModel() = default;
    PasswordKeyModel(const PasswordKeyModel&) = delete;
    PasswordKeyModel& operator=(const PasswordKeyModel&) = delete;
    // tooLong retains the prior output/count, so the adapter must restore the
    // previous input and selection. Other failures clear the output. valid(),
    // not error()==none, determines whether the retained input has a usable key.
    PasswordKeyError update(CryptoProvider& crypto, std::u16string_view password);
    bool valid() const noexcept { return !key_.empty(); }
    std::string_view key() const noexcept;
    std::size_t scalarCount() const noexcept { return scalarCount_; }
    PasswordKeyError error() const noexcept { return error_; }
    void clear() noexcept;
private:
    SecureBuffer key_;
    std::size_t scalarCount_ = 0;
    PasswordKeyError error_ = PasswordKeyError::none;
};
} // namespace phpvpn
