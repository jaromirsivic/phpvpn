// Asynchronous IPC channels of the macOS tray, the port of apps/tray-linux/ipc_async.h
// with the same API (the Configuration Wizard controller is shared logic and uses it
// unchanged). GUI-free C++: the AppKit Dispatcher lives in mac_ui.mm.
//
// Each exchange runs on a detached worker thread that owns only value captures and
// a shared state, never a channel, window or application pointer. The result is
// marshalled to the UI thread through a Dispatcher (the main run loop in the tray, a
// plain queue in the unit tests) and delivered only while the channel has not been
// abandoned. abandon() revokes delivery and signals the cancel pipe without waiting;
// stop() additionally waits for the worker. The budgets are the shared values of
// apps/tray/ipc_timeouts.h.
#pragma once
#include <sys/types.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

#include "core/base/error.h"
#include "core/base/result.h"
#include "core/ipc/ipc_protocol.h"
#include "core/ipc/wizard_ipc.h"
#include "ipc_timeouts.h"
#include "platform/posix/ipc_unix.h"
#include "save_response.h"
#include "status_model.h"

namespace phpvpn::tray {
// Posts a task to the UI thread. Must be callable from any thread.
using Dispatcher = std::function<void(std::function<void()>)>;
// The AppKit main run loop in all common modes (mac_ui.mm, linked into the tray only):
// deliveries continue while a modal alert or the status menu is open.
Dispatcher mainRunLoopDispatcher();

// CLOCK_UPTIME_RAW milliseconds (stops during sleep like the Linux CLOCK_MONOTONIC);
// the clock of SaveOperation::deadlineMs in the macOS tray.
std::int64_t monotonicMs();

struct IpcEndpoint {
    std::string socketPath;  // empty selects kIpcSocketPath
    uid_t expectedServerUid = 0;
};

struct IpcReply {
    // Empty on every failure, as the Win32 readers report it.
    std::string response;
    IpcExchangeOutcome outcome;
    ErrorCode error = ErrorCode::none;
};

struct WizardResult {
    WizardRequest request;
    Result<WizardReply> reply;
};

// One in-flight exchange per channel.
class IpcChannel {
public:
    IpcChannel(IpcEndpoint endpoint, Dispatcher dispatcher);
    ~IpcChannel();
    IpcChannel(const IpcChannel&) = delete;
    IpcChannel& operator=(const IpcChannel&) = delete;
    bool busy() const { return static_cast<bool>(state_); }
    // A complete request document; the reply is not correlated.
    bool requestRaw(std::string request, std::int64_t timeoutMs, std::function<void(IpcReply&)> done);
    // IPC v1 command with a fresh requestId. Replies of commands other than
    // get-config and get-info (which have no envelope) must carry the same
    // requestId, otherwise the response is reported empty. set-config is refused
    // here: it must use requestSave so publication uncertainty is never lost.
    bool request(IpcCommand command, std::string payload, std::function<void(std::string&)> done,
                 std::int64_t timeoutMs = kCommandTimeoutMs);
    // set-config with the absolute deadline of the operation. The classification
    // uses IpcExchangeOutcome::requestSent: a request that never left the process
    // is notSent, anything later without a proven answer is unknown.
    bool requestSave(const SaveOperation& operation, std::string payload,
                     std::function<void(const SaveResult&)> done);
    // One Wizard IPC request with the 5000 ms budget and a fresh requestId.
    bool requestWizard(WizardRequest request, std::function<void(WizardResult&)> done,
                       std::int64_t timeoutMs = kWizardRequestTimeoutMs);
    // UI thread only. Revokes delivery, signals cancellation, never waits.
    void abandon();
    // abandon() and wait until the worker has finished.
    void stop();
    struct State;

private:
    bool start(std::function<std::function<void()>(int cancelFd)> work);
    IpcEndpoint endpoint_;
    Dispatcher dispatcher_;
    std::shared_ptr<State> state_;
};

// Read-only get-status channel with the 400 ms budget. Extra requests while a read
// is in flight are queued (ReadRequestQueue) and delivered in order.
class StatusChannel {
public:
    StatusChannel(IpcEndpoint endpoint, Dispatcher dispatcher, std::function<void(const TrayStatus&)> onStatus);
    ~StatusChannel();
    StatusChannel(const StatusChannel&) = delete;
    StatusChannel& operator=(const StatusChannel&) = delete;
    void request();
    void abandon();
    void stop();
    struct State;

private:
    IpcEndpoint endpoint_;
    Dispatcher dispatcher_;
    std::function<void(const TrayStatus&)> onStatus_;
    std::shared_ptr<State> state_;
};

// Synchronous building blocks, exposed for the unit tests.
IpcReply exchangeOnce(const IpcEndpoint& endpoint, std::string_view request, std::int64_t timeoutMs,
                      int cancelFd = -1);
std::string commandResponse(const IpcEndpoint& endpoint, IpcCommand command, std::string_view payload,
                            std::int64_t timeoutMs, int cancelFd = -1);
}  // namespace phpvpn::tray
