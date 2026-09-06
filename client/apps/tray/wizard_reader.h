#pragma once
#include "status_reader.h"
#include "core/ipc/wizard_ipc.h"

namespace phpvpn::tray {
struct WizardReadResult { WizardRequest request; Result<WizardReply> reply; std::uint64_t generation = 0; };
// A bounded, cancellable IPC channel. Separate instances keep heartbeat and
// cancellation independent of an in-flight start acknowledgement or result.
class WizardReader {
public:
    explicit WizardReader(std::wstring pipeName = kIpcPipeNameW) : reader_(std::move(pipeName)) {}
    bool request(WizardRequest request, HWND target, UINT message, std::uint64_t generation = 0);
    std::optional<WizardReadResult> take();
    bool busy() const { return pending_.has_value(); }
    void stop() { reader_.stop(); pending_.reset(); }
private:
    CommandReader reader_;
    std::optional<WizardRequest> pending_;
    std::uint64_t generation_ = 0;
};
} // namespace phpvpn::tray
