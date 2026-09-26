#pragma once
#include <windows.h>
#include <shellapi.h>
#include <optional>
#include <string>
#include "layout.h"
#include "core/presentation/installation_notice.h"

namespace phpvpn::tray {
class ShellAdapter {
public:
    virtual ~ShellAdapter() = default;
    virtual bool notify(DWORD operation, NOTIFYICONDATAW& data) = 0;
    virtual std::optional<Point> menuAnchor(HWND) { return {}; }
};
class Win32ShellAdapter final : public ShellAdapter {
public:
    bool notify(DWORD operation, NOTIFYICONDATAW& data) override { return Shell_NotifyIconW(operation, &data) != FALSE; }
    std::optional<Point> menuAnchor(HWND window) override {
        NOTIFYICONIDENTIFIER identity{sizeof(identity)};
        identity.hWnd = window; identity.uID = 1;
        RECT rect{};
        if (FAILED(Shell_NotifyIconGetRect(&identity, &rect))) return {};
        return Point{rect.left + (rect.right - rect.left) / 2, rect.top + (rect.bottom - rect.top) / 2};
    }
};
// Only successful publications become the comparison baseline. Failed changes
// remain retryable. Icon handles are borrowed from the window's resource cache.
class ShellIcon : public InformationNoticePresenter {
public:
    explicit ShellIcon(ShellAdapter* shell) : shell_(shell) {}
    explicit ShellIcon(ShellAdapter& shell) : ShellIcon(&shell) {}
    ~ShellIcon() { remove(); }
    ShellIcon(const ShellIcon&) = delete;
    ShellIcon& operator=(const ShellIcon&) = delete;
    bool publish(HWND window, UINT callback, HICON icon, const std::wstring& tooltip) {
        if (!shell_ || tooltip.size() >= 128 || !icon) return false;
        if (published_ && data_.hWnd == window && data_.hIcon == icon && tooltip_ == tooltip) return true;
        auto next = data_;
        next.cbSize = sizeof(next); next.hWnd = window; next.uID = 1;
        next.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP; next.uCallbackMessage = callback; next.hIcon = icon;
        wcscpy_s(next.szTip, tooltip.c_str());
        if (!shell_->notify(published_ ? NIM_MODIFY : NIM_ADD, next)) return false;
        data_ = next; tooltip_ = tooltip; published_ = true;
        return true;
    }
    void remove() {
        if (published_) { shell_->notify(NIM_DELETE, data_); published_ = false; }
    }
    bool showInformation(std::string_view title, std::string_view body) override {
        if (!published_) return false;
        auto notice = data_;
        if (!copyUtf8(title, notice.szInfoTitle) || !copyUtf8(body, notice.szInfo)) return false;
        notice.uFlags = NIF_INFO;
        notice.dwInfoFlags = NIIF_INFO | NIIF_RESPECT_QUIET_TIME;
        // Windows owns visibility, dismissal and duration. Do not cache NIF_INFO
        // in data_: ordinary status/icon updates must never replay this notice.
        return shell_->notify(NIM_MODIFY, notice);
    }
private:
    template<std::size_t N>
    static bool copyUtf8(std::string_view text, wchar_t (&buffer)[N]) {
        if (text.empty() || text.size() > N * 4 || text.find('\0') != std::string_view::npos) return false;
        const auto count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
            static_cast<int>(text.size()), buffer, static_cast<int>(N - 1));
        if (!count) return false;
        buffer[count] = L'\0';
        return true;
    }
    ShellAdapter* shell_;
    NOTIFYICONDATAW data_{};
    std::wstring tooltip_;
    bool published_ = false;
};
} // namespace phpvpn::tray
