#pragma once
#include <array>
#include <string_view>

namespace phpvpn::tray {
enum class MenuAction { toggle, separator, wizard, contracts, edit, info, statistics, about, exit };
struct MenuItem { MenuAction action; std::wstring_view title; };
inline constexpr std::array<MenuItem, 11> configurationMenu{{
    {MenuAction::toggle, L"Connect"}, {MenuAction::separator, L""},
    {MenuAction::wizard, L"Configuration Wizard"}, {MenuAction::contracts, L"Communication Contracts"},
    {MenuAction::edit, L"Edit Config"}, {MenuAction::separator, L""},
    {MenuAction::info, L"Show Server Info"}, {MenuAction::statistics, L"Statistics"},
    {MenuAction::about, L"About"}, {MenuAction::separator, L""}, {MenuAction::exit, L"Exit"}
}};
// About is local text only: the build version first, then licence and copyright.
inline constexpr std::wstring_view aboutTitle = L"About PHPVPN";
inline constexpr std::wstring_view aboutHeading = L"PHPVPN version " PHPVPN_CLIENT_VERSION;
inline constexpr std::wstring_view aboutText =
    L"PHPVPN is freeware and open source software.\nLicense: MIT\nCopyright © www.pilandrome.com";
// Menus describe actions, never cached configuration availability. Every item
// stays clickable and its action reports a service response or a concrete error.
constexpr bool requiresConfigState(MenuAction action) {
    return action == MenuAction::statistics;
}
} // namespace phpvpn::tray
