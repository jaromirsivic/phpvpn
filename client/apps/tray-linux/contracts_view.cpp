#include "contracts_view.h"

#include <algorithm>
#include <array>

#include "core/base/secure_buffer.h"
#include "core/base/utf8.h"
#include "core/presentation/table_pagination.h"

namespace phpvpn::tray {
std::string contractsSaveLabel(std::size_t changes) {
    if (changes == 0) return "Save";
    return "Save (" + std::to_string(changes) + (changes == 1 ? " change)" : " changes)");
}

ContractsFooter contractsFooter(const ContractsLifecycleSnapshot& snapshot, bool saved, std::size_t changes,
                                bool canPrepareSave, bool canRepeat) {
    ContractsFooter footer;
    footer.tableSensitive = !saved && !snapshot.saving && !snapshot.unknown;
    footer.saveEnabled = !saved && changes > 0 && canPrepareSave;
    footer.checkVisible = snapshot.unknown;
    footer.repeatVisible = canRepeat;
    footer.saveVisible = !snapshot.unknown;
    footer.checkEnabled = !snapshot.checking;
    return footer;
}

ContractsMessage contractsMessage(std::string_view localError, std::optional<ContractFocus> blocking,
                                  std::string_view notice) {
    ContractsMessage message;
    message.validation = localError.empty() && blocking.has_value();
    if (!localError.empty()) message.text = std::string(localError);
    else if (blocking) message.text = std::string(kContractKeyError) + "\nFirst invalid peer: " + blocking->remoteIp.toString();
    else message.text = std::string(notice);
    return message;
}

std::vector<ContractsPagerItem> contractsPagerItems(int count, int current) {
    std::vector<ContractsPagerItem> items;
    if (count <= 1) return items;
    auto add = [&](std::string label, int page, std::string name, bool active = false) {
        ContractsPagerItem item{std::move(label), page, std::move(name), active, page >= 0 && page != current};
        // The current number stays focusable and uses the active color.
        if (active) item.sensitive = true;
        items.push_back(std::move(item));
    };
    add("«", 0, "First page");
    add("‹", std::max(0, current - 1), "Previous page");
    int previous = -1;
    for (int page : tablePageNumbers(count, current)) {
        if (page > previous + 1) add("…", -1, "");
        add(std::to_string(page + 1), page,
            "Page " + std::to_string(page + 1) + " of " + std::to_string(count) + (page == current ? ", current page" : ""),
            page == current);
        previous = page;
    }
    add("›", std::min(count - 1, current + 1), "Next page");
    add("»", count - 1, "Last page");
    return items;
}

bool contractsEditAccept(std::u16string_view current, std::size_t first, std::size_t last,
                         std::u16string_view insertion, bool replaceAll, std::size_t bound,
                         const ContractsEditCheck& check) {
    if (!check) return false;
    // A NUL must invalidate Password SHA even when the previous input is already at
    // its scalar limit; it never turns into a length-only rejection.
    if (insertion.find(u'\0') != std::u16string_view::npos) return check(std::u16string_view(u"\0", 1));
    std::u16string proposed;
    struct Wipe {
        std::u16string& text;
        ~Wipe() { secureWipe(text.data(), text.size() * sizeof(char16_t)); }
    } wipe{proposed};
    if (!replaceAll) {
        const auto n = current.size();
        first = std::min(first, n);
        last = std::min(std::max(first, last), n);
        if (insertion.size() > bound || n - (last - first) > bound - insertion.size())
            return check(std::u16string(bound + 1, u'x'));
        proposed.assign(current);
        proposed.replace(first, last - first, insertion);
    } else {
        if (insertion.size() > bound) return check(std::u16string(bound + 1, u'x'));
        proposed.assign(insertion);
    }
    return check(proposed);
}

std::size_t contractsUtf16Offset(std::string_view utf8, std::size_t characters) {
    std::size_t units = 0, index = 0;
    while (index < utf8.size() && characters > 0) {
        const auto lead = static_cast<unsigned char>(utf8[index]);
        const std::size_t length = lead < 0x80 ? 1 : lead < 0xE0 ? 2 : lead < 0xF0 ? 3 : 4;
        units += length == 4 ? 2 : 1;
        index += length;
        --characters;
    }
    return units;
}

std::u16string contractsUtf16(std::string_view utf8) {
    auto converted = utf8ToUtf16(utf8);
    return converted ? std::move(converted).value() : std::u16string{};
}

bool contractsKeyTextSupported(std::u16string_view text, std::string& utf8) {
    if (text.find(u'\0') != std::u16string_view::npos || text.find_first_of(u"\r\n") != std::u16string_view::npos)
        return false;
    auto converted = utf16ToUtf8(text);
    if (!converted) return false;
    utf8 = std::move(converted).value();
    return true;
}

std::string_view passwordShaNotice(PasswordKeyError error, bool targetEnabled) {
    if (!targetEnabled) return "Enable this contract to apply the key. Your password is kept.";
    switch (error) {
        case PasswordKeyError::tooLong: return "Password must contain at most 255 Unicode characters.";
        case PasswordKeyError::invalidUtf16: return "Password contains an incomplete Unicode character.";
        case PasswordKeyError::unsupportedControl: return "Password contains an unsupported control character.";
        case PasswordKeyError::cryptoFailed: return "The SHA-256 key could not be generated.";
        case PasswordKeyError::none: break;
    }
    return {};
}

bool contractsRandomPassword(CryptoProvider& crypto, std::u16string& password) {
    constexpr std::u16string_view alphabet = u"ghjkmnpqrstuvwxyz";
    constexpr auto limit = 256 - (256 % alphabet.size());
    SecureBuffer bytes(64);
    std::array<char16_t, 64> result{};
    struct Wiper {
        std::array<char16_t, 64>& value;
        ~Wiper() { secureWipe(value.data(), value.size() * sizeof(char16_t)); }
    } wipe{result};
    std::size_t used = 0;
    while (used < result.size()) {
        if (!crypto.randomBytes(bytes.mutableSpan())) return false;
        for (auto byte : bytes.span()) {
            // Reject 255 for this 17-character alphabet, so every character has
            // exactly 15 possible byte values and no modulo bias.
            if (byte >= limit) continue;
            result[used++] = alphabet[byte % alphabet.size()];
            if (used == result.size()) break;
        }
    }
    secureWipe(password.data(), password.size() * sizeof(char16_t));
    password.assign(result.data(), result.size());
    return true;
}
}  // namespace phpvpn::tray
