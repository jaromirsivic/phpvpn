#include "platform/windows/ipc_pipe.h"
#include <windows.h>
#include <sddl.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <limits>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>
#include "core/ipc/ipc_protocol.h"
#include "platform/iface/ipc_executors.h"

namespace phpvpn {
namespace {
constexpr std::size_t kSlots = 32;
// MSVC strings round capacity to 16 bytes including the terminal NUL. Reserve
// this fixed overhead for both strings inside the 2 MiB I/O allocation budget.
constexpr std::size_t kStringAllocationSlack = 16;
constexpr DWORD kChunkBytes = 64 * 1024 - 2 * kStringAllocationSlack;
constexpr std::size_t kMaxStringCapacity = kIpcMaxMessageBytes + kStringAllocationSlack - 1;
static_assert(kSlots * (kChunkBytes + 2 * kStringAllocationSlack) == 2 * 1024 * 1024);
constexpr DWORD kKernelBufferBytes = 16 * 1024;
constexpr ULONGLONG kIoDeadlineMs = 5000;
constexpr ULONGLONG kTickMs = 250;
constexpr const wchar_t* kSecurity = L"D:(A;;GRGW;;;WD)(A;;FA;;;SY)(A;;FA;;;BA)";
class Handle {
public:
    HANDLE value = INVALID_HANDLE_VALUE;
    Handle() = default;
    explicit Handle(HANDLE h) : value(h) {}
    ~Handle() { close(); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    void close() {
        if (value != INVALID_HANDLE_VALUE && value != nullptr) CloseHandle(value);
        value = INVALID_HANDLE_VALUE;
    }
    explicit operator bool() const { return value != INVALID_HANDLE_VALUE && value != nullptr; }
};
class Security {
public:
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    SECURITY_ATTRIBUTES attributes{};
    ~Security() { if (descriptor) LocalFree(descriptor); }
    bool build() {
        if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(kSecurity,
            SDDL_REVISION_1, &descriptor, nullptr)) return false;
        attributes = {sizeof(attributes), descriptor, FALSE};
        return true;
    }
};
bool validPipeName(std::wstring_view name) {
    return name.starts_with(L"\\\\.\\pipe\\") && name.size() > 9 &&
        name.size() < 256 && name.find(L'\0') == std::wstring_view::npos;
}
DWORD remaining(ULONGLONG deadline) {
    const auto now = GetTickCount64();
    return now >= deadline ? 0 : static_cast<DWORD>(std::min<ULONGLONG>(deadline - now, MAXDWORD - 1));
}
// No OVERLAPPED or backing buffer is destroyed until the kernel confirms terminal I/O.
void cancelAndDrain(HANDLE pipe, OVERLAPPED& operation) {
    CancelIoEx(pipe, &operation);
    DWORD ignored = 0;
    GetOverlappedResult(pipe, &operation, &ignored, TRUE);
}
}

struct IpcPipeServer::Impl {
    using Job = IpcJob;
    enum class Phase { listening, reading, executing, writing, awaitingClose };
    struct Slot {
        Handle pipe;
        Handle event;
        OVERLAPPED operation{};
        std::array<char, kChunkBytes> chunk{};
        std::string request;
        std::shared_ptr<Job> job;
        Phase phase = Phase::listening;
        bool pending = false;
        DWORD transferred = 0;
        DWORD error = ERROR_SUCCESS;
        ULONGLONG deadline = 0;
    };
    IpcPipeServerOptions options;
    std::wstring name;
    std::mutex lifecycle;
    std::atomic<bool> running{false};
    Handle stopEvent;
    Handle completedEvent;
    Security security;
    std::array<std::unique_ptr<Slot>, kSlots> slots;
    IpcExecutors executors;
    std::thread loop;
    std::atomic<std::size_t> allocatedSlots{0};
    std::atomic<std::uint64_t> ioTimeouts{0};

    Impl(IpcHandler h, IpcPipeServerOptions o)
        : options(std::move(o)),
          name(options.pipeName.empty() ? kIpcPipeNameW : options.pipeName),
          executors(std::move(h), options, kMaxStringCapacity,
                    [this] { SetEvent(completedEvent.value); }) {}

    void prepare(Slot& slot) {
        ResetEvent(slot.event.value);
        slot.operation = {};
        slot.operation.hEvent = slot.event.value;
        slot.transferred = 0;
        slot.error = ERROR_SUCCESS;
        slot.pending = false;
    }
    void completed(Slot& slot, BOOL success, DWORD transferred) {
        slot.transferred = transferred;
        slot.error = success ? ERROR_SUCCESS : GetLastError();
        if (!success && slot.error == ERROR_IO_PENDING) slot.pending = true;
        else {
            // For an overlapped message read, ERROR_MORE_DATA can leave the
            // immediate byte count zero. The terminal OVERLAPPED owns the count.
            if (!success && slot.error == ERROR_MORE_DATA) slot.pending = true;
            SetEvent(slot.event.value);
        }
    }
    bool listen(Slot& slot, bool first = false) {
        prepare(slot);
        slot.phase = Phase::listening;
        slot.deadline = 0;
        slot.pipe.value = CreateNamedPipeW(name.c_str(),
            PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED | (first ? FILE_FLAG_FIRST_PIPE_INSTANCE : 0),
            PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
            static_cast<DWORD>(kSlots), kKernelBufferBytes, kKernelBufferBytes, 0,
            &security.attributes);
        if (!slot.pipe) return false;
        const BOOL success = ConnectNamedPipe(slot.pipe.value, &slot.operation);
        completed(slot, success, 0);
        if (!success && slot.error == ERROR_PIPE_CONNECTED) {
            slot.error = ERROR_SUCCESS;
            slot.pending = false;
            SetEvent(slot.event.value);
        }
        return true;
    }
    void recycle(Slot& slot) {
        if (slot.pending) cancelAndDrain(slot.pipe.value, slot.operation);
        slot.pending = false;
        slot.pipe.close();
        slot.job.reset();
        std::string{}.swap(slot.request);
        if (running.load(std::memory_order_acquire) && !listen(slot)) {
            // A failed listener is retried only by bounded event-loop maintenance.
            ResetEvent(slot.event.value);
            slot.deadline = GetTickCount64() + kTickMs;
            slot.phase = Phase::listening;
        }
    }
    void read(Slot& slot, bool first = false) {
        prepare(slot);
        slot.phase = Phase::reading;
        if (first) {
            slot.deadline = GetTickCount64() + kIoDeadlineMs;
            // Fixed upper capacity prevents geometric growth beyond the reservation.
            slot.request.reserve(kIpcMaxMessageBytes);
        }
        DWORD bytes = 0;
        const BOOL success = ReadFile(slot.pipe.value, slot.chunk.data(), kChunkBytes,
                                      &bytes, &slot.operation);
        completed(slot, success, bytes);
    }
    void write(Slot& slot) {
        prepare(slot);
        slot.phase = Phase::writing;
        slot.deadline = GetTickCount64() + kIoDeadlineMs;
        DWORD bytes = 0;
        const BOOL success = WriteFile(slot.pipe.value, slot.job->response.data(),
            static_cast<DWORD>(slot.job->response.size()), &bytes, &slot.operation);
        completed(slot, success, bytes);
    }
    void awaitClientClose(Slot& slot) {
        // Write completion may mean only "buffered". Keep the server end alive
        // until the client closes after reading or the original output deadline
        // expires. This replaces unbounded FlushFileBuffers without data loss.
        prepare(slot);
        slot.phase = Phase::awaitingClose;
        DWORD bytes = 0;
        const BOOL success = ReadFile(slot.pipe.value, slot.chunk.data(), 1, &bytes, &slot.operation);
        completed(slot, success, bytes);
    }
    void dispatch(Slot& slot) {
        slot.job = std::make_shared<Job>();
        slot.job->request = std::move(slot.request);
        try {
            // Classification, bounded queueing and the busy response are shared.
            switch (executors.admit(slot.job)) {
            case IpcAdmission::queued:
                slot.phase = Phase::executing;
                slot.deadline = 0;
                ResetEvent(slot.event.value);
                return;
            case IpcAdmission::respond: write(slot); return;
            case IpcAdmission::close: break;
            }
            recycle(slot);
        } catch (...) { recycle(slot); }
    }
    void advance(Slot& slot) {
        ResetEvent(slot.event.value);
        if (slot.pending) {
            DWORD bytes = 0;
            const BOOL success = GetOverlappedResult(slot.pipe.value, &slot.operation, &bytes, FALSE);
            const DWORD error = success ? ERROR_SUCCESS : GetLastError();
            if (error == ERROR_IO_INCOMPLETE) return;
            slot.pending = false;
            slot.transferred = bytes;
            slot.error = error;
        }
        switch (slot.phase) {
        case Phase::listening:
            if (slot.error == ERROR_SUCCESS) read(slot, true);
            else recycle(slot);
            break;
        case Phase::reading:
            if (slot.error != ERROR_SUCCESS && slot.error != ERROR_MORE_DATA) { recycle(slot); break; }
            if (slot.transferred > kIpcMaxMessageBytes - slot.request.size()) {
                executors.countRejected();
                recycle(slot);
                break;
            }
            slot.request.append(slot.chunk.data(), slot.transferred);
            if (slot.error == ERROR_MORE_DATA) read(slot);
            else if (slot.request.empty()) recycle(slot);
            else dispatch(slot);
            break;
        case Phase::writing:
            if (slot.error == ERROR_SUCCESS && slot.transferred == slot.job->response.size())
                awaitClientClose(slot);
            else recycle(slot);
            break;
        case Phase::awaitingClose:
            recycle(slot); // A second request is not a second command.
            break;
        case Phase::executing: break;
        }
    }
    void runLoop() {
        ULONGLONG nextTick = GetTickCount64();
        std::array<HANDLE, kSlots + 2> handles{};
        handles[0] = stopEvent.value;
        handles[1] = completedEvent.value;
        for (std::size_t i = 0; i < kSlots; ++i) handles[i + 2] = slots[i]->event.value;
        while (running.load(std::memory_order_acquire)) {
            ULONGLONG now = GetTickCount64();
            if (now >= nextTick) {
                if (options.onTick) { try { options.onTick(); } catch (...) {} }
                nextTick = GetTickCount64() + kTickMs;
            }
            ResetEvent(completedEvent.value);
            // One completion per slot per pass prevents a large/active client
            // from monopolising the event loop or delaying a short command.
            for (auto& holder : slots) {
                auto& slot = *holder;
                if (!slot.pipe) {
                    if (now >= slot.deadline && !listen(slot)) slot.deadline = now + kTickMs;
                    continue;
                }
                if (slot.deadline && now >= slot.deadline) {
                    if (slot.phase != Phase::listening) ioTimeouts.fetch_add(1);
                    recycle(slot);
                } else if (slot.phase == Phase::executing && slot.job &&
                           slot.job->done.load(std::memory_order_acquire)) {
                    if (slot.job->closeOnly) recycle(slot);
                    else write(slot);
                } else if (WaitForSingleObject(slot.event.value, 0) == WAIT_OBJECT_0) {
                    try { advance(slot); } catch (...) { recycle(slot); }
                }
            }
            ULONGLONG deadline = nextTick;
            for (const auto& slot : slots)
                if (slot->deadline) deadline = std::min(deadline, slot->deadline);
            const DWORD waited = WaitForMultipleObjects(static_cast<DWORD>(handles.size()),
                handles.data(), FALSE, remaining(deadline));
            if (waited == WAIT_OBJECT_0 || waited == WAIT_FAILED) break;
        }
        // Every pending OVERLAPPED is cancelled and observed terminal before any
        // pipe, event, chunk or request/response storage is released.
        for (auto& slot : slots) {
            if (slot->pending) cancelAndDrain(slot->pipe.value, slot->operation);
            slot->pending = false;
            slot->pipe.close();
        }
    }
    Status start() {
        std::lock_guard lock(lifecycle);
        if (running.load()) return Error(ErrorCode::service_already_running);
        if (!executors.hasHandler() || !validPipeName(name)) return Error(ErrorCode::invalid_argument);
        if (!security.descriptor && !security.build()) return Error(ErrorCode::ipc_unavailable);
        stopEvent.value = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        completedEvent.value = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!stopEvent || !completedEvent) { cleanup(); return Error(ErrorCode::ipc_unavailable); }
        try {
            for (std::size_t i = 0; i < kSlots; ++i) {
                slots[i] = std::make_unique<Slot>();
                slots[i]->event.value = CreateEventW(nullptr, TRUE, FALSE, nullptr);
                if (!slots[i]->event || !listen(*slots[i], i == 0)) {
                    cleanup();
                    return Error(ErrorCode::ipc_unavailable, "the local pipe could not be opened");
                }
            }
            allocatedSlots.store(kSlots);
            running.store(true, std::memory_order_release);
            executors.start();
            loop = std::thread([this] { runLoop(); });
            return Status::success();
        } catch (...) {
            stopLocked();
            return Error(ErrorCode::ipc_unavailable);
        }
    }
    void cleanup() {
        for (auto& slot : slots) {
            if (!slot) continue;
            if (slot->pending && slot->pipe) cancelAndDrain(slot->pipe.value, slot->operation);
            slot.reset();
        }
        allocatedSlots.store(0);
        stopEvent.close();
        completedEvent.close();
    }
    void stopLocked() {
        const bool wasRunning = running.exchange(false, std::memory_order_acq_rel);
        executors.closeAdmission();
        if (stopEvent) SetEvent(stopEvent.value);
        // cancelHandlers once, then the executors finish their queues.
        executors.shutdown(wasRunning);
        if (loop.joinable()) loop.join();
        // Accepted jobs and tasks are drained after cancellation, never dropped.
        executors.join();
        cleanup();
    }
    void stop() { std::lock_guard lock(lifecycle); stopLocked(); }
};

IpcPipeServer::IpcPipeServer(IpcHandler handler)
    : IpcPipeServer(std::move(handler), {}) {}
IpcPipeServer::IpcPipeServer(IpcHandler handler, IpcPipeServerOptions options)
    : impl_(std::make_unique<Impl>(std::move(handler), std::move(options))) {}
IpcPipeServer::~IpcPipeServer() { stop(); }
Status IpcPipeServer::start() { return impl_->start(); }
void IpcPipeServer::stop() { impl_->stop(); }
bool IpcPipeServer::tryPostFile(std::function<void()> task) {
    return impl_->executors.tryPost(IpcWorkClass::file, std::move(task));
}
bool IpcPipeServer::tryPostDiagnostic(std::function<void()> task) {
    return impl_->executors.tryPost(IpcWorkClass::diagnostic, std::move(task));
}
IpcPipeServerStats IpcPipeServer::stats() const {
    IpcPipeServerStats result;
    result.connectionSlots = impl_->allocatedSlots.load();
    impl_->executors.fillStats(result);
    result.ioTimeouts = impl_->ioTimeouts.load();
    return result;
}

Result<std::string> sendIpcRequestTo(std::wstring_view pipeName, std::string_view requestJson,
                                   std::int64_t timeoutMs) {
    if (!validPipeName(pipeName) || timeoutMs <= 0 || timeoutMs > MAXDWORD - 1)
        return Error(ErrorCode::invalid_argument);
    if (requestJson.size() > kIpcMaxMessageBytes) return Error(ErrorCode::ipc_message_too_large);
    const std::wstring name(pipeName);
    const ULONGLONG deadline = GetTickCount64() + static_cast<ULONGLONG>(timeoutMs);
    Handle pipe;
    for (;;) {
        pipe.value = CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
            OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
        if (pipe) break;
        if (GetLastError() != ERROR_PIPE_BUSY) return Error(ErrorCode::ipc_unavailable);
        if (!remaining(deadline) || !WaitNamedPipeW(name.c_str(), remaining(deadline)))
            return Error(ErrorCode::timeout);
    }
    DWORD mode = PIPE_READMODE_MESSAGE;
    if (!SetNamedPipeHandleState(pipe.value, &mode, nullptr, nullptr)) return Error(ErrorCode::ipc_unavailable);
    Handle event(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    if (!event) return Error(ErrorCode::ipc_unavailable);
    OVERLAPPED operation{};
    operation.hEvent = event.value;
    auto finishIo = [&](BOOL success, DWORD& bytes) -> DWORD {
        DWORD error = success ? ERROR_SUCCESS : GetLastError();
        if (!success && error == ERROR_MORE_DATA) {
            GetOverlappedResult(pipe.value, &operation, &bytes, FALSE);
        }
        if (!success && error == ERROR_IO_PENDING) {
            const DWORD waited = WaitForSingleObject(event.value, remaining(deadline));
            if (waited != WAIT_OBJECT_0) {
                cancelAndDrain(pipe.value, operation);
                return ERROR_TIMEOUT;
            }
            if (!GetOverlappedResult(pipe.value, &operation, &bytes, FALSE)) error = GetLastError();
            else error = ERROR_SUCCESS;
        }
        if (GetTickCount64() >= deadline) return ERROR_TIMEOUT;
        return error;
    };
    DWORD bytes = 0;
    DWORD error = finishIo(WriteFile(pipe.value, requestJson.data(),
        static_cast<DWORD>(requestJson.size()), &bytes, &operation), bytes);
    if (error == ERROR_TIMEOUT) return Error(ErrorCode::timeout);
    if (error != ERROR_SUCCESS || bytes != requestJson.size()) return Error(ErrorCode::ipc_unavailable);
    std::string response;
    response.reserve(kIpcMaxMessageBytes);
    std::array<char, kChunkBytes> chunk{};
    for (;;) {
        ResetEvent(event.value);
        operation = {};
        operation.hEvent = event.value;
        bytes = 0;
        error = finishIo(ReadFile(pipe.value, chunk.data(), static_cast<DWORD>(chunk.size()),
                                 &bytes, &operation), bytes);
        if (error == ERROR_TIMEOUT) return Error(ErrorCode::timeout);
        if (error != ERROR_SUCCESS && error != ERROR_MORE_DATA) return Error(ErrorCode::ipc_unavailable);
        if (bytes > kIpcMaxMessageBytes - response.size()) return Error(ErrorCode::ipc_message_too_large);
        response.append(chunk.data(), bytes);
        if (error == ERROR_SUCCESS) return response;
    }
}
Result<std::string> sendIpcRequest(std::string_view requestJson, std::int64_t timeoutMs) {
    return sendIpcRequestTo(kIpcPipeNameW, requestJson, timeoutMs);
}
}  // namespace phpvpn
