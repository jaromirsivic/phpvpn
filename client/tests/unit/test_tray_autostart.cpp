#include "harness.h"
#include "platform/windows/tray_autostart.h"
#include <windows.h>

namespace phpvpn {
PHPVPN_TEST(tray_autostart, command_is_absolute_quoted_and_bounded) {
    const auto exists = [](const auto&) { return true; };
    auto command = buildTrayAutostartCommand(L"C:\\Program Files\\Příliš\\phpvpn-service.exe", exists);
    PHPVPN_REQUIRE_OK(command);
    PHPVPN_CHECK_EQ(*command, std::wstring(L"\"C:\\Program Files\\Příliš\\phpvpn-tray.exe\""));
    for (const auto& path : {std::wstring(), std::wstring(L"phpvpn-service.exe"),
        std::wstring(L"C:\\x\"\\phpvpn-service.exe"), std::wstring(L"C:\\x\\..\\phpvpn-service.exe"),
        L"C:\\" + std::wstring(260, L'x') + L"\\phpvpn-service.exe",
        std::wstring(L"C:\\x\0z\\phpvpn-service.exe", 26)})
        PHPVPN_CHECK(!buildTrayAutostartCommand(path, exists));
    PHPVPN_CHECK(!buildTrayAutostartCommand(L"C:\\x\\phpvpn-service.exe", [](const auto&) { return false; }));
}
PHPVPN_TEST(tray_autostart, critical_and_auxiliary_failures_are_independent) {
    std::wstring stored = L"previous complete value";
    unsigned writes = 0, removals = 0, scm = 0;
    unsigned long failure = ERROR_ACCESS_DENIED;
    TrayAutostartBackend backend{[](const auto&) { return true; }, [&](const auto& value) {
        ++writes; if (!failure) stored = value; return failure;
    }, [&] { ++removals; if (!failure) stored.clear(); return failure; }};
    auto critical = [&] { ++scm; return ok(); };
    auto result = registerServiceWithTray(L"C:\\app\\phpvpn-service.exe", critical, backend);
    PHPVPN_CHECK(result.critical.ok());
    PHPVPN_CHECK_EQ(result.autostartWarning, static_cast<unsigned long>(ERROR_ACCESS_DENIED));
    PHPVPN_CHECK_EQ(stored, std::wstring(L"previous complete value"));
    failure = 0;
    for (int i = 0; i < 2; ++i) PHPVPN_CHECK(registerServiceWithTray(L"C:\\app\\phpvpn-service.exe", critical, backend).critical.ok());
    PHPVPN_CHECK_EQ(stored, std::wstring(L"\"C:\\app\\phpvpn-tray.exe\""));
    const auto before = writes;
    result = registerServiceWithTray(L"C:\\app\\phpvpn-service.exe", [] { return Status(Error(ErrorCode::internal)); }, backend);
    PHPVPN_CHECK(!result.critical);
    PHPVPN_CHECK_EQ(writes, before);
    failure = ERROR_ACCESS_DENIED;
    result = removeServiceWithTray(critical, backend);
    PHPVPN_CHECK(result.critical.ok());
    PHPVPN_CHECK_EQ(result.autostartWarning, failure);
    result = removeServiceWithTray([] { return Status(Error(ErrorCode::internal)); }, backend);
    PHPVPN_CHECK(!result.critical);
    PHPVPN_CHECK_EQ(removals, 2u);
    failure = 0;
    for (int i = 0; i < 2; ++i) PHPVPN_CHECK(removeServiceWithTray(critical, backend).critical.ok());
    PHPVPN_CHECK(stored.empty());
}
}
