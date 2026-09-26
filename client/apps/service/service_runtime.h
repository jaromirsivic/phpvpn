// Portable composition of the running service.
//
// The runtime owns the configuration store, the engine with its platform
// backends, the automatic startup Connect, the Wizard, the IPC dispatcher and
// the IPC server, and builds and tears them down in one fixed order. Everything
// that differs per platform (data locations, directory security, the
// ConfigFiles backend, the log file and the IPC transport) arrives through
// ServicePlatformHost. The Windows SCM/console host and the Linux systemd and
// foreground entry both drive this one class.
#pragma once

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>

#include "core/base/result.h"
#include "core/runtime/status_snapshot.h"
#include "platform/iface/config_files.h"
#include "platform/iface/ipc_server.h"
#include "startup_connect.h"

namespace phpvpn {

class CryptoProvider;
class Transport;
class Clock;
class TunnelDriver;
class NetConfig;
class ConfigStore;
class VpnEngine;
class WizardService;
class IpcDispatcher;

// Storage prepared by the platform for one start.
struct ServiceStorage {
    std::filesystem::path configFile;
    std::filesystem::path journalFile;
    // Set unless storageError is set. An unusable storage does not stop the
    // service: IPC stays available and reports the configuration error.
    std::unique_ptr<ConfigFiles> files;
    std::optional<Error> storageError;
};

struct ServicePlatformHost {
    // Resolves the data locations, normalizes their security, creates the
    // ConfigFiles backend and, when the storage is usable, configures the
    // platform log file. An error aborts the start.
    std::function<Result<ServiceStorage>()> prepareStorage;
    // Directory the tunnel driver library is loaded from. Empty on Linux.
    std::function<Result<std::wstring>()> installDirectory;
    // Internal adapter identity, never taken from JSON or public IPC.
    std::wstring adapterName;
    std::wstring adapterGuid;
    // Creates, without starting, the platform IPC server around the shared
    // hooks of the runtime.
    std::function<std::unique_ptr<IpcServer>(IpcHandler, IpcServerOptions)> createIpcServer;
};

// Everything the running service owns, so every host shares one construction
// and one shutdown path.
class ServiceRuntime {
public:
    ServiceRuntime();
    ~ServiceRuntime();
    ServiceRuntime(const ServiceRuntime&) = delete;
    ServiceRuntime& operator=(const ServiceRuntime&) = delete;

    // Builds everything and starts the IPC server. After a failure the host
    // may still call stop() to release what was built.
    Status start(ServicePlatformHost host);
    // Arms the automatic startup Connect once the host reported the service as
    // running (SCM RUNNING, systemd READY=1, foreground banner).
    void requestStartupConnect();
    // Ordered teardown; safe after a partial start and when called twice.
    void stop();
    // Connection state for a supervisor status line, empty before start.
    std::optional<StatusSnapshot> status() const;

private:
    StartupConnect::Post filePost();

    ServicePlatformHost host_;
    std::unique_ptr<CryptoProvider> crypto_;
    std::unique_ptr<Transport> transport_;
    std::unique_ptr<Clock> clock_;
    std::unique_ptr<TunnelDriver> tunnelDriver_;
    std::unique_ptr<NetConfig> netConfig_;
    std::unique_ptr<ConfigStore> configStore_;
    std::unique_ptr<VpnEngine> engine_;
    std::unique_ptr<StartupConnect> startupConnect_;
    std::unique_ptr<WizardService> wizard_;
    std::unique_ptr<IpcDispatcher> dispatcher_;
    std::unique_ptr<IpcServer> server_;
};

// Pure work-class classifier and busy response of the service IPC, shared by
// every transport.
IpcWorkClass classifyServiceIpcRequest(std::string_view json);
std::string serviceIpcBusyResponse(std::string_view json);

}  // namespace phpvpn
