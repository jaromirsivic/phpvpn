// Launch arguments and runtime options of the macOS tray. GUI-free, unit tested.
//
// Arguments: the LaunchAgent starts the tray without arguments (status item only,
// first get-status, never Connect). --show-window shows the control window of the
// running instance, --installation-complete shows the installation notice and --help
// prints the usage and exits before any AppKit or IPC work. Other arguments are
// ignored, as LaunchServices and Xcode add their own (-psn_..., -NSDocument...).
//
// TEST ONLY: PHPVPN_TRAY_TEST_SOCKET selects a private IPC socket (absolute path)
// and PHPVPN_TRAY_TEST_SERVER_UID the uid the socket peer must run as (default 0).
// They exist for isolated tests and are never set by packaging or the LaunchAgent.
// Production always uses kIpcSocketPath with a root peer.
#pragma once
#include <sys/types.h>

#include <charconv>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>

namespace phpvpn::tray {
inline constexpr std::string_view kTrayBundleIdentifier = "com.pilandrome.phpvpn-tray";
// Distributed notifications a second launch posts to the primary instance.
inline constexpr std::string_view kShowWindowNotification = "com.pilandrome.phpvpn-tray.show-window";
inline constexpr std::string_view kInstallationCompleteNotification =
    "com.pilandrome.phpvpn-tray.installation-complete";

inline constexpr std::string_view kTrayUsage =
    "Usage: phpvpn-tray [--show-window] [--installation-complete] [--help]\n"
    "\n"
    "PHPVPN menu bar tray. It only talks to the PHPVPN service over its local IPC socket;\n"
    "quitting the tray never disconnects the VPN.\n"
    "\n"
    "  --show-window            show the PHPVPN control window of the running tray\n"
    "  --installation-complete  show the installation notice\n"
    "  --help                   print this help and exit\n"
    "\n"
    "One tray runs per login session; a second launch forwards its arguments to it.\n";

struct LaunchRequest {
    bool showWindow = false;
    bool installationComplete = false;
    bool help = false;
};

// Exact matches only (same semantics as the Windows and Linux trays). argv[0] is the
// program name.
inline LaunchRequest parseLaunchArguments(int argc, const char* const* argv) {
    LaunchRequest request;
    for (int i = 1; i < argc; ++i) {
        if (!argv[i]) continue;
        const std::string_view argument(argv[i]);
        if (argument == "--show-window") request.showWindow = true;
        else if (argument == "--installation-complete") request.installationComplete = true;
        else if (argument == "--help" || argument == "-h") request.help = true;
    }
    return request;
}

struct TrayOptions {
    std::string socketPath;  // empty: kIpcSocketPath
    uid_t expectedServerUid = 0;
    bool testEndpoint = false;
};

// getenv-like lookup, injected for tests. Returns nullopt for an invalid test setting;
// the tray then refuses to start instead of silently using another endpoint.
inline std::optional<TrayOptions> trayOptionsFromEnvironment(const std::function<const char*(const char*)>& lookup) {
    TrayOptions options;
    const char* socket = lookup("PHPVPN_TRAY_TEST_SOCKET");
    const char* uid = lookup("PHPVPN_TRAY_TEST_SERVER_UID");
    if (socket && *socket) {
        const std::string_view path(socket);
        // sockaddr_un::sun_path is 104 bytes on macOS, including the terminating NUL.
        if (path.front() != '/' || path.size() >= 104) return std::nullopt;
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
    return options;
}
}  // namespace phpvpn::tray
