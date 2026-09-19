#include "tray_autostart.h"
#include <windows.h>

namespace phpvpn {
Result<std::wstring> buildTrayAutostartCommand(const std::wstring& servicePath,
    const std::function<bool(const std::filesystem::path&)>& isFile) {
    if (servicePath.empty() || servicePath.find(L'\0') != std::wstring::npos ||
        servicePath.find(L'"') != std::wstring::npos)
        return Error(ErrorCode::invalid_argument);
    const std::filesystem::path service(servicePath);
    if (!service.is_absolute() || service != service.lexically_normal() ||
        service.filename() != L"phpvpn-service.exe") return Error(ErrorCode::invalid_argument);
    const auto tray = service.parent_path() / L"phpvpn-tray.exe";
    const auto command = L"\"" + tray.wstring() + L"\"";
    if (command.size() > 260 || !isFile(tray)) return Error(ErrorCode::invalid_argument);
    return command;
}
unsigned long registerTrayAutostart(const std::wstring& path, const TrayAutostartBackend& backend) {
    const auto command = buildTrayAutostartCommand(path, backend.isFile);
    return command ? backend.set(*command) : ERROR_INVALID_PARAMETER;
}
TrayAutostartBackend windowsTrayAutostartBackend() {
    return {
        [](const auto& path) { std::error_code ec; return std::filesystem::is_regular_file(path, ec); },
        [](const std::wstring& command) -> unsigned long {
            HKEY key = nullptr;
            auto code = RegOpenKeyExW(HKEY_LOCAL_MACHINE, kTrayRunKey, 0, KEY_SET_VALUE | KEY_WOW64_64KEY, &key);
            if (code != ERROR_SUCCESS) return code;
            code = RegSetValueExW(key, kTrayRunValue, 0, REG_SZ,
                reinterpret_cast<const BYTE*>(command.c_str()), static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t)));
            RegCloseKey(key);
            return code;
        },
        []() -> unsigned long {
            HKEY key = nullptr;
            auto code = RegOpenKeyExW(HKEY_LOCAL_MACHINE, kTrayRunKey, 0, KEY_SET_VALUE | KEY_WOW64_64KEY, &key);
            if (code == ERROR_FILE_NOT_FOUND) return ERROR_SUCCESS;
            if (code != ERROR_SUCCESS) return code;
            code = RegDeleteValueW(key, kTrayRunValue);
            RegCloseKey(key);
            return code == ERROR_FILE_NOT_FOUND ? ERROR_SUCCESS : code;
        }};
}
ServiceRegistrationResult registerServiceWithTray(const std::wstring& path,
    const std::function<Status()>& critical, const TrayAutostartBackend& backend) {
    auto result = critical();
    if (!result) return {result, 0};
    return {result, registerTrayAutostart(path, backend)};
}
ServiceRegistrationResult removeServiceWithTray(const std::function<Status()>& critical,
    const TrayAutostartBackend& backend) {
    auto result = critical();
    return {result, backend.remove()};
}
}
