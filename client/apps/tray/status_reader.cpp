#include "status_reader.h"
#include <array>
#include "core/crypto/crypto_provider.h"
#include "core/ipc/ipc_protocol.h"
#include "core/protocol/json.h"

namespace phpvpn::tray {
namespace {
// The general synchronous IPC helper can wait on a stalled server indefinitely.
// Status reads instead use overlapped I/O and one 400 ms total deadline so the
// independent 500 ms timer and closing any window stay responsive.
std::string readResponse(IpcCommand command, const std::string& payload, DWORD timeoutMs, HANDLE cancel = nullptr) {
    auto crypto = createPlatformCryptoProvider();
    if (!crypto.ok()) return {};
    const auto uuid = crypto.value()->randomUuidV4();
    if (!uuid.ok()) return {};
    const auto request = IpcProtocol::buildRequest(command, uuid.value(), payload);
    const ULONGLONG deadline = GetTickCount64() + timeoutMs;
    HANDLE pipe = CreateFileW(kIpcPipeNameW, GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                              OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
    if (pipe == INVALID_HANDLE_VALUE && GetLastError() == ERROR_PIPE_BUSY) {
        WaitNamedPipeW(kIpcPipeNameW, 100);
        pipe = CreateFileW(kIpcPipeNameW, GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                           OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
    }
    if (pipe == INVALID_HANDLE_VALUE) return {};
    DWORD mode = PIPE_READMODE_MESSAGE;
    SetNamedPipeHandleState(pipe, &mode, nullptr, nullptr);
    OVERLAPPED operation{};
    operation.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!operation.hEvent) { CloseHandle(pipe); return {}; }
    auto finish = [&](BOOL started, DWORD& transferred) -> DWORD {
        DWORD error = started ? ERROR_SUCCESS : GetLastError();
        if (error != ERROR_IO_PENDING) return error;
        const ULONGLONG now = GetTickCount64();
        const DWORD remaining = now < deadline ? static_cast<DWORD>(deadline - now) : 0;
        const HANDLE events[]{operation.hEvent, cancel};
        if (WaitForMultipleObjects(cancel ? 2 : 1, events, FALSE, remaining) != WAIT_OBJECT_0) {
            CancelIoEx(pipe, &operation);
            GetOverlappedResult(pipe, &operation, &transferred, TRUE);
            return ERROR_TIMEOUT;
        }
        return GetOverlappedResult(pipe, &operation, &transferred, FALSE) ? ERROR_SUCCESS : GetLastError();
    };
    DWORD transferred = 0;
    DWORD error = finish(WriteFile(pipe, request.data(), static_cast<DWORD>(request.size()),
                                    &transferred, &operation), transferred);
    std::string response;
    if (error == ERROR_SUCCESS && transferred == request.size()) {
        std::array<char, 8192> chunk{};
        do {
            ResetEvent(operation.hEvent);
            transferred = 0;
            error = finish(ReadFile(pipe, chunk.data(), static_cast<DWORD>(chunk.size()),
                                     &transferred, &operation), transferred);
            if ((error != ERROR_SUCCESS && error != ERROR_MORE_DATA) ||
                response.size() + transferred > kIpcMaxMessageBytes || GetTickCount64() > deadline) {
                response.clear();
                break;
            }
            response.append(chunk.data(), transferred);
        } while (error == ERROR_MORE_DATA);
    }
    CloseHandle(operation.hEvent);
    CloseHandle(pipe);
    if (command != IpcCommand::get_config && command != IpcCommand::get_info) {
        const auto document = Json::parse(response);
        if (!document.ok() || !document.value().isObject()) return {};
        const auto* requestId = document.value().find("requestId");
        if (!requestId || !requestId->isString() || requestId->stringValue() != uuid.value()) return {};
    }
    return response;
}
}  // namespace
void StatusReader::request(HWND target, UINT message) {
    {
        std::lock_guard lock(mutex_);
        if (!queue_.request()) return;
    }
    if (worker_.joinable()) worker_.join();
    stopping_.store(false);
    worker_ = std::thread([this, target, message] {
        for (;;) {
            auto snapshot = parseStatusResponse(readResponse(IpcCommand::get_status, {}, 400));
            {
                std::lock_guard lock(mutex_);
                if (!stopping_.load()) results_.push_back(std::move(snapshot));
            }
            if (!stopping_.load()) PostMessageW(target, message, 0, 0);
            {
                std::lock_guard lock(mutex_);
                if (stopping_.load() || !queue_.finish()) break;
            }
        }
    });
}
std::optional<TrayStatus> StatusReader::take() {
    std::lock_guard lock(mutex_);
    if (results_.empty()) return {};
    auto result = std::move(results_.front());
    results_.pop_front();
    return result;
}
void StatusReader::stop() {
    stopping_.store(true);
    if (worker_.joinable()) worker_.join();
    std::lock_guard lock(mutex_);
    results_.clear();
    queue_.stop();
}
CommandReader::CommandReader() : cancel_(CreateEventW(nullptr, TRUE, FALSE, nullptr)) {}
CommandReader::~CommandReader() { stop(); if (cancel_) CloseHandle(cancel_); }
bool CommandReader::request(IpcCommand command, const std::string& payload, HWND target, UINT message) {
    if (!cancel_ || busy_.exchange(true)) return false;
    if (worker_.joinable()) worker_.join();
    ResetEvent(cancel_);
    worker_ = std::thread([this, command, payload, target, message] {
        auto response = readResponse(command, payload, 30000, cancel_);
        {
            std::lock_guard lock(mutex_);
            result_ = std::move(response);
        }
        busy_.store(false);
        if (WaitForSingleObject(cancel_, 0) != WAIT_OBJECT_0) PostMessageW(target, message, 0, 0);
    });
    return true;
}
std::optional<std::string> CommandReader::take() {
    std::lock_guard lock(mutex_);
    auto result = std::move(result_);
    result_.reset();
    return result;
}
void CommandReader::stop() {
    if (cancel_) SetEvent(cancel_);
    if (worker_.joinable()) worker_.join();
    busy_.store(false);
    std::lock_guard lock(mutex_);
    result_.reset();
}
}  // namespace phpvpn::tray
