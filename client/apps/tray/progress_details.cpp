#include "progress_details.h"
#include "core/config/config_document.h"

namespace phpvpn::tray {
ProgressDetails consumeProgressConfig(std::string document) {
    struct Wipe {
        std::string &text;
        ~Wipe() { secureWipe(text.data(), text.size()); }
    } wipe{document};
    if (document.size() > kMaxConfigFileBytes || !isStrictUtf8(document))
        return {};
    ConfigWarnings warnings;
    auto values = validateConfigValues(document, warnings, false);
    if (!values)
        return {};
    Wipe raw{values->rawJson}, secret{values->serverSecret};
    return {std::move(values->serverUrl.original), values->virtualIp->toString(), true};
    // Shared validator temporaries are destroyed on return. Key buffers wipe
    // through SecureBuffer. This is best effort, not an allocator/OS guarantee.
}
} // namespace phpvpn::tray
