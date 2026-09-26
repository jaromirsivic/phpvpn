// Runtime options of the GTK tray that do not come from argv.
//
// TEST ONLY: PHPVPN_TRAY_TEST_SOCKET selects a private IPC socket (absolute path)
// and PHPVPN_TRAY_TEST_SERVER_UID the uid the socket peer must run as (default 0).
// They exist for the development mock service in tests/linux/tray and are never
// set by packaging, autostart or the desktop launcher. Production always uses
// kIpcSocketPath with a root peer.
//
// PHPVPN_TRAY_ICON_THEME_PATH points a non-installed development run at a
// directory that contains hicolor/<n>x<n>/apps/phpvpn*.png.
#pragma once
#include <sys/types.h>

#include <charconv>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace phpvpn::tray {
struct TrayOptions {
    std::string socketPath;  // empty: kIpcSocketPath
    uid_t expectedServerUid = 0;
    std::string iconThemePath;  // empty: installed icon theme only
    bool testEndpoint = false;
};

// getenv-like lookup, injected for tests. Returns nullopt for an invalid test setting;
// the tray then refuses to start instead of silently using another endpoint.
inline std::optional<TrayOptions> trayOptionsFromEnvironment(
    const std::function<const char*(const char*)>& lookup) {
    TrayOptions options;
    const char* socket = lookup("PHPVPN_TRAY_TEST_SOCKET");
    const char* uid = lookup("PHPVPN_TRAY_TEST_SERVER_UID");
    if (socket && *socket) {
        const std::string_view path(socket);
        if (path.front() != '/' || path.size() >= 108) return std::nullopt;  // sockaddr_un::sun_path
        options.socketPath = std::string(path);
        options.testEndpoint = true;
    }
    if (uid && *uid) {
        // A peer uid without a private socket would weaken the production check.
        if (!options.testEndpoint) return std::nullopt;
        const std::string_view text(uid);
        unsigned long long value = 0;
        const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
        if (error != std::errc{} || end != text.data() + text.size() ||
            value > static_cast<unsigned long long>(static_cast<uid_t>(-1) - 1)) return std::nullopt;
        options.expectedServerUid = static_cast<uid_t>(value);
    }
    if (const char* icons = lookup("PHPVPN_TRAY_ICON_THEME_PATH"); icons && *icons) {
        if (*icons != '/') return std::nullopt;
        options.iconThemePath = icons;
    }
    return options;
}

// Exact argv matches, same semantics as requestsInstallationNotice.
inline bool requestsShowWindow(int argc, const char* const* argv) {
    for (int i = 1; i < argc; ++i)
        if (std::string_view(argv[i]) == "--show-window") return true;
    return false;
}
}  // namespace phpvpn::tray
