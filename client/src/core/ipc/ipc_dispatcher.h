// Maps a validated IPC request onto the engine.
//
// The dispatcher is deliberately identity blind: it never inspects the caller,
// its token, its process or its path. The only protective boundary is the fixed
// versioned contract, the complete message validation and the closed command
// list, exactly as client.md describes.
#pragma once

#include <string>
#include <string_view>

#include "core/ipc/ipc_protocol.h"
#include "core/runtime/vpn_engine.h"

namespace phpvpn {
class WizardService;

class IpcDispatcher {
public:
    explicit IpcDispatcher(VpnEngine& engine, WizardService* wizard = nullptr) : engine_(engine), wizard_(wizard) {}

    // Returns the exact bytes to send back. `get-info` and `get-config` answer
    // without an IPC envelope, every other command answers with one.
    std::string handle(std::string_view requestJson);

private:
    VpnEngine& engine_;
    WizardService* wizard_;
};

}  // namespace phpvpn
