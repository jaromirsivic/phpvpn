#include "core/protocol/server_version.h"

namespace phpvpn {

static_assert(semverMajor(PHPVPN_CLIENT_VERSION).has_value(), "PHPVPN_CLIENT_VERSION must start with a SemVer MAJOR");

std::string displayableServerVersion(std::string_view version) {
    if (version.empty() || version.size() > kMaxDisplayedServerVersionLength) return "unrecognised";
    for (const char c : version) {
        const bool allowed = (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                             c == '.' || c == '+' || c == '-';
        if (!allowed) return "unrecognised";
    }
    return std::string(version);
}

Status requireCompatibleServerVersion(std::string_view serverVersion, std::string_view clientVersion) {
    const auto server = semverMajor(serverVersion);
    const auto client = semverMajor(clientVersion);
    if (server && client && *server == *client) return ok();
    return fail(ErrorCode::server_version_incompatible, displayableServerVersion(serverVersion));
}

std::string serverVersionMismatchText(const Error& error) {
    return "Client version " PHPVPN_CLIENT_VERSION ", server version " +
           (error.detail.empty() ? std::string("unrecognised") : displayableServerVersion(error.detail)) + ".";
}

}  // namespace phpvpn
