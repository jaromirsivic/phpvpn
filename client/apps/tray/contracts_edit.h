#pragma once
#include <windows.h>
#include <commctrl.h>
#include <functional>
#include <string>
#include "core/base/secure_buffer.h"

namespace phpvpn::tray {
// Contracts-only preflight for native single-line EDIT mutations. Rejects a
// whole over-budget paste/replacement before native truncation or undo changes.
class ContractsEdit {
public:
    using Check = std::function<bool(std::wstring_view)>;
    bool attach(HWND field, std::size_t bound, Check check) {
        field_ = field; bound_ = bound; check_ = std::move(check);
        SendMessageW(field_, EM_SETLIMITTEXT, static_cast<WPARAM>(bound + 1), 0);
        return SetWindowSubclass(field_, procedure, 1, reinterpret_cast<DWORD_PTR>(this)) != FALSE;
    }
    void clear() {
        if (field_) { bypass_ = true; SetWindowTextW(field_, L""); SendMessageW(field_, EM_EMPTYUNDOBUFFER, 0, 0); bypass_ = false; }
    }
    void set(std::wstring_view text) {
        bypass_ = true; std::wstring copy(text); SetWindowTextW(field_, copy.c_str());
        secureWipe(copy.data(), copy.size() * sizeof(wchar_t)); bypass_ = false;
    }
    bool bypassing() const { return bypass_; }
private:
    struct Text {
        std::wstring value;
        ~Text() { secureWipe(value.data(), value.size() * sizeof(wchar_t)); }
    };
    bool accept(std::wstring_view insertion, bool all) {
        // A NUL must invalidate Password SHA even when the previous input is
        // already at its scalar limit. It must never turn into a length-only
        // rejection retaining a seemingly applicable old hash.
        if (insertion.find(L'\0') != std::wstring_view::npos) return check_(std::wstring_view(L"\0", 1));
        Text proposed;
        if (!all) {
            const auto n = GetWindowTextLengthW(field_);
            proposed.value.resize(static_cast<std::size_t>(n) + 1);
            GetWindowTextW(field_, proposed.value.data(), n + 1); proposed.value.resize(n);
            DWORD first = 0, last = 0;
            SendMessageW(field_, EM_GETSEL, reinterpret_cast<WPARAM>(&first), reinterpret_cast<LPARAM>(&last));
            first = (std::min)(first, static_cast<DWORD>(n)); last = (std::min)(last, static_cast<DWORD>(n));
            if (insertion.size() > bound_ || proposed.value.size() - (last - first) > bound_ - insertion.size())
                return check_(std::wstring(bound_ + 1, L'x'));
            proposed.value.replace(first, last - first, insertion);
        } else {
            if (insertion.size() > bound_) return check_(std::wstring(bound_ + 1, L'x'));
            proposed.value.assign(insertion);
        }
        return check_(proposed.value);
    }
    static LRESULT CALLBACK procedure(HWND h, UINT m, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR data) {
        auto& self = *reinterpret_cast<ContractsEdit*>(data);
        if (m == WM_NCDESTROY) { self.field_ = nullptr; RemoveWindowSubclass(h, procedure, 1); }
        if (self.bypass_) return DefSubclassProc(h, m, w, l);
        // Native read-only still permits some programmatic edit messages.
        // Block them before preflight can mutate the working model. Copy and
        // selection remain native, while set()/clear() use the explicit bypass.
        if ((GetWindowLongPtrW(h, GWL_STYLE) & ES_READONLY) &&
            (m == WM_SETTEXT || m == EM_REPLACESEL || m == WM_PASTE || m == WM_CUT ||
             m == WM_CLEAR || m == WM_UNDO || m == EM_UNDO ||
             (m == WM_CHAR && (w >= 32 || w == 0 || w == VK_BACK)))) return 0;
        if (m == WM_SETTEXT || m == EM_REPLACESEL) {
            const auto* text = reinterpret_cast<const wchar_t*>(l);
            const auto n = text ? wcsnlen(text, self.bound_ + 1) : 0;
            if (!self.accept({text ? text : L"", n}, m == WM_SETTEXT)) return FALSE;
        } else if (m == WM_CHAR && (w >= 32 || w == 0)) {
            const wchar_t character = static_cast<wchar_t>(w);
            if (!self.accept({&character, 1}, false)) return 0;
        } else if (m == WM_PASTE) {
            if (!OpenClipboard(h)) return 0;
            const auto handle = GetClipboardData(CF_UNICODETEXT);
            const auto bytes = handle ? GlobalSize(handle) : 0;
            const auto* text = handle ? static_cast<const wchar_t*>(GlobalLock(handle)) : nullptr;
            bool accepted = false;
            if (text && bytes >= sizeof(wchar_t)) {
                const auto available = bytes / sizeof(wchar_t);
                const auto n = wcsnlen(text, (std::min)(available, self.bound_ + 1));
                accepted = n < available && self.accept({text, n}, false);
            }
            if (text) GlobalUnlock(handle);
            CloseClipboard();
            if (!accepted) return 0;
        }
        return DefSubclassProc(h, m, w, l);
    }
    HWND field_ = nullptr;
    std::size_t bound_ = 0;
    Check check_;
    bool bypass_ = false;
};
} // namespace phpvpn::tray
