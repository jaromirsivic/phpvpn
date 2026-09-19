#pragma once
#include <filesystem>
#include <functional>
#include <string>
#include "core/base/result.h"

namespace phpvpn {
inline constexpr wchar_t kTrayRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
inline constexpr wchar_t kTrayRunValue[] = L"phpvpn-tray";
// Backend operations atomically set one complete REG_SZ or remove only our value.
struct TrayAutostartBackend {
    std::function<bool(const std::filesystem::path&)> isFile;
    std::function<unsigned long(const std::wstring&)> set;
    std::function<unsigned long()> remove;
};
Result<std::wstring> buildTrayAutostartCommand(const std::wstring& servicePath,
    const std::function<bool(const std::filesystem::path&)>& isFile);
unsigned long registerTrayAutostart(const std::wstring& servicePath, const TrayAutostartBackend& backend);
TrayAutostartBackend windowsTrayAutostartBackend();
struct ServiceRegistrationResult {
    Status critical;
    unsigned long autostartWarning = 0;
};
ServiceRegistrationResult registerServiceWithTray(const std::wstring& path,
    const std::function<Status()>& critical, const TrayAutostartBackend& backend);
ServiceRegistrationResult removeServiceWithTray(const std::function<Status()>& critical,
    const TrayAutostartBackend& backend);
}
