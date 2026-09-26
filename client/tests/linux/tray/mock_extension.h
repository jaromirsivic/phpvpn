// DEVELOPMENT HARNESS ONLY. Registration point of phpvpn-tray-mock-service for
// area mocks in their own files (tests/linux/tray/mock_<area>.cpp): a static
// MockExtensionRegistrar adds an extension before main runs.
#pragma once
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/ipc/ipc_protocol.h"
#include "core/ipc/wizard_ipc.h"
#include "platform/iface/ipc_server.h"

namespace phpvpn::mock {
struct MockExtension {
    // Answers an IPC v1 request before the base mock; nullopt leaves it to the base.
    std::function<std::optional<std::string>(const IpcRequest&)> ipc;
    // Adjusts every status snapshot the base mock returns.
    std::function<void(StatusSnapshot&)> status;
    // Service work class of a complete request (the service classifier); nullopt: not mine.
    std::function<std::optional<IpcWorkClass>(std::string_view)> classify;
    // One stdin control line; true when consumed.
    std::function<bool(const std::string&)> control;
    // One "--name value" command line option; true when consumed.
    std::function<bool(const std::string& name, const std::string& value)> option;
};

inline std::vector<MockExtension>& mockExtensions() {
    static std::vector<MockExtension> extensions;
    return extensions;
}

struct MockExtensionRegistrar {
    explicit MockExtensionRegistrar(MockExtension extension) { mockExtensions().push_back(std::move(extension)); }
};

// Wizard IPC counterpart of MockExtension::ipc (WP10b configuration store): answers a
// Wizard request before the base mock; current is the base mock's status snapshot
// (connection identity of get-config-state). nullopt leaves it to the base.
using MockWizardHandler = std::function<std::optional<std::string>(const WizardRequest&, const StatusSnapshot& current)>;
inline std::vector<MockWizardHandler>& mockWizardHandlers() {
    static std::vector<MockWizardHandler> handlers;
    return handlers;
}
struct MockWizardRegistrar {
    explicit MockWizardRegistrar(MockWizardHandler handler) { mockWizardHandlers().push_back(std::move(handler)); }
};
}  // namespace phpvpn::mock
