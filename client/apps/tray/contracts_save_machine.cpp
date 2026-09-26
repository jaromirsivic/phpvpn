#include "contracts_save_machine.h"
#include "ipc_timeouts.h"
#include "core/base/secure_buffer.h"
#include "core/ipc/ipc_protocol.h"

namespace phpvpn::tray {
namespace {
constexpr char unknownText[] = "The save result is unknown. The file may already have changed. Check the current configuration before saving again.";
}
Result<std::size_t> contractsEnvelopeSize(std::string_view document) {
    auto encoded = IpcProtocol::buildRequest(IpcCommand::set_config, "12345678-1234-4234-9234-123456789abc", document);
    const auto size = encoded.size();
    secureWipe(encoded.data(), encoded.size());
    return size;
}
Status ContractsSaveMachine::load(CryptoProvider& crypto, std::string_view document, std::string path, std::string revision) {
    if (model_) return fail(ErrorCode::busy);
    stage_ = ContractsStage::loading;
    auto created = ContractsModel::create(crypto, document, std::move(path), std::move(revision));
    if (!created) { stage_ = ContractsStage::closed; return created.error(); }
    model_ = std::move(created).value();
    ++windowGeneration_; ++draftGeneration_;
    stage_ = ContractsStage::editing;
    notice_.clear(); repeatAllowed_ = prepared_ = false;
    return ok();
}
ContractsStage ContractsSaveMachine::stage() const {
    return stage_ == ContractsStage::editing && model_ && model_->blockingErrors() ? ContractsStage::invalid : stage_;
}
ContractsLifecycleSnapshot ContractsSaveMachine::snapshot(bool checking) const {
    return {model_ != nullptr, model_ && model_->dirty() && stage_ != ContractsStage::saved, stage_ == ContractsStage::saving,
        stage_ == ContractsStage::saveUnknown, checking, windowGeneration_, draftGeneration_,
        operationGeneration_, checkGeneration_, operation_ ? operation_->deadlineMs : 0};
}
void ContractsSaveMachine::changed() {
    if (stage_ == ContractsStage::editing) { ++draftGeneration_; notice_.clear(); }
}
Status ContractsSaveMachine::prepare(bool confirmedRepeat, bool checking) {
    if (!model_ || prepared_) return fail(ErrorCode::busy);
    if (confirmedRepeat) {
        if (!canRepeat(checking) || !model_->resumeEditing(model_->saveGeneration())) return fail(ErrorCode::busy);
        stage_ = ContractsStage::editing;
    }
    if (stage_ != ContractsStage::editing) return fail(ErrorCode::busy);
    auto candidate = model_->prepareSave(contractsEnvelopeSize);
    if (!candidate) { notice_ = errorCodeDescription(candidate.code()); return candidate.error(); }
    prepared_ = true; repeatAllowed_ = false;
    return ok();
}
bool ContractsSaveMachine::dispatch(std::uint64_t nowMs, const SaveSender& send) {
    if (!prepared_ || !model_ || !model_->candidate()) return false;
    prepared_ = false;
    operation_ = SaveOperation{windowGeneration_, draftGeneration_, ++operationGeneration_, nowMs + kContractsSaveDeadlineMs};
    bool started = false;
    {
        std::string payload(model_->candidate()->text());
        struct Wipe { std::string& text; ~Wipe() { secureWipe(text.data(), text.size()); } } wipe{payload};
        started = send && send(*operation_, payload);
    }
    if (!started) {
        model_->resumeEditing(model_->saveGeneration()); operation_.reset();
        notice_ = "The save request could not be sent. Your draft has been kept.";
        return false;
    }
    stage_ = ContractsStage::saving; notice_ = "Saving configuration...";
    return true;
}
ContractsEvent ContractsSaveMachine::saveFinished(const SaveResult& result) {
    if (stage_ != ContractsStage::saving || !operation_ || result.operation != *operation_) return ContractsEvent::none;
    operation_.reset();
    if (result.outcome == SaveOutcome::confirmed) {
        stage_ = ContractsStage::saved;
        notice_ = "Changes saved. Checking connection status...";
        return ContractsEvent::saved;
    }
    if (result.outcome == SaveOutcome::unknown) {
        stage_ = ContractsStage::saveUnknown; notice_ = unknownText;
        return ContractsEvent::unknown;
    }
    model_->resumeEditing(model_->saveGeneration()); stage_ = ContractsStage::editing;
    notice_ = errorCodeDescription(result.error);
    return ContractsEvent::rejected;
}
bool ContractsSaveMachine::check(bool checking, const CheckSender& send) {
    if (!model_ || stage_ != ContractsStage::saveUnknown || checking || !send) return false;
    if (!send(++checkGeneration_)) return false;
    repeatAllowed_ = false; notice_ = "Checking current configuration...";
    return true;
}
ContractsEvent ContractsSaveMachine::checkFinished(std::uint64_t checkGeneration, const Result<WizardReply>& reply) {
    if (!model_ || stage_ != ContractsStage::saveUnknown || checkGeneration != checkGeneration_) return ContractsEvent::none;
    ++checkGeneration_; repeatAllowed_ = false;
    if (!reply || reply->error) {
        const auto error = reply ? *reply->error : WizardIpc::safeError(reply.error());
        notice_ = error.description + "\n" + error.suggestedAction;
        if (error.configPath) notice_ += "\n" + *error.configPath;
        return ContractsEvent::checkFailed;
    }
    const auto& value = reply->result;
    const auto availability = value.find("availability")->stringValue();
    if (availability == "valid") {
        const auto* document = value.find("document");
        auto match = model_->candidate()->matchesDocument(document->stringValue());
        if (!match) { notice_ = errorCodeDescription(match.code()); return ContractsEvent::checkFailed; }
        if (*match) { notice_ = "The current configuration matches your changes."; return ContractsEvent::matched; }
    } else if (availability != "missing") {
        notice_ = "The current configuration is " + availability + ". Resolve this condition before saving again.\n" +
            value.find("configPath")->stringValue();
        return ContractsEvent::checkFailed;
    }
    repeatAllowed_ = true;
    notice_ = "The current configuration differs. Your draft has been kept.";
    return ContractsEvent::different;
}
void ContractsSaveMachine::close() {
    operation_.reset();
    if (model_) model_->close();
    model_.reset(); ++windowGeneration_; ++checkGeneration_;
    stage_ = ContractsStage::closed; prepared_ = repeatAllowed_ = false; notice_.clear();
}
} // namespace phpvpn::tray
