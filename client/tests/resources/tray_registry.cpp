#include "platform/windows/tray_autostart.h"
#include <windows.h>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
void require(bool value) { if (!value) throw std::runtime_error("redirected registry assertion failed"); }
struct IsolatedRegistry {
    std::wstring path = L"Software\\PHPVPN-tests\\autostart-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64());
    HKEY root = nullptr;
    bool redirected = false, created = false;
    ~IsolatedRegistry() {
        if (redirected) RegOverridePredefKey(HKEY_LOCAL_MACHINE, nullptr);
        if (root) RegCloseKey(root);
        if (created) RegDeleteTreeW(HKEY_CURRENT_USER, path.c_str());
    }
};
std::wstring read(HKEY key, const wchar_t* name) {
    DWORD type = 0, bytes = 0;
    require(RegQueryValueExW(key, name, nullptr, &type, nullptr, &bytes) == ERROR_SUCCESS);
    require(type == REG_SZ && bytes >= sizeof(wchar_t) && bytes % sizeof(wchar_t) == 0);
    std::vector<wchar_t> buffer(bytes / sizeof(wchar_t));
    require(RegQueryValueExW(key, name, nullptr, &type, reinterpret_cast<BYTE*>(buffer.data()), &bytes) == ERROR_SUCCESS);
    require(buffer.back() == L'\0');
    return {buffer.data(), buffer.size() - 1};
}
}
int main() {
    try {
        IsolatedRegistry isolated;
        DWORD disposition = 0;
        require(RegCreateKeyExW(HKEY_CURRENT_USER, isolated.path.c_str(), 0, nullptr, 0, KEY_ALL_ACCESS,
            nullptr, &isolated.root, &disposition) == ERROR_SUCCESS);
        isolated.created = disposition == REG_CREATED_NEW_KEY;
        require(isolated.created);
        HKEY key = nullptr;
        require(RegCreateKeyExW(isolated.root, phpvpn::kTrayRunKey, 0, nullptr, 0, KEY_ALL_ACCESS | KEY_WOW64_64KEY,
            nullptr, &key, nullptr) == ERROR_SUCCESS);
        struct CloseKey { HKEY key; ~CloseKey() { if (key) RegCloseKey(key); } } close{key};
        const wchar_t neighbor[] = L"unchanged neighbor";
        require(RegSetValueExW(key, L"neighbor", 0, REG_SZ, reinterpret_cast<const BYTE*>(neighbor), sizeof(neighbor)) == ERROR_SUCCESS);
        require(RegOverridePredefKey(HKEY_LOCAL_MACHINE, isolated.root) == ERROR_SUCCESS);
        isolated.redirected = true;
        auto backend = phpvpn::windowsTrayAutostartBackend();
        const std::wstring command = L"\"C:\\Program Files\\P\u0159\u00edli\u0161\\phpvpn-tray.exe\"";
        for (int i = 0; i < 2; ++i) {
            require(backend.set(command) == ERROR_SUCCESS);
            require(read(key, phpvpn::kTrayRunValue) == command);
            require(read(key, L"neighbor") == neighbor);
        }
        for (int i = 0; i < 2; ++i) require(backend.remove() == ERROR_SUCCESS);
        DWORD bytes = 0;
        require(RegQueryValueExW(key, phpvpn::kTrayRunValue, nullptr, nullptr, nullptr, &bytes) == ERROR_FILE_NOT_FOUND);
        require(read(key, L"neighbor") == neighbor);
        RegCloseKey(close.key); close.key = nullptr;
        require(RegDeleteTreeW(isolated.root, phpvpn::kTrayRunKey) == ERROR_SUCCESS);
        require(backend.remove() == ERROR_SUCCESS);
        std::cout << "PASS: real Win32 registry backend, private HKCU redirection only, exact REG_SZ, repeat install/remove and neighbor preservation\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
