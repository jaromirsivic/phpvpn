// This entry point is compiled only into the explicit isolated E2E target.
// Production phpvpn-service has no CLI or environment override for these names.
#include "service_host.h"
#include "platform/windows/paths_win.h"

int wmain() {
    auto directory = phpvpn::executableDirectory();
    if (!directory) return 1;
    phpvpn::ServiceHostEnvironment environment;
    environment.serviceName = L"phpvpn-plan3-e2e-d6c8";
    environment.pipeName = L"\\\\.\\pipe\\phpvpn-plan3-e2e-d6c8";
    environment.adapterName = L"PHPVPN Plan3 E2E d6c8";
    environment.adapterGuid = L"{4b67d77c-70a2-49f6-97db-a5f4d6c8e2e1}";
    environment.dataDirectory = *directory / "isolated-data";
    return phpvpn::runAsService(environment);
}
