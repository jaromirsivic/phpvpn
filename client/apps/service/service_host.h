// Windows Service host.
//
// The service owns the configuration, the Wintun adapter, the session, every
// network operation and all three worker roles. The tray and the console
// application are only local IPC clients and never touch any of that.
#pragma once

#include <string>

#include "core/base/result.h"

namespace phpvpn {

// Runs the process as a Windows Service. Returns after the service stopped.
int runAsService();

// Runs the same engine in the foreground, which is how the service is debugged
// and how the developer install verifies a build.
int runInConsole();

// Registration helpers used by install.bat through the service executable, so
// the batch file never has to reproduce the service parameters.
Status installService(const std::wstring& executablePath);
Status uninstallService();

}  // namespace phpvpn
