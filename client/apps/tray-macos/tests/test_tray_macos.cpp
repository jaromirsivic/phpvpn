// GUI-free tests of the macOS tray helpers (label "macos"): launch arguments, the
// test endpoint options, the status item model (icon, tooltip, menu, change gate),
// the macOS texts and the IPC channel preconditions. No AppKit, window server or
// running service is needed.
#include <unistd.h>

#include <map>
#include <optional>
#include <string>

#include "harness.h"
#include "ipc_async.h"
#include "macos_options.h"
#include "macos_text.h"
#include "status_item_model.h"

using namespace phpvpn;
using namespace phpvpn::tray;

namespace {
std::optional<TrayOptions> optionsFor(const std::map<std::string, std::string>& values) {
    return trayOptionsFromEnvironment([&values](const char* name) -> const char* {
        const auto found = values.find(name);
        return found == values.end() ? nullptr : found->second.c_str();
    });
}

TrayStatus status(const char* state, const char* phase, const char* tunnelReady) {
    TrayStatus value;
    value.valid = true;
    value.state = state;
    value.phase = phase;
    value.tunnelReady = tunnelReady;
    return value;
}
}  // namespace

PHPVPN_TEST(TrayMacos, LaunchArgumentsAreExactMatches) {
    const char* launch[]{"phpvpn-tray", "-psn_0_12345", "--show-window", "--installation-complete"};
    const auto request = parseLaunchArguments(4, launch);
    PHPVPN_CHECK(request.showWindow);
    PHPVPN_CHECK(request.installationComplete);
    PHPVPN_CHECK(!request.help);

    const char* help[]{"phpvpn-tray", "--help"};
    PHPVPN_CHECK(parseLaunchArguments(2, help).help);

    // argv[0] and inexact spellings never request anything.
    const char* inexact[]{"--show-window", "--show-window=1", "--Installation-complete"};
    const auto none = parseLaunchArguments(3, inexact);
    PHPVPN_CHECK(!none.showWindow);
    PHPVPN_CHECK(!none.installationComplete);
    PHPVPN_CHECK(!none.help);
    PHPVPN_CHECK(!kTrayUsage.empty());
}

PHPVPN_TEST(TrayMacos, ProductionEndpointWithoutTestSettings) {
    const auto options = optionsFor({});
    PHPVPN_CHECK(options.has_value());
    PHPVPN_CHECK(options->socketPath.empty());
    PHPVPN_CHECK_EQ(options->expectedServerUid, static_cast<uid_t>(0));
    PHPVPN_CHECK(!options->testEndpoint);
}

PHPVPN_TEST(TrayMacos, TestEndpointSettingsAreValidated) {
    const auto socket = optionsFor({{"PHPVPN_TRAY_TEST_SOCKET", "/private/tmp/phpvpn-test/control.sock"},
                                    {"PHPVPN_TRAY_TEST_SERVER_UID", "501"}});
    PHPVPN_CHECK(socket.has_value());
    PHPVPN_CHECK(socket->testEndpoint);
    PHPVPN_CHECK_EQ(socket->socketPath, std::string("/private/tmp/phpvpn-test/control.sock"));
    PHPVPN_CHECK_EQ(socket->expectedServerUid, static_cast<uid_t>(501));

    PHPVPN_CHECK(!optionsFor({{"PHPVPN_TRAY_TEST_SOCKET", "relative.sock"}}).has_value());
    PHPVPN_CHECK(!optionsFor({{"PHPVPN_TRAY_TEST_SOCKET", "/" + std::string(103, 'x')}}).has_value());
    // A peer uid without a private socket would weaken the production check.
    PHPVPN_CHECK(!optionsFor({{"PHPVPN_TRAY_TEST_SERVER_UID", "501"}}).has_value());
    PHPVPN_CHECK(!optionsFor({{"PHPVPN_TRAY_TEST_SOCKET", "/tmp/a.sock"}, {"PHPVPN_TRAY_TEST_SERVER_UID", "5x"}})
                      .has_value());
    PHPVPN_CHECK(!optionsFor({{"PHPVPN_TRAY_TEST_SOCKET", "/tmp/a.sock"}, {"PHPVPN_TRAY_TEST_SERVER_UID", "-1"}})
                      .has_value());
}

PHPVPN_TEST(TrayMacos, StatusItemFollowsTheWindowsIconRules) {
    const auto unavailable = indicatorView(TrayStatus{}, true, false, false);
    PHPVPN_CHECK(unavailable.icon == TrayIconState::disconnected);
    PHPVPN_CHECK_EQ(unavailable.iconName, std::string("phpvpn-disconnected"));
    PHPVPN_CHECK_EQ(unavailable.statusText, std::string("PHPVPN: service status unavailable"));
    PHPVPN_CHECK(!unavailable.toggleDisconnects);

    const auto connected = indicatorView(status("connected", "running", "yes"), false, false, false);
    PHPVPN_CHECK(connected.icon == TrayIconState::connected);
    PHPVPN_CHECK_EQ(connected.iconName, std::string("phpvpn-connected"));
    PHPVPN_CHECK_EQ(connected.statusText, std::string("PHPVPN: connected"));
    PHPVPN_CHECK(connected.toggleDisconnects);

    // An accepted local Disconnect changes the icon at once, also over a connected snapshot.
    const auto stopping = indicatorView(status("connected", "running", "yes"), false, true, false);
    PHPVPN_CHECK(stopping.icon == TrayIconState::connecting);
    PHPVPN_CHECK_EQ(stopping.statusText, std::string("PHPVPN: connecting"));

    const auto recovering = indicatorView(status("recovering", "reconnecting", "no"), false, false, false);
    PHPVPN_CHECK(recovering.icon == TrayIconState::connecting);
    PHPVPN_CHECK_EQ(recovering.statusText, std::string("PHPVPN: recovering"));

    const auto error = indicatorView(status("error", "idle", "no"), false, false, false);
    PHPVPN_CHECK(error.icon == TrayIconState::disconnected);
    PHPVPN_CHECK_EQ(error.statusText, std::string("PHPVPN: error"));
}

PHPVPN_TEST(TrayMacos, StatusItemMenuKeepsTheUx04Order) {
    const auto view = indicatorView(status("disconnected", "idle", "no"), false, false, false);
    const auto menu = statusItemMenu(view);
    PHPVPN_CHECK_EQ(menu.size(), static_cast<std::size_t>(13));
    PHPVPN_CHECK(menu[0].kind == IndicatorMenuEntry::Kind::status);
    PHPVPN_CHECK(!menu[0].enabled);
    PHPVPN_CHECK_EQ(menu[0].label, std::string("PHPVPN: disconnected"));
    PHPVPN_CHECK(menu[1].kind == IndicatorMenuEntry::Kind::separator);
    const char* labels[]{"Connect", "", "Configuration Wizard", "Communication contracts", "Edit Config", "",
                         "Show Server Info", "Statistics", "About", "", "Exit"};
    for (std::size_t i = 0; i < 11; ++i) {
        PHPVPN_CHECK_EQ(menu[i + 2].label, std::string(labels[i]));
        if (menu[i + 2].kind == IndicatorMenuEntry::Kind::action) PHPVPN_CHECK(menu[i + 2].enabled);
    }
    const auto connected = statusItemMenu(indicatorView(status("connected", "running", "yes"), false, false, false));
    PHPVPN_CHECK_EQ(connected[2].label, std::string("Disconnect"));
}

PHPVPN_TEST(TrayMacos, ChangeGatePublishesOnlyRealChanges) {
    IndicatorChangeGate gate;
    const auto view = indicatorView(TrayStatus{}, true, false, false);
    PHPVPN_CHECK(gate.changed(view));
    PHPVPN_CHECK(!gate.changed(view));
    PHPVPN_CHECK(gate.changed(indicatorView(status("connected", "running", "yes"), false, false, false)));
    gate.reset();
    PHPVPN_CHECK(gate.changed(view));
}

PHPVPN_TEST(TrayMacos, TextsNameTheMacConfigurationPath) {
    PHPVPN_CHECK_EQ(expectedConfigurationPath(), std::string("/Library/Application Support/phpvpn/config.json"));
    PHPVPN_CHECK(kServiceStartHint.find("launchctl bootstrap system") != std::string_view::npos);
    PHPVPN_CHECK(aboutHeadingText().starts_with("PHPVPN version "));
    PHPVPN_CHECK(!aboutBodyText().empty());
    PHPVPN_CHECK_EQ(utf8(L"©"), std::string("\xC2\xA9"));
}

PHPVPN_TEST(TrayMacos, MonotonicClockAndChannelPreconditions) {
    const auto first = monotonicMs();
    const auto second = monotonicMs();
    PHPVPN_CHECK(second >= first);

    // Invalid requests never reach a socket.
    const IpcEndpoint endpoint{"/private/var/run/phpvpn-tray-test-missing/control.sock", ::getuid()};
    PHPVPN_CHECK(exchangeOnce(endpoint, "", 1000).error == ErrorCode::invalid_argument);
    PHPVPN_CHECK(exchangeOnce(endpoint, "{}", 0).error == ErrorCode::timeout);
    const auto missing = exchangeOnce(endpoint, "{}", 1000);
    PHPVPN_CHECK(missing.response.empty());
    PHPVPN_CHECK(missing.error != ErrorCode::none);
    PHPVPN_CHECK(!missing.outcome.requestSent);

    // Without a dispatcher nothing starts; a save needs its generations and deadline.
    IpcChannel channel(endpoint, Dispatcher{});
    PHPVPN_CHECK(!channel.request(IpcCommand::get_status, {}, [](std::string&) {}));
    PHPVPN_CHECK(!channel.busy());
    IpcChannel saver(endpoint, [](std::function<void()> task) { task(); });
    PHPVPN_CHECK(!saver.requestSave(SaveOperation{}, "{}", [](const SaveResult&) {}));
    PHPVPN_CHECK(!saver.request(IpcCommand::set_config, "{}", [](std::string&) {}));
    PHPVPN_CHECK(!saver.busy());
}
