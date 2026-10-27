// GTK-free models of the Linux tray owned by WP09 (plan-v001 section 5, WP09 step 4):
// status to icon/text/menu mapping, change detection, StatusNotifierWatcher policy
// through an injected sink, the common Exit coordinator, the test-only endpoint
// options and the asynchronous IPC channels against a real Unix-socket server.
#include "harness.h"

#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <map>
#include <mutex>
#include <thread>

#include "core/ipc/ipc_protocol.h"
#include "core/ipc/wizard_ipc.h"
#include "core/protocol/json.h"
#include "indicator_model.h"
#include "ipc_async.h"
#include "lifecycle.h"
#include "platform/linux/ipc_unix.h"
#include "tray_options.h"
#include "tray_text.h"

namespace phpvpn::tray {
namespace {
TrayStatus status(const char* state, const char* phase = "idle", const char* tunnel = "no", bool error = false) {
    TrayStatus result;
    result.valid = true;
    result.state = state;
    result.phase = phase;
    result.tunnelReady = tunnel;
    result.hasError = error;
    return result;
}

std::vector<std::string> labels(const std::vector<IndicatorMenuEntry>& entries) {
    std::vector<std::string> result;
    for (const auto& entry : entries)
        result.push_back(entry.kind == IndicatorMenuEntry::Kind::separator ? std::string("-") : entry.label);
    return result;
}

// Thread-safe stand-in for the GLib main context.
class QueueDispatcher {
public:
    Dispatcher dispatcher() {
        return [this](std::function<void()> task) {
            { std::lock_guard lock(mutex_); tasks_.push_back(std::move(task)); }
            changed_.notify_all();
        };
    }
    // Runs queued tasks on the calling ("UI") thread until done() or the limit.
    template <class Predicate>
    bool pumpUntil(Predicate done, int limitMs = 3000) {
        const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(limitMs);
        while (!done()) {
            std::function<void()> task;
            {
                std::unique_lock lock(mutex_);
                if (!changed_.wait_until(lock, end, [&] { return !tasks_.empty(); })) return done();
                task = std::move(tasks_.front());
                tasks_.pop_front();
            }
            task();
        }
        return true;
    }
    void drainFor(int ms) { pumpUntil([] { return false; }, ms); }
private:
    std::mutex mutex_;
    std::condition_variable changed_;
    std::deque<std::function<void()>> tasks_;
};

class TempSocketDir {
public:
    TempSocketDir() {
        char pattern[] = "/tmp/phpvpn-tray-XXXXXX";
        const char* made = mkdtemp(pattern);
        path_ = made ? made : "";
    }
    ~TempSocketDir() { std::error_code error; std::filesystem::remove_all(path_, error); }
    std::string socket() const { return path_ + "/control.sock"; }
private:
    std::string path_;
};

std::string statusReply(std::string_view request, ConnectionState state = ConnectionState::disconnected) {
    auto parsed = IpcProtocol::parseRequest(request);
    if (!parsed) return "{}";
    StatusSnapshot snapshot;
    snapshot.state = state;
    snapshot.stateChangedAtUtc = "2026-09-26T10:00:00.000Z";
    snapshot.clientVersion = "1.0.0";
    return IpcProtocol::buildStatusResponse(parsed->requestId, snapshot);
}

IpcSocketServerOptions serverOptions(const TempSocketDir& dir) {
    IpcSocketServerOptions options;
    options.socketPath = dir.socket();
    options.expectedOwnerUid = geteuid();
    return options;
}

class FakeSink final : public WatcherPresenceSink {
public:
    void openFallbackWindow() override { ++windows; }
    void showWatcherHint() override { ++hints; }
    int windows = 0, hints = 0;
};

class FakeParticipant final : public LifecycleParticipant {
public:
    LifecycleSnapshot snapshot;
    int discards = 0, ended = 0;
    bool veto = false;
    LifecycleSnapshot lifecycleSnapshot() const override { return snapshot; }
    bool approvedDiscard(const LifecycleSnapshot& approved) override {
        if (veto || approved != snapshot) return false;
        ++discards;
        snapshot = {};
        return true;
    }
    void endSession() override { ++ended; }
};
}  // namespace
}  // namespace phpvpn::tray

using namespace phpvpn;
using namespace phpvpn::tray;

PHPVPN_TEST(tray_linux_indicator, icon_and_text_follow_the_windows_rules) {
    const auto unavailable = indicatorView(status("connected", "running", "yes"), true, false, false);
    PHPVPN_CHECK(unavailable.icon == TrayIconState::disconnected);
    PHPVPN_CHECK_EQ(unavailable.iconName, std::string("phpvpn-disconnected"));
    PHPVPN_CHECK_EQ(unavailable.statusText, std::string("PHPVPN: service status unavailable"));
    // The toggle follows the last authoritative state even when a read failed.
    PHPVPN_CHECK(unavailable.toggleDisconnects);

    const auto connected = indicatorView(status("connected", "running", "yes"), false, false, false);
    PHPVPN_CHECK_EQ(connected.iconName, std::string("phpvpn-connected"));
    PHPVPN_CHECK_EQ(connected.statusText, std::string("PHPVPN: connected"));

    const auto unhealthy = indicatorView(status("connected", "running", "no"), false, false, false);
    PHPVPN_CHECK_EQ(unhealthy.iconName, std::string("phpvpn-disconnected"));
    const auto connecting = indicatorView(status("connecting", "health", "unknown"), false, false, false);
    PHPVPN_CHECK_EQ(connecting.iconName, std::string("phpvpn-connecting"));
    PHPVPN_CHECK_EQ(connecting.statusText, std::string("PHPVPN: connecting"));
    const auto recovering = indicatorView(status("recovering", "health", "no"), false, false, false);
    PHPVPN_CHECK_EQ(recovering.iconName, std::string("phpvpn-connecting"));
    PHPVPN_CHECK_EQ(recovering.statusText, std::string("PHPVPN: recovering"));
    const auto stopping = indicatorView(status("connected", "stopping", "yes"), false, false, false);
    PHPVPN_CHECK_EQ(stopping.iconName, std::string("phpvpn-connecting"));
    const auto error = indicatorView(status("error", "idle", "no", true), false, false, false);
    PHPVPN_CHECK_EQ(error.iconName, std::string("phpvpn-disconnected"));
    PHPVPN_CHECK_EQ(error.statusText, std::string("PHPVPN: error"));
    PHPVPN_CHECK(!error.toggleDisconnects);
    const auto idle = indicatorView(status("disconnected"), false, false, false);
    PHPVPN_CHECK_EQ(idle.statusText, std::string("PHPVPN: disconnected"));
    PHPVPN_CHECK(!idle.toggleDisconnects);

    // An accepted Disconnect changes the icon at once, even over a connected snapshot.
    const auto disconnecting = indicatorView(status("connected", "running", "yes"), false, true, false);
    PHPVPN_CHECK_EQ(disconnecting.iconName, std::string("phpvpn-connecting"));
    PHPVPN_CHECK_EQ(disconnecting.statusText, std::string("PHPVPN: connecting"));
    const auto localConnect = indicatorView(status("disconnected"), false, false, true);
    PHPVPN_CHECK_EQ(localConnect.iconName, std::string("phpvpn-connecting"));
    // Invalid snapshots never claim a state.
    PHPVPN_CHECK_EQ(indicatorView(TrayStatus{}, false, false, false).iconName, std::string("phpvpn-disconnected"));
    PHPVPN_CHECK_EQ(std::string(kApplicationIconName), std::string("phpvpn"));
}

PHPVPN_TEST(tray_linux_indicator, menu_has_status_item_then_exact_ux04_order) {
    const auto view = indicatorView(status("disconnected"), false, false, false);
    const auto menu = indicatorMenu(view);
    PHPVPN_CHECK((labels(menu) == std::vector<std::string>{
        "PHPVPN: disconnected", "-", "Connect", "-", "Configuration Wizard", "Communication contracts",
        "Edit Config", "-", "Show Server Info", "Statistics", "About", "-", "Exit"}));
    PHPVPN_CHECK(menu[0].kind == IndicatorMenuEntry::Kind::status);
    PHPVPN_CHECK(!menu[0].sensitive);
    for (std::size_t i = 2; i < menu.size(); ++i)
        if (menu[i].kind == IndicatorMenuEntry::Kind::action) PHPVPN_CHECK(menu[i].sensitive);
    PHPVPN_CHECK(menu[2].action == MenuAction::toggle);
    PHPVPN_CHECK(menu.back().action == MenuAction::exit);

    const auto connected = indicatorMenu(indicatorView(status("recovering", "health", "no"), false, false, false));
    PHPVPN_CHECK_EQ(connected[2].label, std::string("Disconnect"));
    PHPVPN_CHECK_EQ(connected[0].label, std::string("PHPVPN: recovering"));

    // The control window mirrors the menu without the status item.
    const auto buttons = controlWindowActions(view);
    PHPVPN_CHECK_EQ(buttons.size(), menu.size() - 2);
    PHPVPN_CHECK_EQ(buttons[0].label, std::string("Connect"));
    PHPVPN_CHECK_EQ(menuLabel(MenuAction::contracts, false), std::string("Communication contracts"));
    PHPVPN_CHECK_EQ(menuLabel(MenuAction::toggle, true), std::string("Disconnect"));
}

PHPVPN_TEST(tray_linux_indicator, publishes_only_real_changes) {
    IndicatorChangeGate gate;
    const auto first = indicatorView(status("disconnected"), false, false, false);
    PHPVPN_CHECK(gate.changed(first));
    PHPVPN_CHECK(!gate.changed(first));
    // A new but identical snapshot is not a change.
    PHPVPN_CHECK(!gate.changed(indicatorView(status("disconnected"), false, false, false)));
    PHPVPN_CHECK(gate.changed(indicatorView(status("disconnected"), true, false, false)));
    PHPVPN_CHECK(gate.changed(indicatorView(status("connecting", "health"), false, false, false)));
    gate.reset();
    PHPVPN_CHECK(gate.changed(indicatorView(status("connecting", "health"), false, false, false)));
}

PHPVPN_TEST(tray_linux_indicator, watcher_absence_opens_fallback_and_hints_once) {
    FakeSink sink;
    WatcherPresence presence(sink);
    PHPVPN_CHECK(!presence.known());
    presence.vanished();  // GDBus reports an initially missing name as vanished
    PHPVPN_CHECK_EQ(sink.windows, 1);
    PHPVPN_CHECK_EQ(sink.hints, 1);
    presence.vanished();
    PHPVPN_CHECK_EQ(sink.windows, 1);
    presence.appeared();
    PHPVPN_CHECK(presence.present());
    presence.vanished();
    PHPVPN_CHECK_EQ(sink.windows, 2);
    PHPVPN_CHECK_EQ(sink.hints, 1);

    FakeSink present;
    WatcherPresence available(present);
    available.appeared();
    PHPVPN_CHECK_EQ(present.windows, 0);
    PHPVPN_CHECK_EQ(present.hints, 0);
}

PHPVPN_TEST(tray_linux_exit, asks_only_for_dirty_or_unknown_and_waits_for_busy) {
    FakeParticipant wizard, editor;
    std::vector<LifecycleParticipant*> list{&wizard, &editor};
    auto current = [&] { return list; };
    std::vector<std::string> questions;
    bool answer = false;
    auto confirm = [&](std::string_view question) { questions.emplace_back(question); return answer; };
    ExitCoordinator exit;
    PHPVPN_CHECK(exit.advance(current, confirm) == ExitCoordinator::Step::idle);

    // Clean windows: no question at all.
    exit.begin();
    PHPVPN_CHECK(exit.advance(current, confirm) == ExitCoordinator::Step::finished);
    PHPVPN_CHECK(questions.empty());

    // A running save is awaited without asking.
    editor.snapshot = {true, true, false, true, 3};
    exit.begin();
    PHPVPN_CHECK(exit.advance(current, confirm) == ExitCoordinator::Step::wait);
    PHPVPN_CHECK(questions.empty());
    editor.snapshot.busy = false;
    // No keeps every draft and ends the Exit.
    PHPVPN_CHECK(exit.advance(current, confirm) == ExitCoordinator::Step::cancelled);
    PHPVPN_CHECK_EQ(questions.back(), std::string("Discard unsaved changes?"));
    PHPVPN_CHECK(!exit.active());
    PHPVPN_CHECK_EQ(editor.discards, 0);

    // Yes discards exactly the approved generations.
    answer = true;
    wizard.snapshot = {true, false, true, false, 7};
    exit.begin();
    PHPVPN_CHECK(exit.advance(current, confirm) == ExitCoordinator::Step::finished);
    PHPVPN_CHECK_EQ(questions.back(), std::string(kDiscardAndUnknownQuestion));
    PHPVPN_CHECK_EQ(wizard.discards, 1);
    PHPVPN_CHECK_EQ(editor.discards, 1);

    // A temporary veto retries without a second question.
    wizard.snapshot = {true, true, false, false, 8};
    wizard.veto = true;
    questions.clear();
    exit.begin();
    PHPVPN_CHECK(exit.advance(current, confirm) == ExitCoordinator::Step::wait);
    PHPVPN_CHECK(exit.advance(current, confirm) == ExitCoordinator::Step::wait);
    PHPVPN_CHECK_EQ(questions.size(), 1u);
    wizard.veto = false;
    PHPVPN_CHECK(exit.advance(current, confirm) == ExitCoordinator::Step::finished);
    PHPVPN_CHECK_EQ(questions.size(), 1u);

    // A change after the approval invalidates it.
    editor.snapshot = {true, true, false, false, 9};
    questions.clear();
    exit.begin();
    auto editing = [&](std::string_view question) {
        questions.emplace_back(question);
        ++editor.snapshot.generation;  // edited while the question was open
        return true;
    };
    PHPVPN_CHECK(exit.advance(current, editing) == ExitCoordinator::Step::wait);
    PHPVPN_CHECK(exit.advance(current, editing) == ExitCoordinator::Step::wait);
    PHPVPN_CHECK_EQ(questions.size(), 2u);
    PHPVPN_CHECK(exit.advance(current, confirm) == ExitCoordinator::Step::finished);

    PHPVPN_CHECK(!exitQuestion(false, false).has_value());
    PHPVPN_CHECK_EQ(std::string(*exitQuestion(false, true)), std::string(kUnknownQuestion));
}

PHPVPN_TEST(tray_linux_options, test_endpoint_is_explicit_and_validated) {
    std::map<std::string, std::string> env;
    auto lookup = [&](const char* name) -> const char* {
        const auto found = env.find(name);
        return found == env.end() ? nullptr : found->second.c_str();
    };
    auto defaults = trayOptionsFromEnvironment(lookup);
    PHPVPN_CHECK(defaults.has_value());
    PHPVPN_CHECK(defaults->socketPath.empty());
    PHPVPN_CHECK_EQ(defaults->expectedServerUid, static_cast<uid_t>(0));
    PHPVPN_CHECK(!defaults->testEndpoint);

    env["PHPVPN_TRAY_TEST_SERVER_UID"] = "1000";
    PHPVPN_CHECK(!trayOptionsFromEnvironment(lookup).has_value());  // uid without a private socket
    env["PHPVPN_TRAY_TEST_SOCKET"] = "/run/user/1000/phpvpn-mock/control.sock";
    auto test = trayOptionsFromEnvironment(lookup);
    PHPVPN_CHECK(test.has_value());
    PHPVPN_CHECK(test->testEndpoint);
    PHPVPN_CHECK_EQ(test->expectedServerUid, static_cast<uid_t>(1000));
    for (const char* bad : {"-1", "10x", "", "99999999999999999999"}) {
        env["PHPVPN_TRAY_TEST_SERVER_UID"] = bad;
        if (*bad) PHPVPN_CHECK(!trayOptionsFromEnvironment(lookup).has_value());
    }
    env["PHPVPN_TRAY_TEST_SERVER_UID"] = "0";
    env["PHPVPN_TRAY_TEST_SOCKET"] = "relative.sock";
    PHPVPN_CHECK(!trayOptionsFromEnvironment(lookup).has_value());
    env["PHPVPN_TRAY_TEST_SOCKET"] = "/" + std::string(120, 'a');
    PHPVPN_CHECK(!trayOptionsFromEnvironment(lookup).has_value());
    env.erase("PHPVPN_TRAY_TEST_SOCKET");
    env.erase("PHPVPN_TRAY_TEST_SERVER_UID");
    env["PHPVPN_TRAY_ICON_THEME_PATH"] = "/home/user/build/icons";
    PHPVPN_CHECK_EQ(trayOptionsFromEnvironment(lookup)->iconThemePath, std::string("/home/user/build/icons"));

    const char* argv[]{"phpvpn-tray", "--show-window", "--installation-complete"};
    PHPVPN_CHECK(requestsShowWindow(3, argv));
    PHPVPN_CHECK(!requestsShowWindow(1, argv));
    const char* prefix[]{"phpvpn-tray", "--show-window=1", "--show"};
    PHPVPN_CHECK(!requestsShowWindow(3, prefix));
}

PHPVPN_TEST(tray_linux_text, utf8_catalogue_and_placeholders) {
    PHPVPN_CHECK_EQ(utf8(L"PHPVPN: connected"), std::string("PHPVPN: connected"));
    PHPVPN_CHECK_EQ(unavailableWindowText(MenuAction::statistics),
                    std::string("Statistics is not available in this build of the PHPVPN tray."));
    PHPVPN_CHECK(std::string(kWatcherHintText).find("gnome-extensions enable appindicatorsupport@rgcjonas.gmail.com") !=
                 std::string::npos);
}

PHPVPN_TEST(tray_linux_text, about_puts_version_first_then_licence_and_copyright) {
    PHPVPN_CHECK_EQ(utf8(aboutTitle), std::string("About PHPVPN"));
    PHPVPN_CHECK_EQ(aboutDialogText(), std::string("PHPVPN version " PHPVPN_CLIENT_VERSION "\n\n"
                                                   "PHPVPN is freeware and open source software.\nLicense: MIT\n"
                                                   "Copyright \xC2\xA9 www.pilandrome.com"));
    PHPVPN_CHECK_EQ(menuLabel(MenuAction::about, false), std::string("About"));
}

PHPVPN_TEST(tray_linux_ipc, status_channel_delivers_in_order_and_abandon_revokes) {
    TempSocketDir dir;
    std::atomic<int> calls{0};
    IpcSocketServer server([&](std::string_view request) {
        ++calls;
        return statusReply(request, ConnectionState::connecting);
    }, serverOptions(dir));
    PHPVPN_REQUIRE_OK(server.start());
    QueueDispatcher ui;
    std::vector<TrayStatus> received;
    const IpcEndpoint endpoint{dir.socket(), geteuid()};
    StatusChannel channel(endpoint, ui.dispatcher(), [&](const TrayStatus& value) { received.push_back(value); });
    channel.request();
    channel.request();  // queued while the first read is in flight
    PHPVPN_CHECK(ui.pumpUntil([&] { return received.size() == 2; }));
    PHPVPN_CHECK(received[0].valid);
    PHPVPN_CHECK_EQ(received[0].state, std::string("connecting"));

    channel.request();
    channel.abandon();
    ui.drainFor(300);
    PHPVPN_CHECK_EQ(received.size(), 2u);

    // A peer with an unexpected uid is never sent anything and reads as invalid.
    StatusChannel foreign(IpcEndpoint{dir.socket(), geteuid() + 1}, ui.dispatcher(),
                          [&](const TrayStatus& value) { received.push_back(value); });
    const int before = calls;
    foreign.request();
    PHPVPN_CHECK(ui.pumpUntil([&] { return received.size() == 3; }));
    PHPVPN_CHECK(!received[2].valid);
    PHPVPN_CHECK_EQ(calls.load(), before);
    server.stop();
}

PHPVPN_TEST(tray_linux_ipc, command_channel_correlates_and_is_single_flight) {
    TempSocketDir dir;
    std::atomic<bool> wrongId{false};
    IpcSocketServer server([&](std::string_view request) -> std::string {
        auto parsed = IpcProtocol::parseRequest(request);
        if (!parsed) return "{}";
        if (parsed->command == IpcCommand::get_config) return "{\"not\":\"enveloped\"}";
        if (wrongId) return IpcProtocol::buildAcknowledgement("3f2a7c1e-9b44-4b0e-8d2f-5c6a1e7b9d03");
        return statusReply(request, ConnectionState::connecting);
    }, serverOptions(dir));
    PHPVPN_REQUIRE_OK(server.start());
    QueueDispatcher ui;
    IpcChannel channel(IpcEndpoint{dir.socket(), geteuid()}, ui.dispatcher());
    std::vector<std::string> replies;
    PHPVPN_CHECK(channel.request(IpcCommand::connect, {}, [&](std::string& reply) { replies.push_back(reply); }));
    PHPVPN_CHECK(channel.busy());
    PHPVPN_CHECK(!channel.request(IpcCommand::connect, {}, {}));
    PHPVPN_CHECK(!channel.request(IpcCommand::set_config, "{}", {}));
    PHPVPN_CHECK(ui.pumpUntil([&] { return replies.size() == 1; }));
    PHPVPN_CHECK(!channel.busy());
    PHPVPN_CHECK(parseStatusResponse(replies[0]).valid);

    PHPVPN_CHECK(channel.request(IpcCommand::get_config, {}, [&](std::string& reply) { replies.push_back(reply); }));
    PHPVPN_CHECK(ui.pumpUntil([&] { return replies.size() == 2; }));
    PHPVPN_CHECK_EQ(replies[1], std::string("{\"not\":\"enveloped\"}"));

    wrongId = true;
    PHPVPN_CHECK(channel.request(IpcCommand::disconnect, {}, [&](std::string& reply) { replies.push_back(reply); }));
    PHPVPN_CHECK(ui.pumpUntil([&] { return replies.size() == 3; }));
    PHPVPN_CHECK(replies[2].empty());
    server.stop();
}

PHPVPN_TEST(tray_linux_ipc, save_uses_request_sent_for_classification) {
    TempSocketDir dir;
    QueueDispatcher ui;
    const IpcEndpoint endpoint{dir.socket(), geteuid()};
    std::vector<SaveResult> results;
    auto collect = [&](const SaveResult& result) { results.push_back(result); };
    SaveOperation operation{1, 1, 1, static_cast<std::uint64_t>(monotonicMs() + 5000)};
    {
        // No service: the request never left the process.
        IpcChannel channel(endpoint, ui.dispatcher());
        PHPVPN_CHECK(channel.requestSave(operation, "{}", collect));
        PHPVPN_CHECK(ui.pumpUntil([&] { return results.size() == 1; }));
        PHPVPN_CHECK(results[0].outcome == SaveOutcome::notSent);
        PHPVPN_CHECK(results[0].operation == operation);
    }
    std::atomic<int> mode{0};
    IpcSocketServer server([&](std::string_view request) -> std::string {
        auto parsed = IpcProtocol::parseRequest(request);
        if (!parsed) return "{}";
        if (mode == 0) return IpcProtocol::buildAcknowledgement(parsed->requestId);
        if (mode == 1) return IpcProtocol::buildError(parsed->requestId, Error(ErrorCode::config_invalid_json));
        return "not json";
    }, serverOptions(dir));
    PHPVPN_REQUIRE_OK(server.start());
    IpcChannel channel(endpoint, ui.dispatcher());
    for (int step = 0; step < 3; ++step) {
        mode = step;
        PHPVPN_CHECK(channel.requestSave(operation, "{}", collect));
        PHPVPN_CHECK(ui.pumpUntil([&] { return results.size() == static_cast<std::size_t>(step + 2); }));
    }
    PHPVPN_CHECK(results[1].outcome == SaveOutcome::confirmed);
    PHPVPN_CHECK(results[2].outcome == SaveOutcome::rejected);
    PHPVPN_CHECK(results[3].outcome == SaveOutcome::unknown);  // sent, answer not provable

    // An expired deadline is never sent.
    SaveOperation expired{1, 1, 2, static_cast<std::uint64_t>(monotonicMs() - 1)};
    PHPVPN_CHECK(channel.requestSave(expired, "{}", collect));
    PHPVPN_CHECK(ui.pumpUntil([&] { return results.size() == 5; }));
    PHPVPN_CHECK(results[4].outcome == SaveOutcome::notSent);
    PHPVPN_CHECK(results[4].error == ErrorCode::timeout);
    PHPVPN_CHECK(!channel.requestSave(SaveOperation{}, "{}", collect));
    server.stop();
}

PHPVPN_TEST(tray_linux_ipc, wizard_request_and_abandon_without_delivery) {
    TempSocketDir dir;
    std::atomic<bool> slow{false};
    IpcSocketServer server([&](std::string_view request) -> std::string {
        auto parsed = WizardIpc::parseRequest(request);
        if (!parsed) return "{}";
        if (slow) std::this_thread::sleep_for(std::chrono::milliseconds(300));
        return WizardIpc::buildError(parsed->requestId, WizardIpc::safeError(Error(ErrorCode::config_missing)));
    }, serverOptions(dir));
    PHPVPN_REQUIRE_OK(server.start());
    QueueDispatcher ui;
    IpcChannel channel(IpcEndpoint{dir.socket(), geteuid()}, ui.dispatcher());
    int delivered = 0;
    WizardRequest request;
    request.command = WizardCommand::get_config_state;
    PHPVPN_CHECK(channel.requestWizard(request, [&](WizardResult& result) {
        ++delivered;
        PHPVPN_CHECK(result.reply.ok());
        PHPVPN_CHECK(result.reply.value().error.has_value());
        PHPVPN_CHECK(result.request.command == WizardCommand::get_config_state);
        PHPVPN_CHECK(!result.request.requestId.empty());
    }));
    PHPVPN_CHECK(ui.pumpUntil([&] { return delivered == 1; }));

    slow = true;
    PHPVPN_CHECK(channel.requestWizard(request, [&](WizardResult&) { ++delivered; }));
    channel.abandon();
    PHPVPN_CHECK(!channel.busy());
    ui.drainFor(600);
    PHPVPN_CHECK_EQ(delivered, 1);
    channel.stop();
    server.stop();
}
