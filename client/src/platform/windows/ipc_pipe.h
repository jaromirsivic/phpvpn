// Bounded asynchronous local named-pipe transport. One request/response per connection.
#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include "core/base/result.h"

namespace phpvpn {
using IpcHandler = std::function<std::string(std::string_view)>;
enum class IpcWorkClass { shortControl, file, diagnostic };
struct IpcPipeServerOptions {
    // Empty selects the fixed production pipe. Private names are for isolated tests.
    std::wstring pipeName;
    // Pure, bounded classification after the complete message has arrived.
    // The service classifier/handler must validate the complete IPC schema before effects.
    std::function<IpcWorkClass(std::string_view)> classify;
    // Produces the standard correlated busy response. Empty means reject by closing.
    std::function<std::string(std::string_view)> busyResponse;
    // Called once before joining workers. Must cancel/wake long handlers/tasks.
    // Accepted tasks are drained, never silently dropped. They must return promptly
    // after this callback, including tasks that start during shutdown draining.
    std::function<void()> cancelHandlers;
    // Event-loop maintenance every 250 ms or sooner. Must not block or perform I/O.
    std::function<void()> onTick;
};
struct IpcPipeServerStats {
    std::size_t connectionSlots = 0; // Includes listeners, hard maximum 32.
    std::size_t shortQueued = 0;
    std::size_t fileQueued = 0;
    std::size_t diagnosticOutstanding = 0;
    std::uint64_t rejected = 0;
    std::uint64_t ioTimeouts = 0;
};
class IpcPipeServer {
public:
    explicit IpcPipeServer(IpcHandler handler);
    IpcPipeServer(IpcHandler handler, IpcPipeServerOptions options);
    ~IpcPipeServer();
    IpcPipeServer(const IpcPipeServer&) = delete;
    IpcPipeServer& operator=(const IpcPipeServer&) = delete;
    Status start();
    void stop();
    // Queue limits are four waiting file tasks and one diagnostic total.
    // False means the task was not accepted and will never be run.
    // Call after start succeeds. A task must catch its own failures and publish
    // its operation's terminal result, including during shutdown draining.
    bool tryPostFile(std::function<void()> task);
    bool tryPostDiagnostic(std::function<void()> task);
    IpcPipeServerStats stats() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
Result<std::string> sendIpcRequest(std::string_view requestJson, std::int64_t timeoutMs = 10000);
// Explicit test/embedded endpoint. Production tray/CLI retain sendIpcRequest.
Result<std::string> sendIpcRequestTo(std::wstring_view pipeName, std::string_view requestJson,
                                   std::int64_t timeoutMs = 10000);
}  // namespace phpvpn
