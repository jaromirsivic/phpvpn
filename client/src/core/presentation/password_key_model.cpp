#include "core/presentation/password_key_model.h"

#include <array>
#include "core/base/hex.h"

namespace phpvpn {
namespace {
struct PasswordWork {
    std::array<std::uint8_t, PasswordKeyModel::kMaxScalars * 4> bytes{};
    ~PasswordWork() { secureWipe(bytes.data(), bytes.size()); }
};
struct DigestWiper {
    Sha256Digest& digest;
    ~DigestWiper() { secureWipe(digest.data(), digest.size()); }
};
struct KeyTextWiper {
    std::string& text;
    ~KeyTextWiper() { secureWipe(text.data(), text.size()); }
};
} // namespace
PasswordKeyError PasswordKeyModel::update(CryptoProvider& crypto, std::u16string_view password) {
    const auto reject = [&](PasswordKeyError error, bool invalidate) {
        error_ = error;
        if (invalidate) { key_.clear(); scalarCount_ = 0; }
        return error;
    };
    // A valid value cannot exceed 510 units. Check before scanning/copying.
    if (password.size() > kMaxScalars * 2) return reject(PasswordKeyError::tooLong, false);
    PasswordWork work;
    std::size_t scalars = 0, bytes = 0;
    for (std::size_t i = 0; i < password.size(); ++i) {
        std::uint32_t code = password[i];
        if (code >= 0xd800 && code <= 0xdbff) {
            if (i + 1 == password.size() || password[i + 1] < 0xdc00 || password[i + 1] > 0xdfff)
                return reject(PasswordKeyError::invalidUtf16, true);
            code = 0x10000 + ((code - 0xd800) << 10) + (password[++i] - 0xdc00);
        } else if (code >= 0xdc00 && code <= 0xdfff)
            return reject(PasswordKeyError::invalidUtf16, true);
        // Single-line input policy: C0, DEL/C1 and Unicode line/paragraph
        // separators are unsupported. No trimming or replacement occurs.
        if (code < 0x20 || (code >= 0x7f && code <= 0x9f) || code == 0x2028 || code == 0x2029)
            return reject(PasswordKeyError::unsupportedControl, true);
        if (++scalars > kMaxScalars) return reject(PasswordKeyError::tooLong, false);
        const auto append = [&](std::uint32_t byte) { work.bytes[bytes++] = static_cast<std::uint8_t>(byte); };
        if (code < 0x80) append(code);
        else if (code < 0x800) {
            append(0xc0 | (code >> 6)); append(0x80 | (code & 0x3f));
        } else if (code < 0x10000) {
            append(0xe0 | (code >> 12)); append(0x80 | ((code >> 6) & 0x3f)); append(0x80 | (code & 0x3f));
        } else {
            append(0xf0 | (code >> 18)); append(0x80 | ((code >> 12) & 0x3f));
            append(0x80 | ((code >> 6) & 0x3f)); append(0x80 | (code & 0x3f));
        }
    }
    auto hash = crypto.sha256(std::span<const std::uint8_t>(work.bytes.data(), bytes));
    if (!hash) return reject(PasswordKeyError::cryptoFailed, true);
    DigestWiper wipe{*hash};
    auto hex = toLowercaseHex(*hash);
    KeyTextWiper wipeHex{hex};
    key_.assign(asBytes(hex));
    scalarCount_ = scalars;
    error_ = PasswordKeyError::none;
    return error_;
}
std::string_view PasswordKeyModel::key() const noexcept {
    return key_.empty() ? std::string_view{} : std::string_view(reinterpret_cast<const char*>(key_.data()), key_.size());
}
void PasswordKeyModel::clear() noexcept {
    key_.clear();
    scalarCount_ = 0;
    error_ = PasswordKeyError::none;
}
} // namespace phpvpn
