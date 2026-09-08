#pragma once
#include "status_reader.h"
#include "core/ipc/wizard_ipc.h"

namespace phpvpn::tray {
struct WizardReadResult { WizardRequest request; Result<WizardReply> reply; std::uint64_t generation = 0; };
// A bounded, cancellable IPC channel. Separate instances keep heartbeat and
// cancellation independent of an in-flight start acknowledgement or result.
class WizardReader {
public:
    explicit WizardReader(std::wstring pipeName) : reader_(std::move(pipeName)) {}
    ~WizardReader() { stop(); }
    bool request(WizardRequest request, HWND target, UINT message, std::uint64_t generation = 0);
    std::optional<WizardReadResult> take();
    bool busy() const { return pending_.has_value(); }
    void stop() { reader_.stop(); clearPending(); }
    // UI-thread only. The transport worker owns its state and request values.
    // Revokes delivery without waiting for cancellation or an IPC reply.
    void abandon() { reader_.abandon(); clearPending(); ++generation_; }
private:
    void clearPending();
    CommandReader reader_;
    std::optional<WizardRequest> pending_;
    std::uint64_t generation_ = 0;
};

// Wizard gate's read-only status channel, with the original 400ms budget.
// Uses the shared transport ownership rather than StatusReader's this/join.
class WizardStatusReader {
public:
    explicit WizardStatusReader(std::wstring pipeName) : reader_(std::move(pipeName)) {}
    void request(HWND target, UINT message);
    std::optional<TrayStatus> take();
    void stop() { reader_.stop(); requestId_.clear(); }
    void abandon() { reader_.abandon(); requestId_.clear(); }
private:
    CommandReader reader_;
    std::string requestId_;
};
} // namespace phpvpn::tray
