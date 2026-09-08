#include "contracts_save_controller.h"
#include "window_utils.h"

namespace phpvpn::tray {
namespace {
constexpr wchar_t unknownText[] = L"The save result is unknown. The file may already have changed. Check the current configuration before saving again.";
void wipeJson(JsonValue& value) {
    if (value.isString()) { auto& text = const_cast<std::string&>(value.stringValue()); secureWipe(text.data(), text.size()); }
    else if (value.isObject()) for (auto& entry : value.objectValue()) wipeJson(entry.second);
    else if (value.isArray()) for (auto& item : const_cast<JsonArray&>(value.arrayValue())) wipeJson(item);
}
}
Result<std::size_t> contractsEnvelopeSize(std::string_view document) {
    auto encoded = IpcProtocol::buildRequest(IpcCommand::set_config, "12345678-1234-4234-9234-123456789abc", document);
    const auto size = encoded.size();
    secureWipe(encoded.data(), encoded.size());
    return size;
}
Status ContractsSaveController::load(CryptoProvider& crypto, std::string_view document, std::string path, std::string revision) {
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
ContractsStage ContractsSaveController::stage() const {
    return stage_ == ContractsStage::editing && model_ && model_->blockingErrors() ? ContractsStage::invalid : stage_;
}
ContractsLifecycleSnapshot ContractsSaveController::snapshot() const {
    return {model_ != nullptr, model_ && model_->dirty(), stage_ == ContractsStage::saving,
        stage_ == ContractsStage::saveUnknown, checker_.busy(), windowGeneration_, draftGeneration_,
        operationGeneration_, checkGeneration_, operation_ ? operation_->deadlineMs : 0};
}
void ContractsSaveController::changed() {
    if (stage_ == ContractsStage::editing) { ++draftGeneration_; notice_.clear(); }
}
Status ContractsSaveController::prepare(bool confirmedRepeat) {
    if (!model_ || prepared_) return fail(ErrorCode::busy);
    if (confirmedRepeat) {
        if (!canRepeat() || !model_->resumeEditing(model_->saveGeneration())) return fail(ErrorCode::busy);
        stage_ = ContractsStage::editing;
    }
    if (stage_ != ContractsStage::editing) return fail(ErrorCode::busy);
    auto candidate = model_->prepareSave(contractsEnvelopeSize);
    if (!candidate) { notice_ = widen(errorCodeDescription(candidate.code())); return candidate.error(); }
    prepared_ = true; repeatAllowed_ = false;
    return ok();
}
bool ContractsSaveController::dispatch(HWND target, UINT message) {
    if (!prepared_ || !model_ || !model_->candidate()) return false;
    prepared_ = false;
    operation_ = SaveOperation{windowGeneration_, draftGeneration_, ++operationGeneration_, GetTickCount64() + 30000};
    std::string payload(model_->candidate()->text());
    const auto started = writer_.requestSave(*operation_, payload, target, message);
    secureWipe(payload.data(), payload.size());
    if (!started) {
        model_->resumeEditing(model_->saveGeneration()); operation_.reset();
        notice_ = L"The save request could not be sent. Your draft has been kept.";
        return false;
    }
    stage_ = ContractsStage::saving; notice_ = L"Saving configuration...";
    return true;
}
ContractsEvent ContractsSaveController::poll() {
    if (stage_ == ContractsStage::saving) {
        if (auto result = writer_.takeSave(); result && operation_ && result->operation == *operation_) {
            operation_.reset();
            if (result->outcome == SaveOutcome::confirmed) return ContractsEvent::saved;
            if (result->outcome == SaveOutcome::unknown) {
                stage_ = ContractsStage::saveUnknown; notice_ = unknownText;
                return ContractsEvent::unknown;
            }
            model_->resumeEditing(model_->saveGeneration()); stage_ = ContractsStage::editing;
            notice_ = widen(errorCodeDescription(result->error));
            return ContractsEvent::rejected;
        }
    }
    if (auto result = checker_.take()) {
        struct Wipe { WizardReadResult& result; ~Wipe() { if (result.reply) wipeJson(result.reply->result); } } wipe{*result};
        if (!model_ || stage_ != ContractsStage::saveUnknown || result->generation != checkGeneration_) return ContractsEvent::none;
        ++checkGeneration_; repeatAllowed_ = false;
        if (!result->reply || result->reply->error) {
            const auto error = result->reply ? *result->reply->error : WizardIpc::safeError(result->reply.error());
            notice_ = widen(error.description) + L"\n" + widen(error.suggestedAction);
            if (error.configPath) notice_ += L"\n" + widen(*error.configPath);
            return ContractsEvent::checkFailed;
        }
        const auto& value = result->reply->result;
        const auto availability = value.find("availability")->stringValue();
        if (availability == "valid") {
            const auto* document = value.find("document");
            auto match = model_->candidate()->matchesDocument(document->stringValue());
            if (!match) { notice_ = widen(errorCodeDescription(match.code())); return ContractsEvent::checkFailed; }
            if (*match) { notice_ = L"The current configuration matches your changes."; return ContractsEvent::matched; }
        } else if (availability != "missing") {
            notice_ = L"The current configuration is " + widen(availability) + L". Resolve this condition before saving again.\n" +
                widen(value.find("configPath")->stringValue());
            return ContractsEvent::checkFailed;
        }
        repeatAllowed_ = true;
        notice_ = L"The current configuration differs. Your draft has been kept.";
        return ContractsEvent::different;
    }
    return ContractsEvent::none;
}
bool ContractsSaveController::check(HWND target, UINT message) {
    if (!model_ || stage_ != ContractsStage::saveUnknown || checker_.busy()) return false;
    WizardRequest request; request.command = WizardCommand::get_config_document;
    if (!checker_.request(request, target, message, ++checkGeneration_)) return false;
    repeatAllowed_ = false; notice_ = L"Checking current configuration...";
    return true;
}
void ContractsSaveController::close() {
    writer_.abandon(); checker_.abandon(); operation_.reset();
    if (model_) model_->close();
    model_.reset(); ++windowGeneration_; ++checkGeneration_;
    stage_ = ContractsStage::closed; prepared_ = repeatAllowed_ = false; notice_.clear();
}
} // namespace phpvpn::tray
