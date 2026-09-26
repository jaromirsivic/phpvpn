#pragma once

// Tray IPC budgets and polling intervals in milliseconds, shared by the Win32
// and GTK trays so both keep the same timing behaviour.
namespace phpvpn::tray {
// Read-only get-status request of the tray, progress window and Wizard gate.
inline constexpr unsigned kStatusRequestTimeoutMs = 400;
// Ordinary commands and the set-config (Edit Config) request.
inline constexpr unsigned kCommandTimeoutMs = 30000;
// One Wizard IPC request, also used for the Contracts configuration check.
inline constexpr unsigned kWizardRequestTimeoutMs = 5000;
// Best effort Wizard session close while the window is destroyed.
inline constexpr unsigned kWizardCloseTimeoutMs = 1000;
// Editor "Reset to default" get-default-config request.
inline constexpr unsigned kEditorResetTimeoutMs = 5000;
// Absolute deadlines of an Edit Config and a Communication Contracts save.
inline constexpr unsigned kEditorSaveDeadlineMs = 30000;
inline constexpr unsigned kContractsSaveDeadlineMs = 30000;
// Tray status polling and the faster progress window polling.
inline constexpr unsigned kStatusIntervalMs = 5000;
inline constexpr unsigned kProgressIntervalMs = 500;
// Server Info automatic refresh period.
inline constexpr unsigned kServerInfoAutoRefreshMs = 5000;
} // namespace phpvpn::tray
