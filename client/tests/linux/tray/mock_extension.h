// DEVELOPMENT HARNESS ONLY. Registration point of phpvpn-tray-mock-service for
// area mocks in their own files (tests/linux/tray/mock_<area>.cpp): a static
// MockExtensionRegistrar adds an extension before main runs.
#pragma once
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/config/config.h"
#include "core/ipc/ipc_protocol.h"
#include "core/ipc/wizard_ipc.h"
#include "core/protocol/json.h"
#include "platform/iface/ipc_server.h"

namespace phpvpn::mock {
// The service's Connect refusal of a configuration that still carries the
// placeholder server URL (VpnEngine, config_server_url_unset; the state stays
// disconnected). nullopt when the served document names a real server or cannot
// be read: the scenario answers then.
inline std::optional<std::string> serverUrlUnsetReply(const IpcRequest& request, std::string_view document) {
    auto parsed = Json::parse(document);
    if (!parsed.ok() || !parsed.value().isObject()) return std::nullopt;
    const auto* setup = parsed.value().find("masterSetup");
    if (!setup || !setup->isObject()) return std::nullopt;
    const auto* url = setup->find("serverUrl");
    if (!url || !url->isString()) return std::nullopt;
    auto server = parseServerUrl(url->stringValue());
    if (!server.ok() || !isPlaceholderServerUrl(server.value())) return std::nullopt;
    return IpcProtocol::buildError(request.requestId, Error(ErrorCode::config_server_url_unset));
}

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
