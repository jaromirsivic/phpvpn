// Windows Service host.
//
// The service owns the configuration, the Wintun adapter, the session, every
// network operation and all three worker roles. The tray and the console
// application are only local IPC clients and never touch any of that.
#pragma once

#include <string>
#include <filesystem>

#include "core/base/result.h"

namespace phpvpn {

// Runs the process as a Windows Service. Returns after the service stopped.
int runAsService();

// Internal host construction for the opt-in isolated SCM integration target.
// The installed executable never accepts these values from CLI/JSON/IPC.
struct ServiceHostEnvironment {
    std::wstring serviceName = L"phpvpn";
    std::filesystem::path dataDirectory;
    std::wstring pipeName;
    std::wstring adapterName = L"PHPVPN";
    std::wstring adapterGuid = L"{7d1b4d2f-1f34-4d0e-9a3e-2f6c9b5a41d8}";
};
int runAsService(const ServiceHostEnvironment& environment);

// Runs the same engine in the foreground, which is how the service is debugged
// and how the developer install verifies a build.
int runInConsole();

// Registration helpers used by install.bat through the service executable, so
// the batch file never has to reproduce the service parameters.
Status installService(const std::wstring& executablePath);
Status uninstallService();

}  // namespace phpvpn
