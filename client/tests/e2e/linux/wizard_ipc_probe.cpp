// Wizard IPC exercise of the Linux service end-to-end test, not a unit test.
//
// Drives the real Wizard of an isolated phpvpn-service instance over its Unix
// control socket with the production core builders and parsers (wizard_ipc,
// ipc_protocol) and the production IpcSocketClient framing: get-config-state
// reports the expected POSIX config path, open, the three explicit Next checks
// (url, secret, access) against the real relay, finish with a commitId until
// the receipt is confirmed, an identical finish replays the same receipt,
// close, and get-config returns exactly the committed document. The script
// run-service-e2e.sh compares that document with the file at the POSIX path.
// With --expect-url-error the URL check must fail with that code instead, and
// the probe closes the session without the later checks.
//
//   phpvpn-wizard-ipc-probe --socket PATH --relay URL --config-path PATH
//                           [--virtual-ip 10.99.99.2] [--document-out FILE]
//                           [--expect-url-error CODE]
#include <chrono>
#include <cstdio>
#include <fstream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>

#include "core/config/config.h"
#include "core/crypto/crypto_provider.h"
#include "core/ipc/ipc_protocol.h"
#include "core/ipc/wizard_ipc.h"
#include "core/protocol/json.h"
#include "core/wizard/wizard_defaults.h"
#include "platform/linux/ipc_unix.h"

namespace {
using namespace phpvpn;
using Clock = std::chrono::steady_clock;

void check(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}
template <class T> T require(Result<T> result, const char* what) {
    if (!result) throw std::runtime_error(std::string(what) + ": " + result.error().name() + " " + result.error().message());
    return std::move(result).value();
}
void pass(const std::string& message) { std::printf("ok: %s\n", message.c_str()); std::fflush(stdout); }

class Probe {
public:
    explicit Probe(std::string socketPath) {
        IpcSocketClientOptions options;
        options.socketPath = std::move(socketPath);
        client_ = IpcSocketClient(options);
    }
    std::unique_ptr<CryptoProvider> crypto = require(createPlatformCryptoProvider(), "crypto");
    WizardRequest session;

    std::string uuid() { return require(crypto->randomUuidV4(), "uuid"); }
    std::string send(std::string_view request) { return require(client_.exchange(request, 10000), "IPC exchange"); }
    WizardReply wizard(WizardCommand command) {
        session.command = command;
        session.requestId = uuid();
        const auto encoded = require(WizardIpc::buildRequest(session), "Wizard request");
        auto reply = require(WizardIpc::parseReply(command, session.requestId, send(encoded)), "Wizard reply");
        if (reply.error) throw std::runtime_error(std::string("Wizard ") + WizardIpc::commandName(command) + " failed: " + reply.error->code);
        return reply;
    }
    void checkNext(WizardCommand command, WizardOperationKind kind) {
        const auto failure = attemptNext(command, kind);
        check(!failure, "Wizard operation failed: " + (failure ? failure->code : std::string()));
    }
    // The error of one Next check, nullopt when it succeeded.
    std::optional<WizardError> attemptNext(WizardCommand command, WizardOperationKind kind) {
        session.clientAttemptId = uuid();
        ++session.attemptSequence;
        auto started = wizard(command);
        const auto* operation = started.result.find("operationId");
        check(operation != nullptr && operation->isString(), "Next did not return an operationId");
        session.operationId = operation->stringValue();
        const auto deadline = Clock::now() + std::chrono::seconds(45);
        auto heartbeat = Clock::now() + std::chrono::seconds(15);
        for (;;) {
            check(Clock::now() < deadline, "Wizard Next exceeded the deadline, no retry issued");
            auto value = require(WizardIpc::parseOperation(wizard(WizardCommand::get_operation).result), "operation");
            check(value.operationId == session.operationId && value.kind == kind &&
                  value.clientAttemptId == session.clientAttemptId && value.attemptSequence == session.attemptSequence,
                  "Wizard operation correlation mismatch");
            if (value.state == WizardOperationState::succeeded) return std::nullopt;
            if (value.state == WizardOperationState::failed && value.error) return value.error;
            check(value.state == WizardOperationState::pending || value.state == WizardOperationState::running,
                  "Wizard operation failed: " + (value.error ? value.error->code : std::string("cancelled/interrupted")));
            if (Clock::now() >= heartbeat) { wizard(WizardCommand::keepalive); heartbeat += std::chrono::seconds(15); }
            std::this_thread::sleep_for(std::chrono::milliseconds(25));
        }
    }
    // get-config of the public IPC: the exact stored document, no envelope.
    std::string storedDocument() {
        return send(IpcProtocol::buildRequest(IpcCommand::get_config, uuid()));
    }

private:
    IpcSocketClient client_;
};

std::string stringMember(const JsonValue& object, const char* name) {
    const auto* value = object.find(name);
    check(value != nullptr && value->isString(), std::string("missing string member ") + name);
    return value->stringValue();
}

int run(int argc, char** argv) {
    std::string socketPath, relay, configPath, documentOut, expectUrlError, virtualIp = "10.99.99.2";
    for (int index = 1; index + 1 < argc; index += 2) {
        const std::string_view name = argv[index];
        const std::string value = argv[index + 1];
        if (name == "--socket") socketPath = value;
        else if (name == "--relay") relay = value;
        else if (name == "--config-path") configPath = value;
        else if (name == "--virtual-ip") virtualIp = value;
        else if (name == "--document-out") documentOut = value;
        else if (name == "--expect-url-error") expectUrlError = value;
        else throw std::runtime_error("unknown option " + std::string(name));
    }
    if (argc % 2 == 0 || socketPath.empty() || relay.empty() || configPath.empty()) {
        std::fputs("usage: phpvpn-wizard-ipc-probe --socket PATH --relay URL --config-path PATH "
                   "[--virtual-ip A.B.C.D] [--document-out FILE] [--expect-url-error CODE]\n", stderr);
        return 2;
    }

    Probe probe(socketPath);
    const auto state = probe.wizard(WizardCommand::get_config_state);
    check(stringMember(state.result, "configPath") == configPath,
          "get-config-state reports " + stringMember(state.result, "configPath") + ", expected " + configPath);
    pass("get-config-state reports the POSIX config path " + configPath +
         " (availability " + stringMember(state.result, "availability") + ")");

    auto view = require(WizardIpc::parseView(probe.wizard(WizardCommand::open).result), "view");
    check(view.fields.has_value(), "the Wizard view has no fields");
    probe.session.sessionId = view.sessionId;
    probe.session.serviceInstanceId = view.serviceInstanceId;
    probe.session.connectionEpoch = view.connectionEpoch;
    probe.session.fields = *view.fields;
    probe.session.fields.serverUrl = relay;
    probe.session.fields.serverSecret = std::string(kDefaultServerSecret);
    probe.session.fields.virtualIp = virtualIp;
    probe.session.fields.autoDetectIpAddressInWizard = false;
    probe.session.fields.accessKey = defaultAccessKey(virtualIp);
    pass("wizard-open");

    if (!expectUrlError.empty()) {
        // A relay that must be refused at the URL step, e.g. of another MAJOR version.
        const auto failure = probe.attemptNext(WizardCommand::check_url, WizardOperationKind::url);
        check(failure && failure->code == expectUrlError,
              "check url ended with " + (failure ? failure->code : std::string("success")) + ", expected " + expectUrlError);
        check(failure->field == std::optional<std::string>("serverUrl"), "the URL check failure is not bound to serverUrl");
        pass("check url reports " + failure->code + ": " + failure->description);
        probe.wizard(WizardCommand::close);
        std::puts("PASS wizard IPC probe");
        return 0;
    }

    probe.checkNext(WizardCommand::check_url, WizardOperationKind::url);
    pass("check url against the real relay");
    probe.checkNext(WizardCommand::check_secret, WizardOperationKind::secret);
    pass("check secret");
    probe.checkNext(WizardCommand::check_access, WizardOperationKind::access);
    pass("check access");

    probe.session.commitId = probe.uuid();
    probe.session.createDefaultContracts = true;
    probe.wizard(WizardCommand::finish);
    const auto deadline = Clock::now() + std::chrono::seconds(10);
    std::string revision;
    for (;;) {
        auto reply = probe.wizard(WizardCommand::get_commit);
        const auto commitState = stringMember(reply.result, "commitState");
        if (commitState == "confirmed") {
            const auto* receipt = reply.result.find("receipt");
            check(receipt != nullptr && receipt->isObject(), "confirmed commit without receipt");
            check(stringMember(*receipt, "commitId") == probe.session.commitId, "receipt commitId mismatch");
            revision = stringMember(*receipt, "revisionToken");
            break;
        }
        check(commitState == "pending" && Clock::now() < deadline, "finish ended " + commitState + ", no automatic retry");
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
    pass("finish confirmed with commitId " + probe.session.commitId);

    probe.wizard(WizardCommand::finish);  // same commitId and identical input
    const auto repeated = probe.wizard(WizardCommand::get_commit);
    check(stringMember(repeated.result, "commitState") == "confirmed" &&
          stringMember(*repeated.result.find("receipt"), "revisionToken") == revision,
          "an identical finish changed the confirmed receipt");
    pass("identical finish replays the same receipt");
    probe.wizard(WizardCommand::close);
    pass("wizard-close");

    const std::string document = probe.storedDocument();
    ConfigWarnings warnings;
    const auto stored = require(parseAndValidateConfig(document, warnings), "committed document");
    check(stored->virtualIp.toString() == virtualIp, "the committed document has another virtualIp");
    check(stored->serverUrl.original == relay, "the committed document has another serverUrl");
    pass("get-config returns the committed document (virtualIp " + virtualIp + ", " +
         std::to_string(stored->contracts.size()) + " default contracts)");
    if (!documentOut.empty()) {
        std::ofstream out(documentOut, std::ios::binary | std::ios::trunc);
        out << document;
        check(out.good(), "the document could not be written to " + documentOut);
    }
    std::puts("PASS wizard IPC probe");
    return 0;
}
}  // namespace

int main(int argc, char** argv) {
    try {
        return run(argc, argv);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAIL: %s\n", error.what());
        return 1;
    }
}
