#pragma once
#include <array>
#include <cstdint>
#include <string>
#include "layout.h"

namespace phpvpn::tray::ui {
// Portable English presentation constants, not localization keys or an ABI.
inline constexpr wchar_t product[] = L"PHPVPN";
inline constexpr wchar_t startupError[] = L"The PHPVPN tray application could not start.";
inline constexpr wchar_t disconnectedTip[] = L"PHPVPN: disconnected";
inline constexpr wchar_t connectedTip[] = L"PHPVPN: connected";
inline constexpr wchar_t connectingTip[] = L"PHPVPN: connecting";
inline constexpr wchar_t recoveringTip[] = L"PHPVPN: recovering";
inline constexpr wchar_t unavailableTip[] = L"PHPVPN: service status unavailable";
inline constexpr wchar_t errorTip[] = L"PHPVPN: error";
// Four connection checks and the result panel need 596 DIP, plus room for wrapped error text.
inline constexpr Size connectClient{940, 640}, disconnectClient{760, 192}, editorClient{1080, 560},
    statisticsClient{1000, 650}, wizardClient{1000, 680}, contractsClient{1180, 680}, passwordClient{675, 360};
inline constexpr int spacing = 4, fieldGap = 8, relatedGap = 12, sectionGap = 24,
    cardPadding = 24, contentMargin = 32, compactMargin = 16, cardRadius = 8,
    buttonHeight = 36, buttonMinWidth = 88, buttonPadding = 16, rowHeight = 48,
    headerHeight = 40, statisticsBreakpoint = 760;
enum class Font { heading, result, card, subtitle, label, value, strongValue, counter, hint, mono, contractsHeader, count };
struct FontSpec { int height; int weight; bool mono = false; };
inline constexpr std::array<FontSpec, static_cast<std::size_t>(Font::count)> fonts{{
    {28, 600}, {34, 600}, {20, 600}, {17, 400}, {15, 400}, {16, 400},
    {16, 600}, {24, 600}, {13, 400}, {14, 400, true}, {16, 700}}};
enum class Color { background, surface, text, secondaryText, border, separator, primary,
    primaryHover, primaryPressed, primaryText, selectedStep, success, successBackground,
    successBorder, warning, warningBackground, error, errorBackground, errorBorder,
    disabledText, disabledBackground, focus, buttonNeutral, buttonNeutralHover,
    buttonNeutralPressed, buttonReset, buttonResetHover, buttonResetPressed,
    buttonPrimaryDisabled, buttonResetDisabled, buttonNeutralFocus, contractsHeader, readOnlyText, contractsChanged, serverInfoValid, count };
inline constexpr std::array<std::uint32_t, static_cast<std::size_t>(Color::count)> colors{
    0xF7F8FA, 0xFFFFFF, 0x0D1533, 0x4B5C82, 0xD6DCE7, 0xE1E5ED, 0x0062E8,
    0x0056CE, 0x0049AF, 0xFFFFFF, 0xE5EDF8, 0x087C2B, 0xF0FAF2, 0xA8D8B1,
    0x995400, 0xFFF6E5, 0xC82020, 0xFFF2F2, 0xE9A4A4, 0x747C8B, 0xF0F2F5, 0x005FCC,
    0xF0F2F5, 0xE4E8EF, 0xD6DCE7, 0xE02E2A, 0xC42724, 0xA8201D,
    0xB4C9E5, 0xDEBABA, 0x454B54, 0x454B54, 0x454B54, 0xFFF2B3, 0xDDF2E2};
// Exact uint64 rendering, independent of the process/Windows locale.
inline std::wstring englishInteger(std::uint64_t value) {
    auto result = std::to_wstring(value);
    for (std::size_t i = result.size(); i > 3;) { i -= 3; result.insert(i, 1, L','); }
    return result;
}
} // namespace phpvpn::tray::ui
