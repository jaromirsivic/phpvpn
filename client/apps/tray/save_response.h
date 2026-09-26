#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include "core/base/error.h"

// Portable save classification shared by the Win32 and GTK trays. No platform
// headers: the transport feeds the raw reply and the write gate state in.
namespace phpvpn::tray {
// Local identity, never part of the IPC schema. Deadline uses the monotonic
// millisecond clock of the caller (GetTickCount64 on Windows).
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
    std::string validationDescription;
};
// Strict local consumer of the existing acknowledgement/error envelope.
SaveResult classifySaveResponse(const SaveOperation& operation, std::string_view requestId,
    std::string_view response, bool writeAttempted, std::uint64_t completedAtMs);
}  // namespace phpvpn::tray
