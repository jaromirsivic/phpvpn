#include "contracts_save_controller.h"
#include "window_utils.h"

namespace phpvpn::tray {
namespace {
void wipeJson(JsonValue& value) {
    if (value.isString()) { auto& text = const_cast<std::string&>(value.stringValue()); secureWipe(text.data(), text.size()); }
    else if (value.isObject()) for (auto& entry : value.objectValue()) wipeJson(entry.second);
    else if (value.isArray()) for (auto& item : const_cast<JsonArray&>(value.arrayValue())) wipeJson(item);
}
}
bool ContractsSaveController::dispatch(HWND target, UINT message) {
    return machine_.dispatch(GetTickCount64(), [&](const SaveOperation& operation, const std::string& payload) {
        return writer_.requestSave(operation, payload, target, message);
    });
}
ContractsEvent ContractsSaveController::poll() {
    if (machine_.saving()) {
        if (auto result = writer_.takeSave()) {
            if (const auto event = machine_.saveFinished(*result); event != ContractsEvent::none) return event;
        }
    }
    if (auto result = checker_.take()) {
        struct Wipe { WizardReadResult& result; ~Wipe() { if (result.reply) wipeJson(result.reply->result); } } wipe{*result};
        return machine_.checkFinished(result->generation, result->reply);
    }
    return ContractsEvent::none;
}
bool ContractsSaveController::check(HWND target, UINT message) {
    return machine_.check(checker_.busy(), [&](std::uint64_t generation) {
        WizardRequest request; request.command = WizardCommand::get_config_document;
        return checker_.request(request, target, message, generation);
    });
}
const std::wstring& ContractsSaveController::notice() const {
    notice_ = widen(machine_.notice());
    return notice_;
}
void ContractsSaveController::close() {
    writer_.abandon(); checker_.abandon(); machine_.close();
}
} // namespace phpvpn::tray
