#include "harness.h"
#include "../../apps/tray/tray_application.h"
#include "../../apps/tray/resource.h"
#include <atomic>
#include <thread>
#include "../../apps/tray/reconnect_controller.h"

namespace {
using namespace phpvpn::tray;
TrayStatus reconnectStatus() {
    TrayStatus status;
    status.valid = true; status.state = "connected"; status.phase = "running";
    status.connectionEpoch = 7; status.connectAttemptId = "fixture-attempt";
    status.connectionStartedAtUtcMs = 123; status.tunnelReady = "yes";
    return status;
}
TrayStatus reconnectIdle() {
    auto status = reconnectStatus();
    status.state = "disconnected"; status.phase = "idle";
    status.connectionStartedAtUtcMs.reset(); status.tunnelReady = "no";
    return status;
}
struct ShellCounts { int adds = 0, modifies = 0, deletes = 0; std::wstring tooltip; bool accept = true; };
class FixtureShell final : public ShellAdapter {
public:
    explicit FixtureShell(ShellCounts& counts) : counts_(counts) {}
    bool notify(DWORD action, NOTIFYICONDATAW& data) override {
        if (action == NIM_ADD) ++counts_.adds;
        if (action == NIM_MODIFY) ++counts_.modifies;
        if (action == NIM_DELETE) ++counts_.deletes;
        counts_.tooltip = data.szTip;
        return counts_.accept;
    }
private:
    ShellCounts& counts_;
};
std::wstring uniqueName() {
    auto crypto = phpvpn::createPlatformCryptoProvider();
    return crypto ? widen(crypto.value()->randomUuidV4().value()) : L"invalid";
}
void drain() {
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
        if (message.message != WM_QUIT) { TranslateMessage(&message); DispatchMessageW(&message); }
    }
}
}
PHPVPN_TEST(Reconnect, exact_identity_idle_and_one_shot_token) {
    ReconnectController controller;
    PHPVPN_CHECK(controller.begin());
    const auto token = controller.token();
    PHPVPN_CHECK(!controller.begin());
    PHPVPN_CHECK(controller.confirm(reconnectStatus()));
    controller.consent();
    PHPVPN_CHECK(controller.revalidate(reconnectStatus()));
    PHPVPN_CHECK(!controller.observeDisconnect(reconnectStatus()));
    auto stopping = reconnectStatus(); stopping.phase = "stopping";
    PHPVPN_CHECK(!controller.observeDisconnect(stopping));
    PHPVPN_CHECK(controller.current(token));
    PHPVPN_CHECK(controller.observeDisconnect(reconnectIdle()));
    PHPVPN_CHECK(!controller.observeDisconnect(reconnectIdle()));
    PHPVPN_CHECK(!controller.consume(token + 1, reconnectIdle()));
    PHPVPN_CHECK(controller.consume(token, reconnectIdle()));
    PHPVPN_CHECK(!controller.consume(token, reconnectIdle()));
    PHPVPN_CHECK(!controller.observeDisconnect(reconnectIdle()));
}
PHPVPN_TEST(Reconnect, rejects_foreign_or_unsafe_idle_and_never_revives) {
    for (int scenario = 0; scenario < 10; ++scenario) {
        ReconnectController controller;
        controller.begin(); controller.confirm(reconnectStatus()); controller.consent(); controller.revalidate(reconnectStatus());
        const auto token = controller.token();
        auto status = reconnectIdle();
        switch (scenario) {
            case 0: status.valid = false; break;
            case 1: ++status.connectionEpoch; break;
            case 2: status.connectionEpoch = 1; break; // service restart
            case 3: status.connectAttemptId = "foreign"; break;
            case 4: status.hasError = true; break;
            case 5: status.tunnelReady = "yes"; break;
            case 6: status.tunnelReady = "unknown"; break;
            case 7: status.connectionStartedAtUtcMs = 123; break;
            case 8: status.state = "error"; break;
            case 9: status = reconnectStatus(); status.connectionStartedAtUtcMs = 456; break;
        }
        PHPVPN_CHECK(!controller.observeDisconnect(status));
        PHPVPN_CHECK(!controller.current(token));
        PHPVPN_CHECK(!controller.observeDisconnect(reconnectIdle()));
        PHPVPN_CHECK(!controller.consume(token, reconnectIdle()));
    }
}
PHPVPN_TEST(Reconnect, revalidation_and_cancellation_in_every_phase) {
    for (int scenario = 0; scenario < 5; ++scenario) {
        ReconnectController controller;
        controller.begin(); controller.confirm(reconnectStatus()); controller.consent();
        auto status = reconnectStatus();
        switch (scenario) {
            case 0: status.valid = false; break;
            case 1: ++status.connectionEpoch; break;
            case 2: status.connectAttemptId = "foreign"; break;
            case 3: status.connectionStartedAtUtcMs = 456; break;
            case 4: status.state = "recovering"; break;
        }
        PHPVPN_CHECK(!controller.revalidate(status));
    }
    for (int phase = 0; phase < 5; ++phase) {
        ReconnectController controller;
        controller.begin(); const auto old = controller.token();
        if (phase > 0) controller.confirm(reconnectStatus());
        if (phase > 1) controller.consent();
        if (phase > 2) controller.revalidate(reconnectStatus());
        if (phase > 3) controller.observeDisconnect(reconnectIdle());
        controller.cancel();
        PHPVPN_CHECK(!controller.current(old));
        PHPVPN_CHECK(!controller.consume(old, reconnectIdle()));
        PHPVPN_CHECK(controller.begin());
        PHPVPN_CHECK(controller.token() != old);
        PHPVPN_CHECK(!controller.consume(old, reconnectIdle()));
    }
}
PHPVPN_TEST(Reconnect, expired_disconnect_reply_cannot_win_race_with_timer) {
    for (const std::uint64_t now : {29999u, 30000u, 30001u}) {
        ReconnectController controller;
        controller.begin(); controller.confirm(reconnectStatus()); controller.consent(); controller.revalidate(reconnectStatus());
        const auto token = controller.token();
        PHPVPN_CHECK_EQ(controller.observeDisconnect(reconnectIdle(),now,30000), now < 30000);
        PHPVPN_CHECK_EQ(controller.consume(token,reconnectIdle()), now < 30000);
        PHPVPN_CHECK(!controller.observeDisconnect(reconnectIdle(),30002,30000));
    }
}
PHPVPN_TEST(TrayApplicationSeam, OwnMutexShellAndMissingEndpointAreIsolated) {
    ShellCounts first, second, duplicate;
    const auto suffix = uniqueName();
    const auto pipe = L"\\\\.\\pipe\\phpvpn-wp02-absent-" + suffix;
    const auto mutex = L"Local\\phpvpn-wp02-" + suffix;
    const auto instance = GetModuleHandleW(nullptr);
    {
        TrayApplication a({pipe, mutex, std::make_unique<FixtureShell>(first), L"Fixture one"});
        PHPVPN_CHECK(a.initialise(instance));
        PHPVPN_CHECK(a.initialise(instance));
        PHPVPN_CHECK(a.window() != nullptr);
        TrayApplication same({pipe, mutex, std::make_unique<FixtureShell>(duplicate), L"Duplicate"});
        PHPVPN_CHECK(same.initialise(instance));
        PHPVPN_CHECK_EQ(same.window(), nullptr);
        PHPVPN_CHECK_EQ(same.run(), 0);
        TrayApplication b({pipe, mutex + L"-other", std::make_unique<FixtureShell>(second), L"Fixture two"});
        PHPVPN_CHECK(b.initialise(instance));
        PHPVPN_CHECK(b.window() && a.window() != b.window());
        const auto deadline = GetTickCount64() + 100;
        do { drain(); Sleep(1); } while (GetTickCount64() < deadline);
        PHPVPN_CHECK_EQ(first.adds, 1); PHPVPN_CHECK_EQ(second.adds, 1); PHPVPN_CHECK_EQ(duplicate.adds, 0);
        PHPVPN_CHECK_EQ(first.modifies, 0); PHPVPN_CHECK_EQ(second.modifies, 0);
        PHPVPN_CHECK_EQ(first.tooltip, L"Fixture one"); PHPVPN_CHECK_EQ(second.tooltip, L"Fixture two");
    }
    drain();
    PHPVPN_CHECK_EQ(first.deletes, 1); PHPVPN_CHECK_EQ(second.deletes, 1); PHPVPN_CHECK_EQ(duplicate.deletes, 0);
    // Names and class registration can be reused after complete destruction.
    {
        TrayApplication again({pipe, mutex, std::make_unique<FixtureShell>(first), L"Fixture one"});
        PHPVPN_CHECK(again.initialise(instance));
        PHPVPN_CHECK(again.window() != nullptr);
    }
    drain();
    PHPVPN_CHECK_EQ(first.adds, 2); PHPVPN_CHECK_EQ(first.deletes, 2);
}
PHPVPN_TEST(TrayApplicationSeam, EmptyDependencyFailsBeforeShellOrIpc) {
    ShellCounts counts;
    TrayApplication app({{}, L"Local\\wp02-empty", std::make_unique<FixtureShell>(counts), {}});
    PHPVPN_CHECK(!app.initialise(GetModuleHandleW(nullptr)));
    PHPVPN_CHECK_EQ(app.window(), nullptr);
    PHPVPN_CHECK_EQ(counts.adds, 0);
}
PHPVPN_TEST(TrayApplicationSeam, FailedShellCannotBecomeSuccessfulOnRepeatedInitialise) {
    ShellCounts counts;
    counts.accept = false;
    const auto suffix = uniqueName();
    {
        TrayApplication app({L"\\\\.\\pipe\\phpvpn-wp02-absent-" + suffix,
            L"Local\\phpvpn-wp02-" + suffix, std::make_unique<FixtureShell>(counts), {}});
        PHPVPN_CHECK(!app.initialise(GetModuleHandleW(nullptr)));
        counts.accept = true;
        PHPVPN_CHECK(!app.initialise(GetModuleHandleW(nullptr)));
        PHPVPN_CHECK_EQ(app.run(), 1);
        PHPVPN_CHECK_EQ(counts.adds, 1);
    }
    drain();
    PHPVPN_CHECK_EQ(counts.deletes, 0);
}
