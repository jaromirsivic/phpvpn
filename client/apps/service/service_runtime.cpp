#include "service_runtime.h"

#include <string>
#include <utility>

#include "core/base/logger.h"
#include "core/config/config_store.h"
#include "core/crypto/crypto_provider.h"
#include "core/ipc/ipc_dispatcher.h"
#include "core/ipc/ipc_protocol.h"
#include "core/ipc/wizard_ipc.h"
#include "core/runtime/vpn_engine.h"
#include "core/wizard/wizard_service.h"
#include "platform/iface/clock.h"
#include "platform/iface/netconfig.h"
#include "platform/iface/transport.h"
#include "platform/iface/tunnel.h"

namespace phpvpn {
namespace {
constexpr const char* kComponent = "service";
}  // namespace

IpcWorkClass classifyServiceIpcRequest(std::string_view json) {
    auto wizard = WizardIpc::parseRequest(json);
    if (wizard) {
        switch (wizard->command) {
            case WizardCommand::open: case WizardCommand::get_config_state:
            case WizardCommand::get_config_document: case WizardCommand::create_default_config:
                return IpcWorkClass::file;
            default: return IpcWorkClass::shortControl;
        }
    }
    auto request = IpcProtocol::parseRequest(json);
    if (!request) return IpcWorkClass::shortControl;
    if (request->command == IpcCommand::get_info || request->command == IpcCommand::get_server_info) return IpcWorkClass::diagnostic;
    if (request->command == IpcCommand::connect || request->command == IpcCommand::get_config || request->command == IpcCommand::set_config)
        return IpcWorkClass::file;
    return IpcWorkClass::shortControl;
}

std::string serviceIpcBusyResponse(std::string_view json) {
    auto wizard = WizardIpc::parseRequest(json);
    if (wizard) return WizardIpc::buildError(wizard->requestId, WizardIpc::safeError(Error(ErrorCode::busy)));
    auto request = IpcProtocol::parseRequest(json);
    return IpcProtocol::buildError(request ? request->requestId : std::string{}, Error(ErrorCode::busy));
}

ServiceRuntime::ServiceRuntime() = default;
ServiceRuntime::~ServiceRuntime() = default;

Status ServiceRuntime::start(ServicePlatformHost host) {
    host_ = std::move(host);

    ServiceStorage storage;
    PHPVPN_TRY_ASSIGN(storage, host_.prepareStorage());
    PHPVPN_LOG_INFO(kComponent, "the service is starting");

    std::wstring installDirectory;
    PHPVPN_TRY_ASSIGN(installDirectory, host_.installDirectory());

    PHPVPN_TRY_ASSIGN(crypto_, createPlatformCryptoProvider());
    PHPVPN_TRY_ASSIGN(transport_, createPlatformTransport());
    clock_ = createPlatformClock();
    tunnelDriver_ = createPlatformTunnelDriver();
    netConfig_ = createPlatformNetConfig();

    EnginePaths paths;
    paths.configFile = storage.configFile;
    paths.journalFile = storage.journalFile;
    paths.installDirectory = std::move(installDirectory);
    paths.adapterName = host_.adapterName;
    paths.adapterGuid = host_.adapterGuid;

    configStore_ = storage.storageError ? std::make_unique<ConfigStore>(*crypto_, storage.configFile, *storage.storageError) :
        std::make_unique<ConfigStore>(*crypto_, std::move(storage.files));
    auto recoveredConfig = configStore_->recover();
    if (!recoveredConfig) PHPVPN_LOG_ERROR(kComponent, std::string("config recovery: ") + recoveredConfig.error().name());
    auto initialConfig = configStore_->ensureDefaultAtStartup();
    if (!initialConfig) PHPVPN_LOG_ERROR(kComponent, std::string("config startup: ") + initialConfig.error().name());
    else PHPVPN_LOG_INFO(kComponent, initialConfig.value() == StartupConfigState::created ? "config startup: default created" :
        initialConfig.value() == StartupConfigState::alreadyExists ? "config startup: concurrent file preserved" : "config startup: existing file preserved");
    engine_ = std::make_unique<VpnEngine>(*crypto_, *transport_, *clock_, *tunnelDriver_,
                                          *netConfig_, std::move(paths), *configStore_);
    auto engineStarted = engine_->start();
    if (!engineStarted) PHPVPN_LOG_ERROR(kComponent, std::string("engine initialization: ") + engineStarted.error().name());
    startupConnect_ = std::make_unique<StartupConnect>(*configStore_, *engine_);

    WizardScheduler scheduler;
    scheduler.file = [this](std::function<void()> task) { return server_ && server_->tryPostFile(std::move(task)); };
    scheduler.diagnostic = [this](std::function<void()> task) { return server_ && server_->tryPostDiagnostic(std::move(task)); };
    std::string instanceId;
    PHPVPN_TRY_ASSIGN(instanceId, crypto_->randomUuidV4());
    wizard_ = std::make_unique<WizardService>(*crypto_, *clock_, *configStore_, *engine_, std::move(scheduler), std::move(instanceId));
    dispatcher_ = std::make_unique<IpcDispatcher>(*engine_, wizard_.get());
    IpcServerOptions options;
    options.classify = classifyServiceIpcRequest;
    options.busyResponse = serviceIpcBusyResponse;
    options.onTick = [this] { wizard_->tick(); startupConnect_->tick(filePost()); };
    options.cancelHandlers = [this] { startupConnect_->cancel(); wizard_->shutdown(); engine_->shutdown(); };
    server_ = host_.createIpcServer(
        [this](std::string_view request) { return dispatcher_->handle(request); }, std::move(options));
    if (!server_) return Error(ErrorCode::internal, "the control IPC server could not be created");
    PHPVPN_TRY(server_->start());

    PHPVPN_LOG_INFO(kComponent, "the service control IPC is ready");
    return Status::success();
}

StartupConnect::Post ServiceRuntime::filePost() {
    return [this](std::function<void()> task) { return server_ && server_->tryPostFile(std::move(task)); };
}

void ServiceRuntime::requestStartupConnect() {
    if (startupConnect_) startupConnect_->request(filePost());
}

void ServiceRuntime::stop() {
    PHPVPN_LOG_INFO(kComponent, "the service is stopping");
    if (startupConnect_) startupConnect_->cancel();
    if (server_) {
        server_->stop();
        server_.reset();
    }
    dispatcher_.reset();
    startupConnect_.reset();
    wizard_.reset();
    if (engine_) {
        engine_->shutdown();
        engine_.reset();
    }
    netConfig_.reset();
    tunnelDriver_.reset();
    transport_.reset();
    clock_.reset();
    configStore_.reset();
    crypto_.reset();
    Logger::instance().flush();
}

std::optional<StatusSnapshot> ServiceRuntime::status() const {
    if (!engine_) return std::nullopt;
    return engine_->status();
}

}  // namespace phpvpn
