#pragma once
#include <windows.h>
#include <atomic>
#include <mutex>
#include <optional>
#include <thread>
#include <deque>
#include <memory>
#include "core/ipc/ipc_protocol.h"
#include "status_model.h"
#include "poll_schedule.h"

namespace phpvpn::tray {
// Read-only IPC capability. No connect/disconnect operation is available to a
// progress window. One in-flight read per reader, bounded independently of UI.
class StatusReader {
public:
    explicit StatusReader(std::wstring pipeName) : pipeName_(std::move(pipeName)) {}
    StatusReader(const StatusReader&) = delete;
    StatusReader& operator=(const StatusReader&) = delete;
    ~StatusReader() { stop(); }
    void request(HWND target, UINT message);
    std::optional<TrayStatus> take();
    void stop();
    void abandon();
private:
    struct State;
    std::shared_ptr<State> state_;
    const std::wstring pipeName_;
};

// Local identity, never part of the IPC schema. Deadline uses GetTickCount64.
struct SaveOperation {
    std::uint64_t windowGeneration = 0, draftGeneration = 0, operationGeneration = 0;
    std::uint64_t deadlineMs = 0;
    bool operator==(const SaveOperation&) const = default;
};
enum class SaveOutcome { confirmed, notSent, rejected, unknown };
struct SaveResult {
    SaveOperation operation;
    SaveOutcome outcome = SaveOutcome::unknown;
    ErrorCode error = ErrorCode::response_invalid;
};
// Strict local consumer of the existing acknowledgement/error envelope.
SaveResult classifySaveResponse(const SaveOperation& operation, std::string_view requestId,
    std::string_view response, bool writeAttempted, std::uint64_t completedAtMs);

class CommandReader {
public:
    explicit CommandReader(std::wstring pipeName);
    ~CommandReader();
    CommandReader(const CommandReader&) = delete;
    CommandReader& operator=(const CommandReader&) = delete;
    bool request(IpcCommand command, const std::string& payload, HWND target, UINT message);
    bool requestRaw(std::string request, DWORD timeoutMs, HWND target, UINT message);
    std::optional<std::string> take();
    bool requestSave(const SaveOperation& operation, const std::string& payload, HWND target, UINT message);
    // Also expires an in-flight Save at its original deadline, without waiting.
    std::optional<SaveResult> takeSave();
    void stop();
    // UI-thread only. Revokes notifications and publication permission, signals
    // cancellation, releases this owner's reference. No I/O or worker wait.
    void abandon();
private:
    struct OperationState;
    std::shared_ptr<OperationState> operation_;
    bool start(std::string request, DWORD timeoutMs, HWND target, UINT message,
        std::optional<IpcCommand> command, std::optional<SaveOperation> save = {});
    const std::wstring pipeName_;
};
}  // namespace phpvpn::tray
