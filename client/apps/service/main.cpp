// Entry point of the PHPVPN Windows Service.
//
// Without an argument the process assumes it was started by the service control
// manager. `--console` runs the same engine in the foreground for debugging, and
// `--install` and `--uninstall` are the registration helpers that install.bat
// calls so the batch file never has to reproduce the service parameters.

#include <windows.h>

#include <cstdio>
#include <string>
#include <vector>

#include "platform/iface/tunnel.h"
#include "platform/windows/paths_win.h"
#include "service_host.h"

namespace {

int printUsage() {
    std::fputs(
        "usage: phpvpn-service [--service | --console | --install | --uninstall |\n"
        "                       --verify-tunnel]\n"
        "\n"
        "  --service        run under the Windows service control manager (default)\n"
        "  --console        run in the foreground for debugging\n"
        "  --install        register the service\n"
        "  --uninstall      stop and remove the service\n"
        "  --verify-tunnel  resolve the tunnel driver allowlist without creating\n"
        "                   an adapter, which is step 4 of the installation\n",
        stdout);
    return 2;
}

// Step 4 of plan_client.md chapter 20.2. Loading the library and resolving the
// whole allowlist proves the package is usable without touching a single
// network setting, so a failure here can still end the installation cleanly.
int verifyTunnelDriver() {
    auto directory = phpvpn::executableDirectory();
    if (!directory.ok()) {
        std::fputs("the installation directory could not be determined\n", stderr);
        return 1;
    }

    auto driver = phpvpn::createPlatformTunnelDriver();
    if (driver == nullptr) {
        std::fputs("the tunnel driver backend is unavailable\n", stderr);
        return 1;
    }

    auto loaded = driver->loadAndVerify(directory.value().wstring());
    if (!loaded.ok()) {
        std::fputs(loaded.error().message().c_str(), stderr);
        std::fputc('\n', stderr);
        return 1;
    }

    // The kernel driver only reports a version once an adapter loaded it, and
    // this check deliberately creates none, so zero is a normal result here.
    const std::uint32_t version = driver->runningDriverVersion();
    if (version == 0) {
        std::fputs("the tunnel driver API is complete, no driver is loaded yet\n", stdout);
    } else {
        std::printf("the tunnel driver API is complete, running driver version %u.%u\n",
                    version >> 16, version & 0xFFFFu);
    }
    return 0;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    std::wstring mode = argc > 1 ? argv[1] : L"--service";

    if (mode == L"--service") {
        return phpvpn::runAsService();
    }
    if (mode == L"--console") {
        return phpvpn::runInConsole();
    }
    if (mode == L"--install") {
        auto directory = phpvpn::executableDirectory();
        if (!directory.ok()) {
            std::fputs("the installation directory could not be determined\n", stderr);
            return 1;
        }
        const std::wstring executable = (directory.value() / "phpvpn-service.exe").wstring();
        auto installed = phpvpn::installService(executable);
        if (installed.autostartWarning)
            std::fprintf(stdout, "WARNING: phpvpn-tray autostart registration failed (Win32 %lu). Retry --install.\n", installed.autostartWarning);
        if (!installed.critical.ok()) {
            std::fputs(installed.critical.error().message().c_str(), stderr);
            std::fputc('\n', stderr);
            return 1;
        }
        std::fputs("the service was registered\n", stdout);
        return 0;
    }
    if (mode == L"--verify-tunnel") {
        return verifyTunnelDriver();
    }
    if (mode == L"--uninstall") {
        auto removed = phpvpn::uninstallService();
        if (removed.autostartWarning)
            std::fprintf(stdout, "WARNING: phpvpn-tray autostart removal failed (Win32 %lu). Retry --uninstall.\n", removed.autostartWarning);
        if (!removed.critical.ok()) {
            std::fputs(removed.critical.error().message().c_str(), stderr);
            std::fputc('\n', stderr);
            return 1;
        }
        std::fputs("the service was removed\n", stdout);
        return 0;
    }

    return printUsage();
}
