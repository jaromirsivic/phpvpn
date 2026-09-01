// Versioned local IPC contract v1, client.md "Verejne lokalni IPC".
//
// A request is a UTF-8 JSON object with exactly `ipcVersion`, `requestId` and
// `command`, plus exactly one `config` member for `set-config`. An unknown
// version, an unknown command, a duplicate member and an extra member are all
// rejected with a short safe error and without performing the operation.
//
// The successful responses of `get-info` and `get-config` are sent without an
// IPC envelope, because they carry the verified server plaintext and the exact
// configuration document.
#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "core/base/result.h"
#include "core/runtime/status_snapshot.h"

namespace phpvpn {

inline constexpr int kIpcVersion = 1;

// Fixed implementation constant that carries the contract version. The clients
// never derive the name from user input.
inline constexpr const char* kIpcPipeName = R"(\\.\pipe\phpvpn-control-v1)";
inline constexpr wchar_t const* kIpcPipeNameW = LR"(\\.\pipe\phpvpn-control-v1)";

// Largest accepted request. A configuration document with the maximum number of
// contracts stays far below this bound.
inline constexpr std::size_t kIpcMaxMessageBytes = 4 * 1024 * 1024;

enum class IpcCommand {
    get_status,
    connect,
    disconnect,
    get_info,
    get_config,
    set_config,
    get_diagnostics,
};

const char* ipcCommandName(IpcCommand command) noexcept;
std::optional<IpcCommand> parseIpcCommand(std::string_view name) noexcept;

struct IpcRequest {
    IpcCommand command = IpcCommand::get_status;
    std::string requestId;
    // Present only for `set-config`.
    std::string config;
};

class IpcProtocol {
public:
    // Full validation of one request document.
    static Result<IpcRequest> parseRequest(std::string_view json);

    static std::string buildRequest(IpcCommand command,
                                    std::string_view requestId,
                                    std::string_view config = {});

    // {"ipcVersion":1,"requestId":"...","result":"ok"}
    static std::string buildAcknowledgement(std::string_view requestId);

    static std::string buildStatusResponse(std::string_view requestId,
                                           const StatusSnapshot& snapshot);

    static std::string buildDiagnosticsResponse(std::string_view requestId,
                                                const DiagnosticsSnapshot& snapshot);

    // {"ipcVersion":1,"requestId":"...","error":{"code":"...","description":"...","recoverable":false}}
    static std::string buildError(std::string_view requestId, const Error& error);
};

}  // namespace phpvpn
