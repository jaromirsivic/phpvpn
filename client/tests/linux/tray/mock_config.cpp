// DEVELOPMENT HARNESS ONLY, never installed or packaged.
//
// Configuration document store of phpvpn-tray-mock-service for the Edit Config and
// Communication contracts windows (WP10 part b), registered through
// mock_extension.h. With --config-dir <dir> the mock answers the configuration
// commands exactly as the service does, through the real core ConfigStore over
// createLinuxConfigFiles (<dir>/config.json owned by the calling user, so no root
// is needed) and the same reply builders as WizardService / IpcDispatcher /
// VpnEngine::setConfigJson:
//
//   IPC v1:  get-config, set-config (full validation, atomic publication)
//   Wizard:  get-config-state, get-config-document, get-default-config,
//            create-default-config
//
// Validation errors are therefore the real ones (config_invalid_json, the HTTP
// header grammar with path/line/header name, ...). Without --config-dir the base
// mock's synthetic replies stay in effect. Control lines on stdin:
//   config missing        remove config.json (next Edit Config offers creation)
//   config invalid        replace config.json with syntactically invalid JSON
//   setconfig normal      default behaviour
//   setconfig busy        answer busy without writing (proven rejection)
//   setconfig foreign     publish, then answer with a foreign requestId (unknown)
//   setconfig delay <ms>  wait before handling set-config (saving state, deadline)
#include <fcntl.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <thread>

#include "core/config/config_defaults.h"
#include "core/config/config_store.h"
#include "core/crypto/crypto_provider.h"
#include "core/ipc/ipc_protocol.h"
#include "core/ipc/wizard_ipc.h"
#include "core/protocol/json.h"
#include "mock_extension.h"
#include "platform/linux/config_files_linux.h"

namespace phpvpn::mock {
namespace {
using J = JsonValue;
J str(std::string_view text) { return J::makeString(std::string(text)); }
J nullable(std::string_view text) { return text.empty() ? J::makeNull() : str(text); }

enum class SaveMode { normal, busy, foreign, delay };

struct Store {
    std::mutex mutex;
    std::unique_ptr<CryptoProvider> crypto;
    std::unique_ptr<ConfigStore> store;
    std::string path, instanceId;
    SaveMode mode = SaveMode::normal;
    int delayMs = 0;
};
Store& instance() {
    static Store store;
    return store;
}

bool storeCommand(WizardCommand command) {
    switch (command) {
        case WizardCommand::get_config_state: case WizardCommand::get_config_document:
        case WizardCommand::get_default_config: case WizardCommand::create_default_config: return true;
        default: return false;
    }
}

// WizardService::execute for the four configuration commands.
std::string handleWizard(Store& s, const WizardRequest& request, const StatusSnapshot& current) {
    const auto failure = [&](const Error& error) {
        return WizardIpc::buildError(request.requestId, WizardIpc::safeError(error, {}, {}, {}, s.path));
    };
    if (request.command == WizardCommand::get_default_config)
        return WizardIpc::buildResult(request.requestId, J::makeObject({{"document", str(makeDefaultEditorDocument())}}));
    if (request.command == WizardCommand::create_default_config) {
        auto created = s.store->createDefault();
        if (!created) return failure(created.error());
        if (!created->snapshot.document) return failure(Error(created->snapshot.reasonCode.value_or(ErrorCode::config_unreadable)));
        return WizardIpc::buildResult(request.requestId, J::makeObject({
            {"created", J::makeBool(created->created)}, {"configPath", str(s.path)},
            {"availability", str(configAvailabilityName(created->snapshot.availability))},
            {"document", str(*created->snapshot.document)}, {"revisionToken", str(created->snapshot.revisionToken)}}));
    }
    auto snapshot = s.store->read();
    if (!snapshot) return failure(snapshot.error());
    if (request.command == WizardCommand::get_config_document && !snapshot->document &&
        snapshot->availability != ConfigAvailability::missing)
        return failure(Error(snapshot->reasonCode.value_or(ErrorCode::config_unreadable)));
    JsonObject result{{"configPath", str(s.path)}, {"availability", str(configAvailabilityName(snapshot->availability))},
                      {"reasonCode", snapshot->reasonCode ? str(errorCodeName(*snapshot->reasonCode)) : J::makeNull()},
                      {"revisionToken", nullable(snapshot->revisionToken)}};
    if (request.command == WizardCommand::get_config_document) {
        result.emplace_back("document", snapshot->document ? str(*snapshot->document) : J::makeNull());
    } else {
        result.emplace_back("serviceInstanceId", str(s.instanceId));
        result.emplace_back("connectionEpoch", J::makeUnsignedInteger(current.connectionEpoch));
        result.emplace_back("vpnState", str(connectionStateName(current.state)));
        result.emplace_back("vpnPhase", str(connectionPhaseName(current.phase)));
    }
    return WizardIpc::buildResult(request.requestId, J::makeObject(std::move(result)));
}

// IpcDispatcher set-config with VpnEngine::setConfigJson.
std::string handleSetConfig(Store& s, const IpcRequest& request) {
    std::string commitId;
    if (auto id = s.crypto->randomUuidV4()) commitId = id.value();
    else return IpcProtocol::buildError(request.requestId, id.error());
    auto commit = s.store->save(request.config, commitId);
    if (!commit) return IpcProtocol::buildError(request.requestId, commit.error());
    if (commit->state != ConfigPublishState::confirmed)
        return IpcProtocol::buildError(request.requestId, Error(ErrorCode::config_write_failed, "the publication is not confirmed"));
    return IpcProtocol::buildAcknowledgement(request.requestId);
}

bool configure(const std::string& directory) {
    auto& s = instance();
    std::lock_guard lock(s.mutex);
    auto crypto = createPlatformCryptoProvider();
    if (!crypto) return false;
    s.crypto = std::move(crypto).value();
    s.path = directory + "/config.json";
    if (auto id = s.crypto->randomUuidV4()) s.instanceId = id.value();
    LinuxConfigFilePolicy policy;
    policy.expectedOwner = geteuid();
    auto files = createLinuxConfigFiles(s.path, policy);
    if (!files) return false;
    s.store = std::make_unique<ConfigStore>(*s.crypto, std::move(files).value());
    return static_cast<bool>(s.store->recover());
}

std::optional<std::string> handleIpc(const IpcRequest& request) {
    auto& s = instance();
    if (!s.store) return std::nullopt;
    if (request.command == IpcCommand::get_config) {
        std::lock_guard lock(s.mutex);
        auto snapshot = s.store->read();
        if (!snapshot) return IpcProtocol::buildError(request.requestId, snapshot.error());
        if (!snapshot->document)
            return IpcProtocol::buildError(request.requestId, Error(snapshot->reasonCode.value_or(ErrorCode::config_not_found)));
        return *snapshot->document;
    }
    if (request.command != IpcCommand::set_config) return std::nullopt;
    SaveMode mode;
    int delay;
    {
        std::lock_guard lock(s.mutex);
        mode = s.mode;
        delay = s.delayMs;
    }
    if (mode == SaveMode::delay) std::this_thread::sleep_for(std::chrono::milliseconds(delay));
    if (mode == SaveMode::busy) return IpcProtocol::buildError(request.requestId, Error(ErrorCode::busy));
    std::lock_guard lock(s.mutex);
    auto reply = handleSetConfig(s, request);
    if (mode == SaveMode::foreign && reply.find("\"result\"") != std::string::npos)
        return IpcProtocol::buildAcknowledgement("00000000-0000-4000-8000-000000000001");
    return reply;
}

std::optional<std::string> handleWizardRequest(const WizardRequest& request, const StatusSnapshot& current) {
    auto& s = instance();
    if (!s.store || !storeCommand(request.command)) return std::nullopt;
    std::lock_guard lock(s.mutex);
    return handleWizard(s, request, current);
}

// As the service: configuration file work runs on the file executor, so a delayed
// set-config never blocks get-status.
std::optional<IpcWorkClass> classify(std::string_view request) {
    if (!instance().store) return std::nullopt;
    if (auto ipc = IpcProtocol::parseRequest(request)) {
        if (ipc->command == IpcCommand::set_config || ipc->command == IpcCommand::get_config) return IpcWorkClass::file;
        return std::nullopt;
    }
    if (auto wizard = WizardIpc::parseRequest(request); wizard && storeCommand(wizard->command)) return IpcWorkClass::file;
    return std::nullopt;
}

bool control(const std::string& line) {
    auto& s = instance();
    std::lock_guard lock(s.mutex);
    if (line == "config missing") {
        if (!s.path.empty()) ::unlink(s.path.c_str());
        return true;
    }
    if (line == "config invalid") {
        if (s.path.empty()) return true;
        const int fd = ::open(s.path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC | O_NOFOLLOW, 0600);
        if (fd >= 0) {
            constexpr std::string_view text = "{\n  \"masterSetup\": {\n    \"serverUrl\": \n}\n";
            [[maybe_unused]] const auto written = ::write(fd, text.data(), text.size());
            ::close(fd);
        }
        return true;
    }
    if (line == "setconfig normal") { s.mode = SaveMode::normal; return true; }
    if (line == "setconfig busy") { s.mode = SaveMode::busy; return true; }
    if (line == "setconfig foreign") { s.mode = SaveMode::foreign; return true; }
    if (line.rfind("setconfig delay ", 0) == 0) {
        s.mode = SaveMode::delay;
        s.delayMs = std::max(0, std::atoi(line.c_str() + 16));
        return true;
    }
    return false;
}

bool option(const std::string& name, const std::string& value) {
    if (name != "--config-dir") return false;
    if (configure(value)) return true;
    std::fprintf(stderr, "mock configuration store could not use %s\n", value.c_str());
    std::exit(2);
}

const MockExtensionRegistrar registrar(MockExtension{handleIpc, {}, classify, control, option});
const MockWizardRegistrar wizardRegistrar(handleWizardRequest);
}  // namespace
}  // namespace phpvpn::mock
