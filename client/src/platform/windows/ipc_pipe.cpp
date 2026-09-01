#include "platform/windows/ipc_pipe.h"

#include <windows.h>

#include <sddl.h>

#include <vector>

#include "core/base/logger.h"
#include "core/ipc/ipc_protocol.h"

#pragma comment(lib, "advapi32.lib")

namespace phpvpn {
namespace {

constexpr const char* kComponent = "ipc-pipe";
constexpr DWORD kPipeBufferBytes = 64 * 1024;

// Message mode keeps one request in one message, so no framing of its own is
// needed and a partial message can never be mistaken for a complete one.
constexpr DWORD kPipeMode = PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT;

// Every local application may connect, regardless of its user, group or token.
// The owner is the local system account and the descriptor grants generic read
// and write to Everyone, which is exactly the accepted risk of client.md.
constexpr const wchar_t* kPipeSecurityDescriptor = L"D:(A;;GRGW;;;WD)(A;;FA;;;SY)(A;;FA;;;BA)";

class SecurityAttributes {
public:
    bool build() {
        if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
                kPipeSecurityDescriptor, SDDL_REVISION_1, &descriptor_, nullptr)) {
            return false;
        }
        attributes_.nLength = sizeof(attributes_);
        attributes_.lpSecurityDescriptor = descriptor_;
        attributes_.bInheritHandle = FALSE;
        return true;
    }

    ~SecurityAttributes() {
        if (descriptor_ != nullptr) {
            LocalFree(descriptor_);
        }
    }

    SECURITY_ATTRIBUTES* get() noexcept { return &attributes_; }

private:
    PSECURITY_DESCRIPTOR descriptor_ = nullptr;
    SECURITY_ATTRIBUTES attributes_{};
};

// Reads one complete message, refusing anything above the contract limit.
Result<std::string> readMessage(HANDLE pipe, OVERLAPPED& overlapped, HANDLE stopEvent) {
    std::string message;
    std::vector<char> chunk(kPipeBufferBytes);

    for (;;) {
        DWORD read = 0;
        ResetEvent(overlapped.hEvent);
        const BOOL ok = ReadFile(pipe, chunk.data(), static_cast<DWORD>(chunk.size()), &read,
                                 &overlapped);
        DWORD error = ok ? ERROR_SUCCESS : GetLastError();

        if (!ok && error == ERROR_IO_PENDING) {
            HANDLE waitHandles[2] = {overlapped.hEvent, stopEvent};
            const DWORD waited = WaitForMultipleObjects(stopEvent != nullptr ? 2 : 1, waitHandles,
                                                        FALSE, INFINITE);
            if (waited != WAIT_OBJECT_0) {
                CancelIo(pipe);
                return Error(ErrorCode::cancelled, "the request was interrupted");
            }
            if (!GetOverlappedResult(pipe, &overlapped, &read, FALSE)) {
                error = GetLastError();
            } else {
                error = ERROR_SUCCESS;
            }
        }

        if (error == ERROR_SUCCESS || error == ERROR_MORE_DATA) {
            if (message.size() + read > kIpcMaxMessageBytes) {
                return Error(ErrorCode::ipc_message_too_large,
                             "the request exceeds the accepted size");
            }
            message.append(chunk.data(), read);
            if (error == ERROR_SUCCESS) {
                return message;
            }
            continue;
        }

        if (error == ERROR_BROKEN_PIPE || error == ERROR_PIPE_NOT_CONNECTED) {
            return Error(ErrorCode::ipc_unavailable, "the local client disconnected");
        }
        return Error(ErrorCode::ipc_unavailable, "the request could not be read");
    }
}

Status writeMessage(HANDLE pipe, OVERLAPPED& overlapped, std::string_view message) {
    DWORD written = 0;
    ResetEvent(overlapped.hEvent);
    const BOOL ok = WriteFile(pipe, message.data(), static_cast<DWORD>(message.size()), &written,
                              &overlapped);
    if (!ok) {
        if (GetLastError() != ERROR_IO_PENDING) {
            return Error(ErrorCode::ipc_unavailable, "the response could not be sent");
        }
        if (WaitForSingleObject(overlapped.hEvent, INFINITE) != WAIT_OBJECT_0 ||
            !GetOverlappedResult(pipe, &overlapped, &written, FALSE)) {
            return Error(ErrorCode::ipc_unavailable, "the response could not be sent");
        }
    }
    if (written != message.size()) {
        return Error(ErrorCode::ipc_unavailable, "the response was truncated");
    }
    return Status::success();
}

}  // namespace

IpcPipeServer::IpcPipeServer(IpcHandler handler) : handler_(std::move(handler)) {}

IpcPipeServer::~IpcPipeServer() {
    stop();
    if (stopEvent_ != nullptr) {
        CloseHandle(static_cast<HANDLE>(stopEvent_));
        stopEvent_ = nullptr;
    }
}

Status IpcPipeServer::start() {
    if (running_.exchange(true)) {
        return Error(ErrorCode::service_already_running, "the IPC server is already running");
    }

    stopEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (stopEvent_ == nullptr) {
        running_.store(false);
        return Error(ErrorCode::ipc_unavailable, "the IPC stop event could not be created");
    }

    thread_ = std::thread([this] { run(); });
    return Status::success();
}

void IpcPipeServer::stop() {
    if (!running_.exchange(false)) {
        if (thread_.joinable()) {
            thread_.join();
        }
        return;
    }
    if (stopEvent_ != nullptr) {
        SetEvent(static_cast<HANDLE>(stopEvent_));
    }
    if (thread_.joinable()) {
        thread_.join();
    }
}

void IpcPipeServer::run() {
    SecurityAttributes security;
    if (!security.build()) {
        PHPVPN_LOG_ERROR(kComponent, "the pipe access control list could not be built");
        running_.store(false);
        return;
    }

    HANDLE stopEvent = static_cast<HANDLE>(stopEvent_);

    while (running_.load()) {
        HANDLE pipe = CreateNamedPipeW(kIpcPipeNameW,
                                       PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED, kPipeMode,
                                       PIPE_UNLIMITED_INSTANCES, kPipeBufferBytes,
                                       kPipeBufferBytes, 0, security.get());
        if (pipe == INVALID_HANDLE_VALUE) {
            PHPVPN_LOG_ERROR(kComponent, "the control pipe could not be created");
            break;
        }

        OVERLAPPED overlapped{};
        overlapped.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (overlapped.hEvent == nullptr) {
            CloseHandle(pipe);
            break;
        }

        bool connected = ConnectNamedPipe(pipe, &overlapped) != 0;
        if (!connected) {
            const DWORD error = GetLastError();
            if (error == ERROR_PIPE_CONNECTED) {
                connected = true;
            } else if (error == ERROR_IO_PENDING) {
                HANDLE waitHandles[2] = {overlapped.hEvent, stopEvent};
                const DWORD waited = WaitForMultipleObjects(2, waitHandles, FALSE, INFINITE);
                if (waited == WAIT_OBJECT_0) {
                    DWORD transferred = 0;
                    connected = GetOverlappedResult(pipe, &overlapped, &transferred, FALSE) != 0;
                } else {
                    CancelIo(pipe);
                }
            }
        }

        if (connected && running_.load()) {
            auto request = readMessage(pipe, overlapped, stopEvent);
            if (request.ok()) {
                const std::string response = handler_(request.value());
                auto written = writeMessage(pipe, overlapped, response);
                if (!written.ok()) {
                    PHPVPN_LOG_WARN(kComponent, "a response could not be delivered");
                }
                FlushFileBuffers(pipe);
            }
        }

        DisconnectNamedPipe(pipe);
        CloseHandle(overlapped.hEvent);
        CloseHandle(pipe);
    }

    running_.store(false);
}

Result<std::string> sendIpcRequest(std::string_view requestJson, std::int64_t timeoutMs) {
    if (requestJson.size() > kIpcMaxMessageBytes) {
        return Error(ErrorCode::ipc_message_too_large, "the request exceeds the accepted size");
    }

    HANDLE pipe = INVALID_HANDLE_VALUE;
    for (;;) {
        pipe = CreateFileW(kIpcPipeNameW, GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                           0, nullptr);
        if (pipe != INVALID_HANDLE_VALUE) {
            break;
        }
        if (GetLastError() != ERROR_PIPE_BUSY) {
            return Error(ErrorCode::ipc_unavailable, "the service is not reachable");
        }
        if (!WaitNamedPipeW(kIpcPipeNameW, static_cast<DWORD>(timeoutMs))) {
            return Error(ErrorCode::timeout, "the service did not accept the connection in time");
        }
    }

    DWORD mode = PIPE_READMODE_MESSAGE;
    SetNamedPipeHandleState(pipe, &mode, nullptr, nullptr);

    DWORD written = 0;
    if (!WriteFile(pipe, requestJson.data(), static_cast<DWORD>(requestJson.size()), &written,
                   nullptr) ||
        written != requestJson.size()) {
        CloseHandle(pipe);
        return Error(ErrorCode::ipc_unavailable, "the request could not be sent");
    }

    std::string response;
    std::vector<char> chunk(kPipeBufferBytes);
    for (;;) {
        DWORD read = 0;
        const BOOL ok = ReadFile(pipe, chunk.data(), static_cast<DWORD>(chunk.size()), &read,
                                 nullptr);
        const DWORD error = ok ? ERROR_SUCCESS : GetLastError();
        if (error == ERROR_SUCCESS || error == ERROR_MORE_DATA) {
            if (response.size() + read > kIpcMaxMessageBytes) {
                CloseHandle(pipe);
                return Error(ErrorCode::ipc_message_too_large,
                             "the response exceeds the accepted size");
            }
            response.append(chunk.data(), read);
            if (error == ERROR_SUCCESS) {
                break;
            }
            continue;
        }
        CloseHandle(pipe);
        if (error == ERROR_BROKEN_PIPE) {
            return Error(ErrorCode::ipc_unavailable, "the service closed the connection");
        }
        return Error(ErrorCode::ipc_unavailable, "the response could not be read");
    }

    CloseHandle(pipe);
    return response;
}

}  // namespace phpvpn
