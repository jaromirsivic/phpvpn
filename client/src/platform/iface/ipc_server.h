// Platform independent shape of the bounded local IPC server, client.md
// "Verejne lokalni IPC". The Windows named-pipe server and the Linux Unix-socket
// server share these types and the work-class executors (ipc_executors.h).
// One request/response per connection, messages of at most kIpcMaxMessageBytes.
#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include "core/base/result.h"

namespace phpvpn {
using IpcHandler = std::function<std::string(std::string_view)>;
enum class IpcWorkClass { shortControl, file, diagnostic };
// Hooks common to every transport. Transport options derive from this struct.
struct IpcServerOptions {
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
struct IpcServerStats {
    std::size_t connectionSlots = 0; // Includes listeners, hard maximum 32.
    std::size_t shortQueued = 0;
    std::size_t fileQueued = 0;
    std::size_t diagnosticOutstanding = 0;
    std::uint64_t rejected = 0;
    std::uint64_t ioTimeouts = 0;
};
// Common lifetime of the platform servers, so a portable service composition can
// own either one.
class IpcServer {
public:
    virtual ~IpcServer() = default;
    virtual Status start() = 0;
    virtual void stop() = 0;
    // Queue limits are four waiting file tasks and one diagnostic total.
    // False means the task was not accepted and will never be run.
    // Call after start succeeds. A task must catch its own failures and publish
    // its operation's terminal result, including during shutdown draining.
    virtual bool tryPostFile(std::function<void()> task) = 0;
    virtual bool tryPostDiagnostic(std::function<void()> task) = 0;
    virtual IpcServerStats stats() const = 0;
};
}  // namespace phpvpn
