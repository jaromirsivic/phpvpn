#include "core/ipc/ipc_dispatcher.h"

#include "core/base/logger.h"

namespace phpvpn {
namespace {

constexpr const char* kComponent = "ipc";

}  // namespace

std::string IpcDispatcher::handle(std::string_view requestJson) {
    auto parsed = IpcProtocol::parseRequest(requestJson);
    if (!parsed.ok()) {
        // A request that fails validation is answered without performing any
        // operation, and without an identifier it cannot echo back.
        PHPVPN_LOG_WARN(kComponent, "a malformed request was rejected");
        return IpcProtocol::buildError(std::string{}, parsed.error());
    }

    const IpcRequest& request = parsed.value();

    switch (request.command) {
        case IpcCommand::get_status:
            return IpcProtocol::buildStatusResponse(request.requestId, engine_.status());

        case IpcCommand::get_diagnostics:
            return IpcProtocol::buildDiagnosticsResponse(request.requestId, engine_.diagnostics());

        case IpcCommand::connect: {
            auto result = engine_.connect();
            if (!result.ok()) {
                return IpcProtocol::buildError(request.requestId, result.error());
            }
            return IpcProtocol::buildStatusResponse(request.requestId, engine_.status());
        }

        case IpcCommand::disconnect: {
            auto result = engine_.disconnect();
            if (!result.ok()) {
                return IpcProtocol::buildError(request.requestId, result.error());
            }
            return IpcProtocol::buildStatusResponse(request.requestId, engine_.status());
        }

        case IpcCommand::get_config: {
            auto config = engine_.storedConfigJson();
            if (!config.ok()) {
                return IpcProtocol::buildError(request.requestId, config.error());
            }
            // Sent without an IPC envelope, as the contract requires.
            return std::move(config).value();
        }

        case IpcCommand::set_config: {
            auto result = engine_.setConfigJson(request.config);
            if (!result.ok()) {
                return IpcProtocol::buildError(request.requestId, result.error());
            }
            return IpcProtocol::buildAcknowledgement(request.requestId);
        }

        case IpcCommand::get_info: {
            auto info = engine_.fetchInfoOnce();
            if (!info.ok()) {
                return IpcProtocol::buildError(request.requestId, info.error());
            }
            // The verified server plaintext is forwarded verbatim, without an
            // IPC envelope and without any part of the encrypted wrapper.
            return std::move(info).value();
        }
    }

    return IpcProtocol::buildError(request.requestId,
                                   Error(ErrorCode::ipc_unknown_command,
                                         "the command is not part of this contract"));
}

}  // namespace phpvpn
