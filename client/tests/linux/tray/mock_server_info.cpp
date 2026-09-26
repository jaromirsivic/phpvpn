// DEVELOPMENT HARNESS ONLY. Server information and Statistics extension of
// phpvpn-tray-mock-service (WP10c), registered through mock_extension.h.
//
//   get-server-info  one correlated result per request with a fresh realistic
//                    snapshot (server_info_fixture.h: pretty JSON, masked contracts,
//                    valid / invalid / offline peers), classified as diagnostic work
//                    like the service (one outstanding, busy otherwise).
//   get-status       optionally carries the large uint64 counters of client.md item 34,
//                    or fails so the tray reports the status as unavailable.
//
// Options: --info-rows <0..252>  --info-delay-ms <n>  --counters normal|large
// Stdin:   "info rows <n>", "info ok", "info error", "info malformed",
//          "info delay <ms>", "info crlf", "info lf",
//          "counters large", "counters normal", "status fail", "status ok"
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <thread>

#include "core/presentation/server_info_model.h"
#include "mock_extension.h"
#include "server_info_fixture.h"

namespace {
using namespace phpvpn;

struct InfoState {
    std::mutex mutex;
    std::size_t rows = 6;
    std::string mode = "ok";  // ok | error | malformed
    int delayMs = 0;
    bool crlf = false;
    bool largeCounters = false;
    bool statusFails = false;
};
InfoState& state() {
    static InfoState value;
    return value;
}

std::int64_t nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch()).count();
}

std::string serverInfo(const IpcRequest& request) {
    std::size_t rows = 0;
    std::string mode;
    int delay = 0;
    bool crlf = false;
    {
        std::lock_guard lock(state().mutex);
        rows = state().rows;
        mode = state().mode;
        delay = state().delayMs;
        crlf = state().crlf;
    }
    // The diagnostic executor is busy for the whole request, as with a slow server.
    if (delay > 0) std::this_thread::sleep_for(std::chrono::milliseconds(delay));
    if (mode == "error")
        return IpcProtocol::buildError(request.requestId, Error(ErrorCode::transport_timeout));
    if (mode == "malformed") return "{\"ipcVersion\":1,\"requestId\":\"" + request.requestId + "\",\"result\":{}}";
    fixture::ServerInfoOptions options;
    options.peers = rows;
    options.generatedAtMs = nowMs();
    options.crlf = crlf;
    return IpcProtocol::buildServerInfoResponse(request.requestId, fixture::serverInfoSnapshot(options));
}

bool control(const std::string& line) {
    std::lock_guard lock(state().mutex);
    auto& s = state();
    if (line.rfind("info rows ", 0) == 0) s.rows = std::min<std::size_t>(252, std::strtoul(line.c_str() + 10, nullptr, 10));
    else if (line.rfind("info delay ", 0) == 0) s.delayMs = std::max(0, std::atoi(line.c_str() + 11));
    else if (line == "info ok" || line == "info error" || line == "info malformed") s.mode = line.substr(5);
    else if (line == "info crlf" || line == "info lf") s.crlf = line == "info crlf";
    else if (line == "counters large" || line == "counters normal") s.largeCounters = line == "counters large";
    else if (line == "status fail" || line == "status ok") s.statusFails = line == "status fail";
    else return false;
    std::cout << "control " << line << std::endl;
    return true;
}

bool option(const std::string& name, const std::string& value) {
    std::lock_guard lock(state().mutex);
    if (name == "--info-rows") state().rows = std::min<std::size_t>(252, std::strtoul(value.c_str(), nullptr, 10));
    else if (name == "--info-delay-ms") state().delayMs = std::max(0, std::atoi(value.c_str()));
    else if (name == "--counters") state().largeCounters = value == "large";
    else return false;
    return true;
}

const mock::MockExtensionRegistrar registrar(mock::MockExtension{
    [](const IpcRequest& request) -> std::optional<std::string> {
        if (request.command == IpcCommand::get_server_info) return serverInfo(request);
        if (request.command == IpcCommand::get_status) {
            std::lock_guard lock(state().mutex);
            if (state().statusFails) return IpcProtocol::buildError(request.requestId, Error(ErrorCode::internal));
        }
        return std::nullopt;
    },
    [](StatusSnapshot& snapshot) {
        std::lock_guard lock(state().mutex);
        if (state().largeCounters) snapshot.counters = fixture::largeCounters();
    },
    [](std::string_view request) -> std::optional<IpcWorkClass> {
        auto parsed = IpcProtocol::parseRequest(request);
        if (parsed && (parsed->command == IpcCommand::get_server_info || parsed->command == IpcCommand::get_info))
            return IpcWorkClass::diagnostic;
        return std::nullopt;
    },
    control,
    option,
});
}  // namespace
