#pragma once
#include <array>
#include <string_view>

namespace phpvpn::tray {
enum class MenuAction { toggle, separator, wizard, contracts, edit, info, statistics, exit };
struct MenuItem { MenuAction action; std::wstring_view title; };
inline constexpr std::array<MenuItem, 10> configurationMenu{{
    {MenuAction::toggle, L"Connect"}, {MenuAction::separator, L""},
    {MenuAction::wizard, L"Configuration Wizard"}, {MenuAction::contracts, L"Communication Contracts"},
    {MenuAction::edit, L"Edit Config"}, {MenuAction::separator, L""},
    {MenuAction::info, L"Show Info"}, {MenuAction::statistics, L"Statistics"},
    {MenuAction::separator, L""}, {MenuAction::exit, L"Exit"}
}};
// Menus describe actions, never cached configuration availability. Every item
// stays clickable and its action reports a service response or a concrete error.
constexpr bool requiresConfigState(MenuAction action) {
    return action == MenuAction::contracts || action == MenuAction::statistics;
}
} // namespace phpvpn::tray
