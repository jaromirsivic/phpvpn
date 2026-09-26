#include "harness.h"
#include "contracts_fixture.h"
#include "../../apps/tray/contracts_save_machine.h"
#include "../../apps/tray/ipc_timeouts.h"
#include <optional>
#include <string>

// Portable coverage of the Contracts save state machine and save classification
// shared by the Win32 and GTK trays. No transport, window or platform crypto.
namespace {
using namespace phpvpn;
using namespace phpvpn::tray;
using contracts_test::document;
using contracts_test::ip;
constexpr char requestId[] = "12345678-1234-4234-8234-123456789abc";
constexpr char unknownText[] = "The save result is unknown. The file may already have changed. Check the current configuration before saving again.";

// ContractsModel::create does not use the provider; any use fails loudly.
class NoCrypto final : public CryptoProvider {
public:
    Result<Sha256Digest> sha256(std::span<const std::uint8_t>) override { return Error(ErrorCode::crypto_failed); }
    Result<Sha256Digest> hmacSha256(std::span<const std::uint8_t>, std::span<const std::uint8_t>) override {
        return Error(ErrorCode::crypto_failed);
    }
    Status aesGcmSeal(std::span<const std::uint8_t>, std::span<const std::uint8_t>, std::span<const std::uint8_t>,
        std::span<const std::uint8_t>, std::vector<std::uint8_t>&, GcmTag&) override { return fail(ErrorCode::crypto_failed); }
    Result<SecureBuffer> aesGcmOpen(std::span<const std::uint8_t>, std::span<const std::uint8_t>, std::span<const std::uint8_t>,
        std::span<const std::uint8_t>, std::span<const std::uint8_t>) override { return Error(ErrorCode::crypto_failed); }
    Status randomBytes(std::span<std::uint8_t>) override { return fail(ErrorCode::crypto_failed); }
};

SaveResult outcome(const SaveOperation& operation, SaveOutcome value, ErrorCode error) {
    SaveResult result; result.operation = operation; result.outcome = value; result.error = error;
    return result;
}
struct Sent { std::optional<SaveOperation> operation; std::string payload; unsigned calls = 0; };
ContractsSaveMachine::SaveSender recorder(Sent& sent, bool accept = true) {
    return [&sent, accept](const SaveOperation& operation, const std::string& payload) {
        ++sent.calls; sent.operation = operation; sent.payload = payload; return accept;
    };
}
// Five peers keep every model small.
const std::string& smallDocument() {
    static const std::string text = document("10.99.99.2", "10.99.99.0/29");
    return text;
}
bool loadAndEdit(ContractsSaveMachine& machine) {
    NoCrypto crypto;
    if (!machine.load(crypto, smallDocument(), "/etc/phpvpn/config.json", "rev-1")) return false;
    auto* model = machine.model();
    if (!model->setKey(ip("10.99.99.3"), std::string(64, 'a')) || !model->setEnabled(ip("10.99.99.3"), true)) return false;
    machine.changed();
    return true;
}
Result<WizardReply> configReply(std::string availability, std::optional<std::string> text = {}) {
    WizardReply reply;
    reply.result = JsonValue::makeObject({{"configPath", JsonValue::makeString("/etc/phpvpn/config.json")},
        {"availability", JsonValue::makeString(std::move(availability))},
        {"document", text ? JsonValue::makeString(*text) : JsonValue::makeNull()}});
    return reply;
}
// Dispatches the prepared draft and returns the operation handed to the transport.
SaveOperation dispatched(ContractsSaveMachine& machine, std::uint64_t nowMs) {
    Sent sent;
    PHPVPN_CHECK(machine.dispatch(nowMs, recorder(sent)));
    PHPVPN_CHECK_EQ(sent.calls, 1u);
    return sent.operation.value_or(SaveOperation{});
}
// Drives a loaded, edited machine into saveUnknown.
bool toUnknown(ContractsSaveMachine& machine) {
    if (!loadAndEdit(machine) || !machine.prepare(false, false)) return false;
    const auto operation = dispatched(machine, 10);
    return machine.saveFinished(outcome(operation, SaveOutcome::unknown, ErrorCode::timeout)) == ContractsEvent::unknown;
}
std::uint64_t startCheck(ContractsSaveMachine& machine) {
    std::uint64_t generation = 0;
    PHPVPN_CHECK(machine.check(false, [&](std::uint64_t value) { generation = value; return true; }));
    return generation;
}
}

PHPVPN_TEST(ContractsSaveMachine, ConfirmedSaveCarriesIdentityAndDeadline) {
    ContractsSaveMachine machine;
    PHPVPN_CHECK_EQ(machine.stage(), ContractsStage::closed);
    PHPVPN_CHECK(loadAndEdit(machine));
    PHPVPN_CHECK_EQ(machine.stage(), ContractsStage::editing);
    auto snapshot = machine.snapshot(false);
    PHPVPN_CHECK(snapshot.open && snapshot.dirty && !snapshot.saving && !snapshot.unknown && !snapshot.checking);
    PHPVPN_CHECK_EQ(snapshot.windowGeneration, 1u);
    PHPVPN_CHECK_EQ(snapshot.draftGeneration, 2u);
    PHPVPN_CHECK(machine.snapshot(true).checking);

    PHPVPN_CHECK(machine.prepare(false, false).ok());
    PHPVPN_CHECK(machine.model()->frozen());
    PHPVPN_CHECK(!machine.prepare(false, false).ok());
    Sent sent;
    PHPVPN_CHECK(machine.dispatch(1000, recorder(sent)));
    PHPVPN_CHECK_EQ(sent.calls, 1u);
    PHPVPN_CHECK(sent.operation == (SaveOperation{1, 2, 1, 1000 + kContractsSaveDeadlineMs}));
    PHPVPN_CHECK_EQ(sent.payload, std::string(machine.model()->candidate()->text()));
    PHPVPN_CHECK_EQ(machine.stage(), ContractsStage::saving);
    PHPVPN_CHECK(machine.saving());
    PHPVPN_CHECK_EQ(machine.notice(), "Saving configuration...");
    snapshot = machine.snapshot(false);
    PHPVPN_CHECK(snapshot.saving);
    PHPVPN_CHECK_EQ(snapshot.operationGeneration, 1u);
    PHPVPN_CHECK_EQ(snapshot.deadlineMs, 1000u + kContractsSaveDeadlineMs);
    // A second dispatch needs a new prepare.
    PHPVPN_CHECK(!machine.dispatch(1000, recorder(sent)));
    PHPVPN_CHECK_EQ(sent.calls, 1u);
    // Changes during saving do not advance the draft.
    machine.changed();
    PHPVPN_CHECK_EQ(machine.snapshot(false).draftGeneration, 2u);

    auto foreign = *sent.operation; ++foreign.operationGeneration;
    PHPVPN_CHECK_EQ(machine.saveFinished(outcome(foreign, SaveOutcome::confirmed, ErrorCode::none)), ContractsEvent::none);
    PHPVPN_CHECK_EQ(machine.stage(), ContractsStage::saving);
    PHPVPN_CHECK_EQ(machine.saveFinished(outcome(*sent.operation, SaveOutcome::confirmed, ErrorCode::none)), ContractsEvent::saved);
    PHPVPN_CHECK_EQ(machine.stage(), ContractsStage::saved);
    PHPVPN_CHECK_EQ(machine.notice(), "Changes saved. Checking connection status...");
    snapshot = machine.snapshot(false);
    PHPVPN_CHECK(snapshot.open && !snapshot.dirty && !snapshot.saving);
    PHPVPN_CHECK_EQ(snapshot.deadlineMs, 0u);
    // The same result again is stale.
    PHPVPN_CHECK_EQ(machine.saveFinished(outcome(*sent.operation, SaveOutcome::confirmed, ErrorCode::none)), ContractsEvent::none);
}

PHPVPN_TEST(ContractsSaveMachine, RefusedDispatchAndRejectionKeepTheDraft) {
    ContractsSaveMachine machine;
    Sent sent;
    PHPVPN_CHECK(!machine.dispatch(5, recorder(sent)));
    PHPVPN_CHECK(loadAndEdit(machine));
    PHPVPN_CHECK(!machine.dispatch(5, recorder(sent)));
    PHPVPN_CHECK_EQ(sent.calls, 0u);

    PHPVPN_CHECK(machine.prepare(false, false).ok());
    PHPVPN_CHECK(!machine.dispatch(5, recorder(sent, false)));
    PHPVPN_CHECK_EQ(sent.calls, 1u);
    PHPVPN_CHECK_EQ(machine.stage(), ContractsStage::editing);
    PHPVPN_CHECK(!machine.model()->frozen());
    PHPVPN_CHECK_EQ(machine.notice(), "The save request could not be sent. Your draft has been kept.");
    PHPVPN_CHECK_EQ(machine.snapshot(false).operationGeneration, 1u);
    PHPVPN_CHECK_EQ(machine.snapshot(false).deadlineMs, 0u);

    machine.changed();
    PHPVPN_CHECK(machine.notice().empty());
    PHPVPN_CHECK(machine.prepare(false, false).ok());
    auto operation = dispatched(machine, 7);
    PHPVPN_CHECK_EQ(operation.operationGeneration, 2u);
    PHPVPN_CHECK_EQ(operation.draftGeneration, 3u);
    PHPVPN_CHECK_EQ(machine.saveFinished(outcome(operation, SaveOutcome::rejected, ErrorCode::config_invalid_json)),
        ContractsEvent::rejected);
    PHPVPN_CHECK_EQ(machine.stage(), ContractsStage::editing);
    PHPVPN_CHECK(!machine.model()->frozen());
    PHPVPN_CHECK_EQ(machine.notice(), std::string(errorCodeDescription(ErrorCode::config_invalid_json)));

    // notSent takes the same resume path as a proven rejection.
    PHPVPN_CHECK(machine.prepare(false, false).ok());
    operation = dispatched(machine, 9);
    PHPVPN_CHECK_EQ(machine.saveFinished(outcome(operation, SaveOutcome::notSent, ErrorCode::ipc_unavailable)),
        ContractsEvent::rejected);
    PHPVPN_CHECK_EQ(machine.stage(), ContractsStage::editing);
    PHPVPN_CHECK_EQ(machine.notice(), std::string(errorCodeDescription(ErrorCode::ipc_unavailable)));
}

PHPVPN_TEST(ContractsSaveMachine, UnknownRequiresCheckBeforeRepeat) {
    ContractsSaveMachine machine;
    PHPVPN_CHECK(toUnknown(machine));
    PHPVPN_CHECK_EQ(machine.stage(), ContractsStage::saveUnknown);
    PHPVPN_CHECK_EQ(machine.notice(), unknownText);
    PHPVPN_CHECK(machine.snapshot(false).unknown);
    PHPVPN_CHECK(machine.model()->frozen());
    PHPVPN_CHECK(!machine.canRepeat(false));
    PHPVPN_CHECK(!machine.prepare(true, false).ok());
    PHPVPN_CHECK(!machine.prepare(false, false).ok());

    // A busy check channel or a refused request starts nothing.
    unsigned sends = 0;
    PHPVPN_CHECK(!machine.check(true, [&](std::uint64_t) { ++sends; return true; }));
    PHPVPN_CHECK_EQ(sends, 0u);
    const auto before = machine.snapshot(false).checkGeneration;
    PHPVPN_CHECK(!machine.check(false, [&](std::uint64_t) { ++sends; return false; }));
    PHPVPN_CHECK_EQ(sends, 1u);
    PHPVPN_CHECK_EQ(machine.notice(), unknownText);
    // The refused request still consumed its generation, as the Win32 channel did.
    PHPVPN_CHECK_EQ(machine.snapshot(false).checkGeneration, before + 1);

    const auto generation = startCheck(machine);
    PHPVPN_CHECK_EQ(generation, before + 2);
    PHPVPN_CHECK_EQ(machine.notice(), "Checking current configuration...");
    PHPVPN_CHECK_EQ(machine.checkFinished(generation - 1, configReply("missing")), ContractsEvent::none);
    PHPVPN_CHECK_EQ(machine.checkFinished(generation, configReply("missing")), ContractsEvent::different);
    PHPVPN_CHECK_EQ(machine.notice(), "The current configuration differs. Your draft has been kept.");
    PHPVPN_CHECK(machine.canRepeat(false));
    PHPVPN_CHECK(!machine.canRepeat(true));
    // The consumed generation makes a duplicate reply stale.
    PHPVPN_CHECK_EQ(machine.checkFinished(generation, configReply("missing")), ContractsEvent::none);

    PHPVPN_CHECK(!machine.prepare(true, true).ok());
    PHPVPN_CHECK(machine.prepare(true, false).ok());
    const auto operation = dispatched(machine, 20);
    PHPVPN_CHECK_EQ(operation.operationGeneration, 2u);
    PHPVPN_CHECK_EQ(machine.saveFinished(outcome(operation, SaveOutcome::confirmed, ErrorCode::none)), ContractsEvent::saved);
}

PHPVPN_TEST(ContractsSaveMachine, CheckOutcomesAndNotices) {
    {
        ContractsSaveMachine machine;
        PHPVPN_CHECK(toUnknown(machine));
        const std::string candidate(machine.model()->candidate()->text());
        PHPVPN_CHECK_EQ(machine.checkFinished(startCheck(machine), configReply("valid", candidate)), ContractsEvent::matched);
        PHPVPN_CHECK_EQ(machine.notice(), "The current configuration matches your changes.");
        PHPVPN_CHECK(!machine.canRepeat(false));
        PHPVPN_CHECK_EQ(machine.stage(), ContractsStage::saveUnknown);
    }
    {
        ContractsSaveMachine machine;
        PHPVPN_CHECK(toUnknown(machine));
        PHPVPN_CHECK_EQ(machine.checkFinished(startCheck(machine), configReply("valid", smallDocument())), ContractsEvent::different);
        PHPVPN_CHECK(machine.canRepeat(false));
    }
    {
        ContractsSaveMachine machine;
        PHPVPN_CHECK(toUnknown(machine));
        PHPVPN_CHECK_EQ(machine.checkFinished(startCheck(machine), configReply("valid", "{")), ContractsEvent::checkFailed);
        PHPVPN_CHECK(!machine.notice().empty());
        PHPVPN_CHECK(!machine.canRepeat(false));
    }
    {
        ContractsSaveMachine machine;
        PHPVPN_CHECK(toUnknown(machine));
        PHPVPN_CHECK_EQ(machine.checkFinished(startCheck(machine), configReply("invalid")), ContractsEvent::checkFailed);
        PHPVPN_CHECK_EQ(machine.notice(), "The current configuration is invalid. Resolve this condition before saving again.\n"
            "/etc/phpvpn/config.json");
    }
    {
        ContractsSaveMachine machine;
        PHPVPN_CHECK(toUnknown(machine));
        WizardReply reply;
        reply.error = WizardError{};
        reply.error->description = "Configuration unreadable.";
        reply.error->suggestedAction = "Inspect the file.";
        reply.error->configPath = "/etc/phpvpn/config.json";
        PHPVPN_CHECK_EQ(machine.checkFinished(startCheck(machine), Result<WizardReply>(reply)), ContractsEvent::checkFailed);
        PHPVPN_CHECK_EQ(machine.notice(), "Configuration unreadable.\nInspect the file.\n/etc/phpvpn/config.json");
        PHPVPN_CHECK(!machine.canRepeat(false));
    }
    {
        ContractsSaveMachine machine;
        PHPVPN_CHECK(toUnknown(machine));
        const auto safe = WizardIpc::safeError(Error(ErrorCode::ipc_unavailable));
        PHPVPN_CHECK_EQ(machine.checkFinished(startCheck(machine), Result<WizardReply>(Error(ErrorCode::ipc_unavailable))),
            ContractsEvent::checkFailed);
        PHPVPN_CHECK_EQ(machine.notice(), safe.description + "\n" + safe.suggestedAction);
    }
}

PHPVPN_TEST(ContractsSaveMachine, InvalidDraftLoadFailureAndClose) {
    ContractsSaveMachine machine;
    NoCrypto crypto;
    PHPVPN_CHECK(!machine.load(crypto, "{}", {}, {}).ok());
    PHPVPN_CHECK_EQ(machine.stage(), ContractsStage::closed);
    PHPVPN_CHECK(machine.model() == nullptr);
    PHPVPN_CHECK(!machine.snapshot(false).open);

    PHPVPN_CHECK(machine.load(crypto, smallDocument(), {}, {}).ok());
    PHPVPN_CHECK_EQ(machine.load(crypto, smallDocument(), {}, {}).code(), ErrorCode::busy);
    PHPVPN_CHECK(machine.model()->setEnabled(ip("10.99.99.4"), true).ok());
    machine.changed();
    PHPVPN_CHECK_EQ(machine.stage(), ContractsStage::invalid);
    PHPVPN_CHECK(!machine.prepare(false, false).ok());
    PHPVPN_CHECK_EQ(machine.notice(), std::string(errorCodeDescription(ErrorCode::config_invalid_key)));
    PHPVPN_CHECK(machine.model()->setEnabled(ip("10.99.99.4"), false).ok());
    machine.changed();
    PHPVPN_CHECK_EQ(machine.stage(), ContractsStage::editing);

    PHPVPN_CHECK(machine.model()->setKey(ip("10.99.99.4"), std::string(64, 'b')).ok());
    PHPVPN_CHECK(machine.model()->setEnabled(ip("10.99.99.4"), true).ok());
    machine.changed();
    PHPVPN_CHECK(machine.prepare(false, false).ok());
    const auto operation = dispatched(machine, 1);
    const auto windowGeneration = machine.snapshot(false).windowGeneration;
    const auto checkGeneration = machine.snapshot(false).checkGeneration;
    machine.close();
    PHPVPN_CHECK_EQ(machine.stage(), ContractsStage::closed);
    PHPVPN_CHECK(machine.model() == nullptr);
    PHPVPN_CHECK(machine.notice().empty());
    const auto closed = machine.snapshot(false);
    PHPVPN_CHECK(!closed.open && !closed.saving && !closed.dirty);
    PHPVPN_CHECK_EQ(closed.windowGeneration, windowGeneration + 1);
    PHPVPN_CHECK_EQ(closed.checkGeneration, checkGeneration + 1);
    PHPVPN_CHECK_EQ(machine.saveFinished(outcome(operation, SaveOutcome::confirmed, ErrorCode::none)), ContractsEvent::none);
    PHPVPN_CHECK_EQ(machine.checkFinished(closed.checkGeneration, configReply("missing")), ContractsEvent::none);
    PHPVPN_CHECK(!machine.check(false, [](std::uint64_t) { return true; }));

    // Reopening yields a new window identity, so an old operation can never match.
    PHPVPN_CHECK(loadAndEdit(machine));
    PHPVPN_CHECK_EQ(machine.snapshot(false).windowGeneration, windowGeneration + 2);
}

PHPVPN_TEST(ContractsSaveMachine, EnvelopeSizeMatchesTheSentRequest) {
    const auto size = contractsEnvelopeSize(smallDocument());
    PHPVPN_REQUIRE_OK(size);
    PHPVPN_CHECK_EQ(*size, IpcProtocol::buildRequest(IpcCommand::set_config, requestId, smallDocument()).size());
}

PHPVPN_TEST(SaveResponse, PortableClassificationOfTheSaveEnvelope) {
    const SaveOperation op{1, 2, 3, 100};
    const auto ack = IpcProtocol::buildAcknowledgement(requestId);
    const auto confirmed = classifySaveResponse(op, requestId, ack, true, 99);
    PHPVPN_CHECK_EQ(confirmed.outcome, SaveOutcome::confirmed);
    PHPVPN_CHECK_EQ(confirmed.error, ErrorCode::none);
    PHPVPN_CHECK(confirmed.operation == op);
    PHPVPN_CHECK_EQ(classifySaveResponse(op, requestId, ack, true, 100).outcome, SaveOutcome::unknown);
    PHPVPN_CHECK_EQ(classifySaveResponse({1, 2, 3, 0}, requestId, ack, true, 0).outcome, SaveOutcome::unknown);
    PHPVPN_CHECK_EQ(classifySaveResponse(op, "not-a-uuid", ack, true, 99).outcome, SaveOutcome::unknown);
    const auto notSent = classifySaveResponse(op, requestId, {}, false, 120);
    PHPVPN_CHECK_EQ(notSent.outcome, SaveOutcome::notSent);
    PHPVPN_CHECK_EQ(notSent.error, ErrorCode::ipc_unavailable);
    const auto rejected = classifySaveResponse(op, requestId, IpcProtocol::buildError(requestId, Error(ErrorCode::config_invalid_cidr)), true, 99);
    PHPVPN_CHECK_EQ(rejected.outcome, SaveOutcome::rejected);
    PHPVPN_CHECK_EQ(rejected.error, ErrorCode::config_invalid_cidr);
    PHPVPN_CHECK(rejected.validationDescription.empty());
    PHPVPN_CHECK_EQ(classifySaveResponse(op, requestId, IpcProtocol::buildError(requestId, Error(ErrorCode::config_write_failed)),
        true, 99).outcome, SaveOutcome::unknown);
    PHPVPN_CHECK_EQ(classifySaveResponse(op, requestId, "{}", true, 99).outcome, SaveOutcome::unknown);
}
