// Bounded local Unix-socket IPC transport, client.md "Verejne lokalni IPC".
//
// SOCK_STREAM, one request/response per connection. Each message is framed as a
// u32 big-endian payload length followed by the UTF-8 JSON payload of at most
// kIpcMaxMessageBytes. The explicit length separates a complete response from a
// truncated one.
//
// Access model: the socket is mode 0666 in a directory owned by the service
// user (root in production) that nobody else can write. The server does not
// authorize clients (public IPC). The client verifies with SO_PEERCRED that the
// peer runs as the expected user before sending anything.
#pragma once
#include <sys/types.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include "core/base/result.h"
#include "platform/iface/ipc_server.h"

namespace phpvpn {
struct IpcSocketServerOptions : IpcServerOptions {
    // Empty selects the fixed production socket kIpcSocketPath. Private paths are
    // for isolated tests. Absolute, shorter than sockaddr_un::sun_path.
    std::string socketPath;
    // Required owner of the socket directory and of a stale socket that may be
    // replaced. Root in production; tests inject their own uid.
    uid_t expectedOwnerUid = 0;
    // Read deadline from accept to the complete request, and write deadline from
    // the first response byte to the client closing. Tests may shorten it.
    std::int64_t ioDeadlineMs = 5000;
};
// Uses the shared work-class executors of ipc_executors.h. The server does not
// create the socket directory; it must exist, be owned by expectedOwnerUid and
// not be writable by group or others. The caller serializes instances (the
// service holds its singleton lock), so an existing socket owned by
// expectedOwnerUid in that directory is treated as stale and replaced.
class IpcSocketServer final : public IpcServer {
public:
    explicit IpcSocketServer(IpcHandler handler);
    IpcSocketServer(IpcHandler handler, IpcSocketServerOptions options);
    ~IpcSocketServer() override;
    IpcSocketServer(const IpcSocketServer&) = delete;
    IpcSocketServer& operator=(const IpcSocketServer&) = delete;
    Status start() override;
    void stop() override;
    bool tryPostFile(std::function<void()> task) override;
    bool tryPostDiagnostic(std::function<void()> task) override;
    IpcServerStats stats() const override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Why an exchange failed, for callers that must tell the cases apart (for example
// the save classification needs to know whether the request left the process).
enum class IpcSocketFailure {
    none,
    invalidArgument,   // invalid_argument
    requestTooLarge,   // ipc_message_too_large, nothing sent
    serviceNotRunning, // ipc_unavailable: no socket (ENOENT) or nobody listening (ECONNREFUSED)
    accessDenied,      // ipc_unavailable: EACCES/EPERM on the socket
    invalidPeer,       // ipc_unavailable: peer uid is not the expected service uid, nothing sent
    timeout,           // timeout: the deadline expired (also while all server slots are busy)
    cancelled,         // cancelled: the cancel descriptor became readable
    connectionClosed,  // ipc_unavailable: closed or failed before any response byte
    truncatedResponse, // ipc_malformed: closed inside the response frame
    responseTooLarge,  // ipc_message_too_large: announced length over the limit
    invalidResponse,   // ipc_malformed: zero length frame
};
struct IpcExchangeOutcome {
    IpcSocketFailure failure = IpcSocketFailure::none;
    // True once the complete request frame was written to the service.
    bool requestSent = false;
};
struct IpcSocketClientOptions {
    // Empty selects kIpcSocketPath.
    std::string socketPath;
    // Required SO_PEERCRED uid of the server. Root in production.
    uid_t expectedServerUid = 0;
};
class IpcSocketClient {
public:
    IpcSocketClient() = default;
    explicit IpcSocketClient(IpcSocketClientOptions options) : options_(std::move(options)) {}
    // Sends one request and reads its response within timeoutMs overall. The
    // request is sent only after the peer credentials were verified. cancelFd,
    // when not negative, cancels the exchange as soon as it becomes readable
    // (for example an eventfd). outcome, when given, receives the failure detail.
    Result<std::string> exchange(std::string_view request, std::int64_t timeoutMs,
                                 int cancelFd = -1, IpcExchangeOutcome* outcome = nullptr) const;

private:
    IpcSocketClientOptions options_;
};

// Production endpoint with root peer verification, as the Windows sendIpcRequest.
Result<std::string> sendIpcRequest(std::string_view requestJson, std::int64_t timeoutMs = 10000);
}  // namespace phpvpn
