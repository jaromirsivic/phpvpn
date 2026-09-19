#pragma once
#include <windows.h>
#include <array>
#include <utility>
#include "ui_style.h"

namespace phpvpn::tray {
template<class Handle, class Deleter> class UniqueUiHandle {
public:
    UniqueUiHandle() = default;
    explicit UniqueUiHandle(Handle value) : value_(value) {}
    ~UniqueUiHandle() { reset(); }
    UniqueUiHandle(const UniqueUiHandle&) = delete;
    UniqueUiHandle& operator=(const UniqueUiHandle&) = delete;
    UniqueUiHandle(UniqueUiHandle&& other) noexcept : value_(other.release()) {}
    UniqueUiHandle& operator=(UniqueUiHandle&& other) noexcept {
        if (this != &other) reset(other.release());
        return *this;
    }
    Handle get() const noexcept { return value_; }
    Handle release() noexcept { return std::exchange(value_, nullptr); }
    void reset(Handle value = nullptr) noexcept {
        if (value == value_) return;
        if (value_) Deleter{}(value_);
        value_ = value;
    }
private:
    Handle value_ = nullptr;
};
struct DeleteGdi { void operator()(HGDIOBJ value) const noexcept { DeleteObject(value); } };
struct DeleteIcon { void operator()(HICON value) const noexcept { DestroyIcon(value); } };
using UniqueFont = UniqueUiHandle<HFONT, DeleteGdi>;
using UniqueBrush = UniqueUiHandle<HBRUSH, DeleteGdi>;
using UniquePen = UniqueUiHandle<HPEN, DeleteGdi>;
using UniqueIcon = UniqueUiHandle<HICON, DeleteIcon>;

// UI-thread/window owned. Getters never allocate, load or take ownership.
// Update outside paint with no cached handle selected in a DC. Reassign native
// control fonts immediately after a successful DPI update, before any painting
// or message pumping. Destroy controls before clear().
class UiResources {
public:
    bool update(HINSTANCE instance, int dpi, bool loadStateIcons = true);
    void applyApplicationIcons(HWND window);
    HICON appIconSmall() const { return appSmall_.get(); }
    HICON appIconLarge() const { return appLarge_.get(); }
    void clear() noexcept;
    int dpi() const noexcept { return dpi_; }
    HFONT font(ui::Font role) const { return fonts_[static_cast<std::size_t>(role)].get(); }
    COLORREF color(ui::Color role) const;
    HBRUSH brush(ui::Color role) const;
    HPEN pen(ui::Color role) const { return pens_[static_cast<std::size_t>(role)].get(); }
    HICON icon(std::size_t state) const { return state < icons_.size() ? icons_[state].get() : nullptr; }
    bool highContrast() const noexcept { return highContrast_; }
private:
    std::array<UniqueFont, static_cast<std::size_t>(ui::Font::count)> fonts_;
    std::array<UniqueBrush, static_cast<std::size_t>(ui::Color::count)> brushes_;
    std::array<UniquePen, static_cast<std::size_t>(ui::Color::count)> pens_;
    std::array<UniqueIcon, 3> icons_;
    UniqueIcon appSmall_, appLarge_;
    HWND applicationWindow_ = nullptr;
    std::array<COLORREF, static_cast<std::size_t>(ui::Color::count)> palette_{};
    int dpi_ = 0;
    HINSTANCE instance_ = nullptr;
    bool hasIcons_ = false, highContrast_ = false;
};
} // namespace phpvpn::tray
