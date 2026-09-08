#include "ui_resources.h"
#include "resource.h"
#include <algorithm>

namespace phpvpn::tray {
namespace {
int systemColor(ui::Color role) {
    switch (role) {
        case ui::Color::text: case ui::Color::secondaryText: case ui::Color::border:
        case ui::Color::separator: case ui::Color::success: case ui::Color::error:
        case ui::Color::warning: case ui::Color::successBorder: case ui::Color::errorBorder: return COLOR_WINDOWTEXT;
        case ui::Color::primary: case ui::Color::primaryHover: case ui::Color::primaryPressed:
        case ui::Color::selectedStep: case ui::Color::focus: return COLOR_HIGHLIGHT;
        case ui::Color::primaryText: return COLOR_HIGHLIGHTTEXT;
        case ui::Color::disabledText: return COLOR_GRAYTEXT;
        default: return COLOR_WINDOW;
    }
}
}
COLORREF UiResources::color(ui::Color role) const {
    if (highContrast_) return GetSysColor(systemColor(role));
    const auto rgb = ui::colors[static_cast<std::size_t>(role)];
    return RGB((rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255);
}
HBRUSH UiResources::brush(ui::Color role) const {
    // System brushes are borrowed and never passed to DeleteObject.
    return highContrast_ ? GetSysColorBrush(systemColor(role)) : brushes_[static_cast<std::size_t>(role)].get();
}
bool UiResources::update(HINSTANCE instance, int dpi, bool loadStateIcons) {
    if (dpi <= 0) return false;
    HIGHCONTRASTW contrast{sizeof(contrast)};
    const bool highContrast = SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(contrast), &contrast, 0) &&
        (contrast.dwFlags & HCF_HIGHCONTRASTON) != 0;
    const bool sameAssets = dpi_ == dpi && instance_ == instance && hasIcons_ == loadStateIcons;
    bool samePalette = highContrast_ == highContrast;
    if (highContrast) for (std::size_t i = 0; i < palette_.size(); ++i)
        samePalette = samePalette && palette_[i] == GetSysColor(systemColor(static_cast<ui::Color>(i)));
    if (sameAssets && samePalette) return true;
    UiResources next;
    next.dpi_ = dpi; next.instance_ = instance; next.hasIcons_ = loadStateIcons; next.highContrast_ = highContrast;
    if (!sameAssets) for (std::size_t i = 0; i < next.fonts_.size(); ++i) {
        const auto spec = ui::fonts[i];
        next.fonts_[i].reset(CreateFontW(-scaleForDpi(spec.height, dpi), 0, 0, 0, spec.weight,
            FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, spec.mono ? FIXED_PITCH | FF_MODERN : DEFAULT_PITCH,
            spec.mono ? L"Consolas" : L"Segoe UI"));
        if (!next.fonts_[i].get()) return false;
    }
    for (std::size_t i = 0; i < next.brushes_.size(); ++i) {
        const auto color = next.color(static_cast<ui::Color>(i));
        next.palette_[i] = color;
        next.brushes_[i].reset(CreateSolidBrush(color));
        next.pens_[i].reset(CreatePen(PS_SOLID, std::max(1, scaleForDpi(1, dpi)), color));
        if (!next.brushes_[i].get() || !next.pens_[i].get()) return false;
    }
    if (!sameAssets && loadStateIcons) {
        constexpr int ids[]{IDI_PHPVPN_DISCONNECTED, IDI_PHPVPN_CONNECTING, IDI_PHPVPN_CONNECTED};
        for (std::size_t i = 0; i < next.icons_.size(); ++i) {
            const int size = GetSystemMetricsForDpi(SM_CXSMICON, static_cast<UINT>(dpi));
            next.icons_[i].reset(static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(ids[i]), IMAGE_ICON, size, size, 0)));
            if (!next.icons_[i].get()) return false;
        }
    }
    if (sameAssets) {
        // Palette changes do not reload icons or invalidate control font handles.
        brushes_ = std::move(next.brushes_); pens_ = std::move(next.pens_);
        palette_ = next.palette_; highContrast_ = highContrast;
    } else *this = std::move(next);
    return true;
}
void UiResources::clear() noexcept { *this = UiResources{}; }
} // namespace phpvpn::tray
