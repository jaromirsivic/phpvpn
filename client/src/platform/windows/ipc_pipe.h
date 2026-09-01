// Named pipe transport for the local IPC contract.
//
// The pipe carries one request and one response per connection. Its security
// descriptor grants generic read and write to Everyone, because the contract is
// deliberately reachable by every local application. The service never
// impersonates the caller and never decides anything from the caller identity.
#pragma once

#include <atomic>
#include <functional>
#include <string>
#include <thread>
#include <vector>

#include "core/base/result.h"

namespace phpvpn {

// Receives the exact request bytes and returns the exact response bytes.
using IpcHandler = std::function<std::string(std::string_view)>;

class IpcPipeServer {
public:
    explicit IpcPipeServer(IpcHandler handler);
    ~IpcPipeServer();

    IpcPipeServer(const IpcPipeServer&) = delete;
    IpcPipeServer& operator=(const IpcPipeServer&) = delete;

    Status start();
    void stop();

private:
    void run();

    IpcHandler handler_;
    std::atomic<bool> running_{false};
    std::thread thread_;
    void* stopEvent_ = nullptr;
};

// Single request client used by the tray and by the console application.
Result<std::string> sendIpcRequest(std::string_view requestJson, std::int64_t timeoutMs = 10000);

}  // namespace phpvpn
