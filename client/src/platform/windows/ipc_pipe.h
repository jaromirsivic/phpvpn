// Bounded asynchronous local named-pipe transport. One request/response per connection.
#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include "core/base/result.h"
#include "platform/iface/ipc_server.h"

namespace phpvpn {
struct IpcPipeServerOptions : IpcServerOptions {
    // Empty selects the fixed production pipe. Private names are for isolated tests.
    std::wstring pipeName;
};
using IpcPipeServerStats = IpcServerStats;
class IpcPipeServer final : public IpcServer {
public:
    explicit IpcPipeServer(IpcHandler handler);
    IpcPipeServer(IpcHandler handler, IpcPipeServerOptions options);
    ~IpcPipeServer() override;
    IpcPipeServer(const IpcPipeServer&) = delete;
    IpcPipeServer& operator=(const IpcPipeServer&) = delete;
    Status start() override;
    void stop() override;
    // Queue limits are four waiting file tasks and one diagnostic total.
    // False means the task was not accepted and will never be run.
    // Call after start succeeds. A task must catch its own failures and publish
    // its operation's terminal result, including during shutdown draining.
    bool tryPostFile(std::function<void()> task) override;
    bool tryPostDiagnostic(std::function<void()> task) override;
    IpcPipeServerStats stats() const override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
Result<std::string> sendIpcRequest(std::string_view requestJson, std::int64_t timeoutMs = 10000);
// Explicit test/embedded endpoint. Production tray/CLI retain sendIpcRequest.
Result<std::string> sendIpcRequestTo(std::wstring_view pipeName, std::string_view requestJson,
                                   std::int64_t timeoutMs = 10000);
}  // namespace phpvpn
