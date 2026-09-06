#include "wizard_reader.h"
#include "core/crypto/crypto_provider.h"

namespace phpvpn::tray {
bool WizardReader::request(WizardRequest request, HWND target, UINT message, std::uint64_t generation) {
    if (pending_) return false;
    auto crypto = createPlatformCryptoProvider();
    if (!crypto) return false;
    auto id = crypto.value()->randomUuidV4();
    if (!id) return false;
    request.requestId = id.value();
    auto encoded = WizardIpc::buildRequest(request);
    if (!encoded || !reader_.requestRaw(std::move(encoded).value(), 5000, target, message)) return false;
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
} // namespace phpvpn::tray
