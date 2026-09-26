#include "service_host.h"
#include "service_registration.h"
#include "service_runtime.h"

#include <windows.h>

#include <atomic>
#include <memory>
#include <string>
#include <vector>

#include "core/base/logger.h"
#include "platform/windows/ipc_pipe.h"
#include "platform/windows/paths_win.h"
#include "platform/windows/config_files_win.h"

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
ServiceHostEnvironment g_environment;

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

// Windows platform parts of the shared runtime: %ProgramData% with the ACL
// repair, the Windows ConfigFiles backend, the rotating log file, Wintun from
// the installation directory and the named-pipe IPC server.
ServicePlatformHost windowsPlatformHost() {
    ServicePlatformHost host;
    host.prepareStorage = []() -> Result<ServiceStorage> {
        std::filesystem::path data;
        if (g_environment.dataDirectory.empty()) {
            PHPVPN_TRY_ASSIGN(data, dataDirectory(false));
        } else {
            data = g_environment.dataDirectory;
        }

        // Local users deliberately read the complete data tree, including historical
        // keys. Filesystem writes remain privileged and IPC publication validated.
        ServiceStorage storage;
        storage.configFile = configFilePath(data);
        storage.journalFile = journalFilePath(data);
        if (!storage.storageError) {
            auto secured = restrictDirectoryToAdministrators(data);
            if (!secured) storage.storageError = secured.error();
        }
        if (!storage.storageError) {
            auto backend = createWindowsConfigFiles(configFilePath(data));
            if (backend) storage.files = std::move(backend).value();
            else storage.storageError = backend.error();
        }

        if (!storage.storageError) Logger::instance().configureFile(logDirectory(data), "phpvpn-service");
        return storage;
    };
    host.installDirectory = []() -> Result<std::wstring> {
        std::filesystem::path installDirectory;
        PHPVPN_TRY_ASSIGN(installDirectory, executableDirectory());
        return installDirectory.wstring();
    };
    host.adapterName = g_environment.adapterName;
    host.adapterGuid = g_environment.adapterGuid;
    host.createIpcServer = [](IpcHandler handler, IpcServerOptions shared) -> std::unique_ptr<IpcServer> {
        IpcPipeServerOptions options;
        static_cast<IpcServerOptions&>(options) = std::move(shared);
        if (!g_environment.pipeName.empty()) options.pipeName = g_environment.pipeName;
        return std::make_unique<IpcPipeServer>(std::move(handler), std::move(options));
    };
    return host;
}

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
    g_statusHandle = RegisterServiceCtrlHandlerExW(g_environment.serviceName.c_str(), serviceControlHandler, nullptr);
    if (g_statusHandle == nullptr) {
        return;
    }

    reportStatus(SERVICE_START_PENDING, NO_ERROR, 30000);

    g_stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (g_stopEvent == nullptr) {
        reportStatus(SERVICE_STOPPED, ERROR_OUTOFMEMORY, 0);
        return;
    }

    auto started = g_runtime.start(windowsPlatformHost());
    if (!started.ok()) {
        PHPVPN_LOG_CRITICAL(kComponent, started.error().message());
        reportStatus(SERVICE_STOPPED, ERROR_SERVICE_SPECIFIC_ERROR, 0);
        return;
    }

    reportStatus(SERVICE_RUNNING, NO_ERROR, 0);
    g_runtime.requestStartupConnect();
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
    return runAsService(ServiceHostEnvironment{});
}

int runAsService(const ServiceHostEnvironment& environment) {
    g_environment = environment;
    SERVICE_TABLE_ENTRYW table[] = {{g_environment.serviceName.data(), serviceMain},
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

    auto started = g_runtime.start(windowsPlatformHost());
    if (!started.ok()) {
        std::fputs(started.error().message().c_str(), stderr);
        std::fputc('\n', stderr);
        return 1;
    }

    std::fputs("phpvpn service is running in the foreground, press Ctrl+C to stop\n", stdout);
    g_runtime.requestStartupConnect();
    WaitForSingleObject(g_stopEvent, INFINITE);

    g_runtime.stop();
    CloseHandle(g_stopEvent);
    g_stopEvent = nullptr;
    return 0;
}

static Status installScmService(const std::wstring& executablePath) {
    const std::filesystem::path path(executablePath);
    if (!path.is_absolute() || path != path.lexically_normal() ||
        executablePath.find(L'"') != std::wstring::npos || executablePath.find(L'\0') != std::wstring::npos)
        return Error(ErrorCode::invalid_argument);
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
        if (error == ERROR_SERVICE_EXISTS) {
            service = OpenServiceW(manager, kServiceName, SERVICE_QUERY_CONFIG);
            DWORD bytes = 0;
            if (service) QueryServiceConfigW(service, nullptr, 0, &bytes);
            std::vector<BYTE> buffer(bytes);
            const auto config = reinterpret_cast<QUERY_SERVICE_CONFIGW*>(buffer.data());
            const bool matches = service && bytes && QueryServiceConfigW(service, config, bytes, &bytes) &&
                config->dwServiceType == SERVICE_WIN32_OWN_PROCESS &&
                command == config->lpBinaryPathName &&
                _wcsicmp(config->lpServiceStartName, L"LocalSystem") == 0;
            if (service) CloseServiceHandle(service);
            CloseServiceHandle(manager);
            return matches ? ok() : Status(Error(ErrorCode::state_conflict,
                "the existing service registration differs; use the package upgrade flow"));
        }
        CloseServiceHandle(manager);
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

static Status uninstallScmService() {
    SC_HANDLE manager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (manager == nullptr) {
        return Error(ErrorCode::internal, "the service manager could not be opened");
    }

    SC_HANDLE service = OpenServiceW(manager, kServiceName, SERVICE_STOP | DELETE | SERVICE_QUERY_STATUS);
    if (service == nullptr) {
        const auto error = GetLastError();
        CloseServiceHandle(manager);
        if (error == ERROR_SERVICE_DOES_NOT_EXIST || error == ERROR_SERVICE_MARKED_FOR_DELETE) return ok();
        return Error(ErrorCode::internal, "the service could not be opened for removal");
    }

    SERVICE_STATUS status{};
    if (!ControlService(service, SERVICE_CONTROL_STOP, &status) && GetLastError() != ERROR_SERVICE_NOT_ACTIVE) {
        CloseServiceHandle(service);
        CloseServiceHandle(manager);
        return Error(ErrorCode::internal, "the service could not be stopped");
    }
    for (int attempt = 0; attempt < 60; ++attempt) {
        if (!QueryServiceStatus(service, &status)) {
            CloseServiceHandle(service);
            CloseServiceHandle(manager);
            return Error(ErrorCode::internal, "the service status could not be queried");
        }
        if (status.dwCurrentState == SERVICE_STOPPED) {
            break;
        }
        Sleep(500);
    }

    if (status.dwCurrentState != SERVICE_STOPPED) {
        CloseServiceHandle(service);
        CloseServiceHandle(manager);
        return Error(ErrorCode::timeout, "the service did not stop before removal");
    }

    const BOOL deleted = DeleteService(service);
    const auto deleteError = deleted ? ERROR_SUCCESS : GetLastError();
    CloseServiceHandle(service);
    CloseServiceHandle(manager);

    if (!deleted && deleteError != ERROR_SERVICE_MARKED_FOR_DELETE) {
        return Error(ErrorCode::internal, "the service could not be removed");
    }
    return Status::success();
}

ServiceRegistrationResult installService(const std::wstring& executablePath) {
    return registerServiceWithTray(executablePath, [&] { return installScmService(executablePath); },
        windowsTrayAutostartBackend());
}
ServiceRegistrationResult uninstallService() {
    return removeServiceWithTray([] { return uninstallScmService(); }, windowsTrayAutostartBackend());
}

}  // namespace phpvpn
