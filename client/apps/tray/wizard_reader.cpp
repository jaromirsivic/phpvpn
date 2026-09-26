#include "wizard_reader.h"
#include "core/crypto/crypto_provider.h"
#include "ipc_timeouts.h"

namespace phpvpn::tray {
void WizardReader::clearPending() {
    if (pending_) {
        for (auto* value : {&pending_->fields.serverUrl, &pending_->fields.serverSecret,
            &pending_->fields.virtualIp, &pending_->fields.accessKey}) secureWipe(value->data(), value->size());
        pending_.reset();
    }
}
bool WizardReader::request(WizardRequest request, HWND target, UINT message, std::uint64_t generation) {
    if (pending_) return false;
    auto crypto = createPlatformCryptoProvider();
    if (!crypto) return false;
    auto id = crypto.value()->randomUuidV4();
    if (!id) return false;
    request.requestId = id.value();
    auto encoded = WizardIpc::buildRequest(request);
    if (!encoded || !reader_.requestRaw(std::move(encoded).value(), kWizardRequestTimeoutMs, target, message)) return false;
    pending_ = std::move(request);
    generation_ = generation;
    return true;
}
std::optional<WizardReadResult> WizardReader::take() {
    auto response = reader_.take();
    if (!response || !pending_) return {};
    auto request = std::move(*pending_); pending_.reset();
    auto reply = response->empty() ? Result<WizardReply>(Error(ErrorCode::ipc_unavailable)) :
        WizardIpc::parseReply(request.command, request.requestId, *response);
    return WizardReadResult{std::move(request), std::move(reply), generation_};
}
void WizardStatusReader::request(HWND target, UINT message) {
    if (!requestId_.empty()) return;
    auto crypto = createPlatformCryptoProvider();
    if (!crypto) return;
    auto id = crypto.value()->randomUuidV4();
    if (!id) return;
    if (reader_.requestRaw(IpcProtocol::buildRequest(IpcCommand::get_status, *id), kStatusRequestTimeoutMs, target, message))
        requestId_ = std::move(*id);
}
std::optional<TrayStatus> WizardStatusReader::take() {
    auto response = reader_.take();
    if (!response) return {};
    const auto id = std::exchange(requestId_, {});
    const auto parsed = Json::parse(*response);
    const auto* replyId = parsed && parsed->isObject() ? parsed->find("requestId") : nullptr;
    if (!replyId || !replyId->isString() || replyId->stringValue() != id) return TrayStatus{};
    return parseStatusResponse(*response);
}
} // namespace phpvpn::tray
