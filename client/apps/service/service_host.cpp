#include "service_host.h"
#include "service_registration.h"

#include <windows.h>

#include <atomic>
#include <memory>
#include <string>

#include "core/base/logger.h"
#include "core/ipc/ipc_dispatcher.h"
#include "core/runtime/vpn_engine.h"
#include "platform/windows/ipc_pipe.h"
#include "platform/windows/paths_win.h"

#pragma comment(lib, "advapi32.lib")

namespace phpvpn {
namespace {

constexpr const char* kComponent = "service";
constexpr const wchar_t* kServiceName = L"phpvpn";
constexpr const wchar_t* kServiceDisplayName = L"PHPVPN Client";
constexpr const wchar_t* kServiceDescription =
    L"Maintains the PHPVPN virtual interface and the authenticated relay session.";

SERVICE_STATUS_HANDLE g_statusHandle = nullptr;
SERVICE_STATUS g_status{};
HANDLE g_stopEvent = nullptr;

void reportStatus(DWORD currentState, DWORD exitCode, DWORD waitHintMs) {
    static DWORD checkPoint = 1;

    g_status.dwCurrentState = currentState;
    g_status.dwWin32ExitCode = exitCode;
    g_status.dwWaitHint = waitHintMs;
    g_status.dwControlsAccepted =
        currentState == SERVICE_START_PENDING ? 0 : SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN;
    g_status.dwCheckPoint = (currentState == SERVICE_RUNNING || currentState == SERVICE_STOPPED)
                                ? 0
                                : checkPoint++;

    if (g_statusHandle != nullptr) {
        SetServiceStatus(g_statusHandle, &g_status);
    }
}

// Everything the running service owns, so both the service and the console mode
// share one construction and one shutdown path.
class ServiceRuntime {
public:
    Status start() {
        std::filesystem::path data;
        PHPVPN_TRY_ASSIGN(data, dataDirectory());

        // The stored keys must not be readable by a standard user; the pipe
        // stays deliberately public, which is a separate, documented decision.
        auto restricted = restrictDirectoryToAdministrators(data);
        if (!restricted.ok()) {
            PHPVPN_LOG_WARN(kComponent, "the data directory permissions could not be tightened");
        }

        Logger::instance().configureFile(logDirectory(data), "phpvpn-service");
        PHPVPN_LOG_INFO(kComponent, "the service is starting");

        std::filesystem::path installDirectory;
        PHPVPN_TRY_ASSIGN(installDirectory, executableDirectory());

        PHPVPN_TRY_ASSIGN(crypto_, createPlatformCryptoProvider());
        PHPVPN_TRY_ASSIGN(transport_, createPlatformTransport());
        clock_ = createPlatformClock();
        tunnelDriver_ = createPlatformTunnelDriver();
        netConfig_ = createPlatformNetConfig();

        EnginePaths paths;
        paths.configFile = configFilePath(data);
        paths.journalFile = journalFilePath(data);
        paths.installDirectory = installDirectory.wstring();

        engine_ = std::make_unique<VpnEngine>(*crypto_, *transport_, *clock_, *tunnelDriver_,
                                              *netConfig_, std::move(paths));
        PHPVPN_TRY(engine_->start());

        dispatcher_ = std::make_unique<IpcDispatcher>(*engine_);
        server_ = std::make_unique<IpcPipeServer>(
            [this](std::string_view request) { return dispatcher_->handle(request); });
        PHPVPN_TRY(server_->start());

        PHPVPN_LOG_INFO(kComponent, "the service is ready");
        return Status::success();
    }

    void stop() {
        PHPVPN_LOG_INFO(kComponent, "the service is stopping");
        if (server_) {
            server_->stop();
            server_.reset();
        }
        dispatcher_.reset();
        if (engine_) {
            engine_->shutdown();
            engine_.reset();
        }
        netConfig_.reset();
        tunnelDriver_.reset();
        transport_.reset();
        clock_.reset();
        crypto_.reset();
        Logger::instance().flush();
    }

private:
    std::unique_ptr<CryptoProvider> crypto_;
    std::unique_ptr<Transport> transport_;
    std::unique_ptr<Clock> clock_;
    std::unique_ptr<TunnelDriver> tunnelDriver_;
    std::unique_ptr<NetConfig> netConfig_;
    std::unique_ptr<VpnEngine> engine_;
    std::unique_ptr<IpcDispatcher> dispatcher_;
    std::unique_ptr<IpcPipeServer> server_;
};

ServiceRuntime g_runtime;

DWORD WINAPI serviceControlHandler(DWORD control, DWORD, LPVOID, LPVOID) {
    switch (control) {
        case SERVICE_CONTROL_STOP:
        case SERVICE_CONTROL_SHUTDOWN:
            reportStatus(SERVICE_STOP_PENDING, NO_ERROR, 30000);
            if (g_stopEvent != nullptr) {
                SetEvent(g_stopEvent);
            }
            return NO_ERROR;
        case SERVICE_CONTROL_INTERROGATE:
            return NO_ERROR;
        default:
            return ERROR_CALL_NOT_IMPLEMENTED;
    }
}

void WINAPI serviceMain(DWORD, LPWSTR*) {
    g_status.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    g_statusHandle = RegisterServiceCtrlHandlerExW(kServiceName, serviceControlHandler, nullptr);
    if (g_statusHandle == nullptr) {
        return;
    }

    reportStatus(SERVICE_START_PENDING, NO_ERROR, 30000);

    g_stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (g_stopEvent == nullptr) {
        reportStatus(SERVICE_STOPPED, ERROR_OUTOFMEMORY, 0);
        return;
    }

    auto started = g_runtime.start();
    if (!started.ok()) {
        PHPVPN_LOG_CRITICAL(kComponent, started.error().message());
        reportStatus(SERVICE_STOPPED, ERROR_SERVICE_SPECIFIC_ERROR, 0);
        return;
    }

    reportStatus(SERVICE_RUNNING, NO_ERROR, 0);
    WaitForSingleObject(g_stopEvent, INFINITE);

    g_runtime.stop();
    CloseHandle(g_stopEvent);
    g_stopEvent = nullptr;
    reportStatus(SERVICE_STOPPED, NO_ERROR, 0);
}

BOOL WINAPI consoleControlHandler(DWORD control) {
    if (control == CTRL_C_EVENT || control == CTRL_BREAK_EVENT || control == CTRL_CLOSE_EVENT) {
        if (g_stopEvent != nullptr) {
            SetEvent(g_stopEvent);
        }
        return TRUE;
    }
    return FALSE;
}

}  // namespace

int runAsService() {
    SERVICE_TABLE_ENTRYW table[] = {{const_cast<LPWSTR>(kServiceName), serviceMain},
                                    {nullptr, nullptr}};
    if (!StartServiceCtrlDispatcherW(table)) {
        return static_cast<int>(GetLastError());
    }
    return 0;
}

int runInConsole() {
    g_stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    SetConsoleCtrlHandler(consoleControlHandler, TRUE);

    Logger::instance().setSecondarySink([](LogLevel, const std::string& line) {
        std::fputs(line.c_str(), stdout);
        std::fputc('\n', stdout);
    });

    auto started = g_runtime.start();
    if (!started.ok()) {
        std::fputs(started.error().message().c_str(), stderr);
        std::fputc('\n', stderr);
        return 1;
    }

    std::fputs("phpvpn service is running in the foreground, press Ctrl+C to stop\n", stdout);
    WaitForSingleObject(g_stopEvent, INFINITE);

    g_runtime.stop();
    CloseHandle(g_stopEvent);
    g_stopEvent = nullptr;
    return 0;
}

Status installService(const std::wstring& executablePath) {
    SC_HANDLE manager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CREATE_SERVICE);
    if (manager == nullptr) {
        return Error(ErrorCode::internal, "the service manager could not be opened");
    }

    const std::wstring command = L"\"" + executablePath + L"\" --service";
    SC_HANDLE service = CreateServiceW(manager, kServiceName, kServiceDisplayName,
                                       SERVICE_ALL_ACCESS, SERVICE_WIN32_OWN_PROCESS,
                                       SERVICE_AUTO_START, SERVICE_ERROR_NORMAL, command.c_str(),
                                       nullptr, nullptr, kServiceDependencies, nullptr, nullptr);
    if (service == nullptr) {
        const DWORD error = GetLastError();
        CloseServiceHandle(manager);
        if (error == ERROR_SERVICE_EXISTS) {
            return Error(ErrorCode::service_already_running, "the service is already registered");
        }
        return Error(ErrorCode::internal, "the service could not be registered");
    }

    SERVICE_DESCRIPTIONW description{};
    description.lpDescription = const_cast<LPWSTR>(kServiceDescription);
    ChangeServiceConfig2W(service, SERVICE_CONFIG_DESCRIPTION, &description);

    // Restart the service on an unexpected termination, which keeps the tunnel
    // ownership recovery path in charge of any leftovers.
    SC_ACTION actions[3] = {{SC_ACTION_RESTART, 5000},
                            {SC_ACTION_RESTART, 15000},
                            {SC_ACTION_RESTART, 60000}};
    SERVICE_FAILURE_ACTIONSW failure{};
    failure.dwResetPeriod = 86400;
    failure.cActions = 3;
    failure.lpsaActions = actions;
    ChangeServiceConfig2W(service, SERVICE_CONFIG_FAILURE_ACTIONS, &failure);

    CloseServiceHandle(service);
    CloseServiceHandle(manager);
    return Status::success();
}

Status uninstallService() {
    SC_HANDLE manager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (manager == nullptr) {
        return Error(ErrorCode::internal, "the service manager could not be opened");
    }

    SC_HANDLE service = OpenServiceW(manager, kServiceName, SERVICE_STOP | DELETE | SERVICE_QUERY_STATUS);
    if (service == nullptr) {
        CloseServiceHandle(manager);
        return Error(ErrorCode::service_not_running, "the service is not registered");
    }

    SERVICE_STATUS status{};
    ControlService(service, SERVICE_CONTROL_STOP, &status);
    for (int attempt = 0; attempt < 60; ++attempt) {
        if (!QueryServiceStatus(service, &status) || status.dwCurrentState == SERVICE_STOPPED) {
            break;
        }
        Sleep(500);
    }

    const BOOL deleted = DeleteService(service);
    CloseServiceHandle(service);
    CloseServiceHandle(manager);

    if (!deleted) {
        return Error(ErrorCode::internal, "the service could not be removed");
    }
    return Status::success();
}

}  // namespace phpvpn
