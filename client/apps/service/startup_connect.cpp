#include "startup_connect.h"
#include "core/base/logger.h"

namespace phpvpn {
void StartupConnect::tick(const Post& post) {
    if (!armed_.load() || cancelled_.load()) return;
    bool expected = false;
    if (!queued_.compare_exchange_strong(expected, true)) return;
    if (!post([this] { execute(); })) queued_.store(false);
}
void StartupConnect::execute() noexcept {
    const auto current = [this] { return !cancelled_.load() && token_ == engine_.startupControlToken() &&
        epoch_ == engine_.connectionIdentity().epoch; };
    if (!current()) return;
    try {
        auto snapshot = store_.read();
        if (!current()) return;
        if (!snapshot || !snapshot->document) {
            engine_.publishStartupError(token_, epoch_, snapshot ? Error(snapshot->reasonCode.value_or(ErrorCode::config_unreadable)) : snapshot.error());
            return;
        }
        auto document = Json::parse(*snapshot->document);
        if (!document) { engine_.publishStartupError(token_, epoch_, Error(ErrorCode::config_invalid_json)); return; }
        auto preference = readAutoConnectOnStartup(*document);
        if (!preference) { engine_.publishStartupError(token_, epoch_, preference.error()); return; }
        if (*preference && current()) (void)engine_.connectAtStartup(token_, epoch_);
    } catch (...) {
        PHPVPN_LOG_ERROR("service", "startup configuration callback failed");
        if (current()) engine_.publishStartupError(token_, epoch_, Error(ErrorCode::internal));
    }
}
}
