#include "status_reader.h"
#include "ipc_timeouts.h"
#include <array>
#include <condition_variable>
#include <functional>
#include "core/base/secure_buffer.h"
#include "core/protocol/uuid.h"
#include "core/crypto/crypto_provider.h"
#include "core/ipc/ipc_protocol.h"
#include "core/protocol/json.h"

namespace phpvpn::tray {
namespace {
// One absolute deadline and terminal cancellation, shared by independent
// status, command and Wizard channels. Callers validate their own correlation.
std::string readRawResponse(const std::wstring& pipeName, const std::string& request, DWORD timeoutMs, HANDLE cancel = nullptr,
    std::uint64_t absoluteDeadline = 0, const std::function<bool()>& permitWrite = {}) {
    if (pipeName.empty() || request.empty() || request.size() > kIpcMaxMessageBytes) return {};
    const ULONGLONG deadline = absoluteDeadline ? absoluteDeadline : GetTickCount64() + timeoutMs;
    if (absoluteDeadline && GetTickCount64() >= deadline) return {};
    HANDLE pipe = CreateFileW(pipeName.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                              OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
    if (pipe == INVALID_HANDLE_VALUE && GetLastError() == ERROR_PIPE_BUSY) {
        const auto now = GetTickCount64();
        if (absoluteDeadline && now >= deadline) return {};
        WaitNamedPipeW(pipeName.c_str(), absoluteDeadline ? static_cast<DWORD>((std::min)(100ull, deadline - now)) : 100);
        pipe = CreateFileW(pipeName.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                           OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
    }
    if (pipe == INVALID_HANDLE_VALUE) return {};
    DWORD mode = PIPE_READMODE_MESSAGE;
    SetNamedPipeHandleState(pipe, &mode, nullptr, nullptr);
    OVERLAPPED operation{};
    operation.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!operation.hEvent) { CloseHandle(pipe); return {}; }
    if (permitWrite && !permitWrite()) {
        CloseHandle(operation.hEvent); CloseHandle(pipe); return {};
    }
    auto finish = [&](BOOL started, DWORD& transferred) -> DWORD {
        DWORD error = started ? ERROR_SUCCESS : GetLastError();
        if (error == ERROR_MORE_DATA) {
            transferred = static_cast<DWORD>(operation.InternalHigh);
            return error;
        }
        if (error != ERROR_IO_PENDING) return error;
        const ULONGLONG now = GetTickCount64();
        const DWORD remaining = now < deadline ? static_cast<DWORD>(deadline - now) : 0;
        const HANDLE events[]{operation.hEvent, cancel};
        if (WaitForMultipleObjects(cancel ? 2 : 1, events, FALSE, remaining) != WAIT_OBJECT_0) {
            CancelIoEx(pipe, &operation);
            GetOverlappedResult(pipe, &operation, &transferred, TRUE);
            return ERROR_TIMEOUT;
        }
        if (GetOverlappedResult(pipe, &operation, &transferred, FALSE)) return ERROR_SUCCESS;
        error = GetLastError();
        if (error == ERROR_MORE_DATA) transferred = static_cast<DWORD>(operation.InternalHigh);
        return error;
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
    return response;
}
std::string readResponse(const std::wstring& pipeName, IpcCommand command, const std::string& payload, DWORD timeoutMs, HANDLE cancel = nullptr,
    const std::function<bool()>& permitWrite = {}) {
    auto crypto = createPlatformCryptoProvider();
    if (!crypto) return {};
    const auto uuid = crypto.value()->randomUuidV4();
    if (!uuid) return {};
    auto response = readRawResponse(pipeName, IpcProtocol::buildRequest(command, uuid.value(), payload), timeoutMs, cancel, 0, permitWrite);
    if (command != IpcCommand::get_config && command != IpcCommand::get_info) {
        const auto document = Json::parse(response);
        if (!document.ok() || !document.value().isObject()) return {};
        const auto* requestId = document.value().find("requestId");
        if (!requestId || !requestId->isString() || requestId->stringValue() != uuid.value()) return {};
    }
    return response;
}
}  // namespace
struct StatusReader::State {
    std::mutex mutex;
    std::condition_variable finished;
    HANDLE cancel = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    bool abandoned = false, done = true;
    std::deque<TrayStatus> results;
    ReadRequestQueue queue;
    ~State() { if (cancel) CloseHandle(cancel); }
};
void StatusReader::request(HWND target, UINT message) {
    if (!state_) state_ = std::make_shared<State>();
    const auto state = state_;
    std::lock_guard lock(state->mutex);
    if (state->abandoned || !state->cancel || !state->queue.request()) return;
    state->done = false;
    try {
        std::thread([state, pipeName = pipeName_, target, message] {
            for (;;) {
                TrayStatus snapshot;
                try {
                    snapshot = parseStatusResponse(readResponse(pipeName, IpcCommand::get_status, {}, kStatusRequestTimeoutMs, state->cancel,
                        [state] { std::lock_guard guard(state->mutex); return !state->abandoned; }));
                } catch (...) { snapshot = {}; }
                std::lock_guard guard(state->mutex);
                if (!state->abandoned) {
                    try {
                        state->results.push_back(std::move(snapshot));
                        if (target) PostMessageW(target, message, 0, 0);
                    } catch (...) { state->queue.stop(); state->done = true; break; }
                }
                if (state->abandoned || !state->queue.finish()) { state->done = true; break; }
            }
            state->finished.notify_all();
        }).detach();
    } catch (...) { state->done = true; state->queue.stop(); }
}
std::optional<TrayStatus> StatusReader::take() {
    const auto state = state_;
    if (!state) return {};
    std::lock_guard lock(state->mutex);
    if (state->abandoned || state->results.empty()) return {};
    auto result = std::move(state->results.front());
    state->results.pop_front();
    return result;
}
void StatusReader::stop() {
    const auto state = state_;
    abandon();
    if (!state) return;
    std::unique_lock lock(state->mutex);
    state->finished.wait(lock, [&] { return state->done; });
}
void StatusReader::abandon() {
    const auto state = std::move(state_);
    if (!state) return;
    std::lock_guard lock(state->mutex);
    state->abandoned = true; state->results.clear(); state->queue.stop();
    if (state->cancel) SetEvent(state->cancel);
}

struct CommandReader::OperationState {
    HANDLE cancel = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    std::mutex mutex;
    std::condition_variable finished;
    bool done = false, abandoned = false, writeAttempted = false;
    HWND target = nullptr;
    UINT message = 0;
    std::optional<std::string> response;
    std::optional<SaveOperation> save;
    std::optional<SaveResult> saveResult;
    ~OperationState() {
        if (response) secureWipe(response->data(), response->size());
        if (cancel) CloseHandle(cancel);
    }
};
CommandReader::CommandReader(std::wstring pipeName) : pipeName_(std::move(pipeName)) {}
CommandReader::~CommandReader() { stop(); }
bool CommandReader::request(IpcCommand command, const std::string& payload, HWND target, UINT message) {
    // set-config must use requestSave so publication uncertainty is never lost.
    if (command == IpcCommand::set_config) return false;
    return start(payload, kCommandTimeoutMs, target, message, command);
}
bool CommandReader::requestRaw(std::string request, DWORD timeoutMs, HWND target, UINT message) {
    return start(std::move(request), timeoutMs, target, message, {});
}
bool CommandReader::requestSave(const SaveOperation& operation, const std::string& payload, HWND target, UINT message) {
    if (!operation.windowGeneration || !operation.operationGeneration || !operation.deadlineMs) return false;
    return start(payload, kCommandTimeoutMs, target, message, IpcCommand::set_config, operation);
}
bool CommandReader::start(std::string request, DWORD timeoutMs, HWND target, UINT message,
    std::optional<IpcCommand> command, std::optional<SaveOperation> save) {
    if (operation_) { secureWipe(request.data(), request.size()); return false; }
    auto state = std::make_shared<OperationState>();
    if (!state->cancel) { secureWipe(request.data(), request.size()); return false; }
    state->target = target; state->message = message; state->save = save;
    // The detached worker owns only value captures and state. It never owns a
    // reader, window or application pointer. stop() can wait, abandon() cannot.
    try {
        std::thread([state, pipeName = pipeName_, request = std::move(request), timeoutMs, command]() mutable {
            std::string response, id, encoded;
            struct Wipe {
                std::string &request, &response, &encoded;
                ~Wipe() { for (auto* s : {&request, &response, &encoded}) secureWipe(s->data(), s->size()); }
            } wipe{request, response, encoded};
            SaveResult saveResult;
            try {
                if (state->save) {
                    auto crypto = createPlatformCryptoProvider();
                    if (crypto) {
                        auto uuid = crypto.value()->randomUuidV4();
                        if (uuid) {
                            id = uuid.value();
                            encoded = IpcProtocol::buildRequest(IpcCommand::set_config, id, request);
                            response = readRawResponse(pipeName, encoded, timeoutMs, state->cancel,
                                state->save->deadlineMs, [state] {
                                    std::lock_guard lock(state->mutex);
                                    if (state->abandoned || GetTickCount64() >= state->save->deadlineMs) return false;
                                    state->writeAttempted = true;
                                    return true;
                                });
                        }
                    }
                } else {
                    const auto permit = [state] { std::lock_guard lock(state->mutex); return !state->abandoned; };
                    if (command) response = readResponse(pipeName, *command, request, timeoutMs, state->cancel, permit);
                    else response = readRawResponse(pipeName, request, timeoutMs, state->cancel, 0, permit);
                }
                if (state->save) saveResult = classifySaveResponse(*state->save, id, response,
                    state->writeAttempted, GetTickCount64());
            } catch (...) {
                if (state->save) saveResult = {*state->save,
                    state->writeAttempted ? SaveOutcome::unknown : SaveOutcome::notSent, ErrorCode::internal};
            }
            {
                std::lock_guard lock(state->mutex);
                if (!state->abandoned) {
                    if (state->save) state->saveResult = saveResult;
                    else state->response = std::move(response);
                    // Serialized with abandon. Already queued messages contain
                    // no pointers and cannot retrieve this abandoned state.
                    if (state->target) PostMessageW(state->target, state->message, 0, 0);
                }
                state->done = true;
            }
            state->finished.notify_all();
        }).detach();
    } catch (...) { secureWipe(request.data(), request.size()); return false; }
    operation_ = std::move(state);
    return true;
}
std::optional<std::string> CommandReader::take() {
    const auto state = operation_;
    if (!state || state->save) return {};
    std::lock_guard lock(state->mutex);
    if (!state->done || state->abandoned) return {};
    auto result = std::move(state->response);
    state->response.reset(); operation_.reset();
    return result;
}
std::optional<SaveResult> CommandReader::takeSave() {
    const auto state = operation_;
    if (!state || !state->save) return {};
    std::optional<SaveResult> result;
    {
        std::lock_guard lock(state->mutex);
        if (state->abandoned) return {};
        if (state->saveResult) result = state->saveResult;
        else if (GetTickCount64() >= state->save->deadlineMs) {
            result = SaveResult{*state->save, state->writeAttempted ? SaveOutcome::unknown : SaveOutcome::notSent, ErrorCode::timeout};
        } else return {};
        state->abandoned = true; state->target = nullptr;
        SetEvent(state->cancel);
    }
    operation_.reset();
    return result;
}
void CommandReader::abandon() {
    const auto state = std::move(operation_);
    if (!state) return;
    std::lock_guard lock(state->mutex);
    state->abandoned = true; state->target = nullptr;
    SetEvent(state->cancel);
}
void CommandReader::stop() {
    const auto state = operation_;
    abandon();
    if (!state) return;
    std::unique_lock lock(state->mutex);
    state->finished.wait(lock, [&] { return state->done; });
}
}  // namespace phpvpn::tray
