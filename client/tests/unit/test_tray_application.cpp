#include "harness.h"
#include "../../apps/tray/tray_application.h"
#include "../../apps/tray/resource.h"
#include "../../apps/tray/tray_arguments.h"
#include <atomic>
#include <functional>
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
struct ShellCounts {
    int adds = 0, modifies = 0, deletes = 0, notices = 0;
    std::wstring tooltip, noticeTitle, noticeText;
    DWORD noticeFlags = 0;
    DWORD noticeThread = 0;
    std::function<void()> onAdd;
    bool accept = true;
};
class FixtureShell final : public ShellAdapter {
public:
    explicit FixtureShell(ShellCounts& counts) : counts_(counts) {}
    bool notify(DWORD action, NOTIFYICONDATAW& data) override {
        if (action == NIM_ADD) {
            ++counts_.adds;
            if (counts_.onAdd) counts_.onAdd();
        }
        if (action == NIM_MODIFY) {
            ++counts_.modifies;
            if (data.uFlags & NIF_INFO) {
                ++counts_.notices;
                counts_.noticeTitle = data.szInfoTitle;
                counts_.noticeText = data.szInfo;
                counts_.noticeFlags = data.dwInfoFlags;
                counts_.noticeThread = GetCurrentThreadId();
            }
        }
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
void pumpFor(ULONGLONG milliseconds) {
    const auto end = GetTickCount64() + milliseconds;
    do { drain(); Sleep(5); } while (GetTickCount64() < end);
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

PHPVPN_TEST(InstallationNotice, OnlyExactInstallerArgumentRequestsNotice) {
    const wchar_t* ordinary[] = {L"phpvpn-tray.exe"};
    const wchar_t* unrelated[] = {L"phpvpn-tray.exe", L"--installation-complete-no", L"installation-complete"};
    const wchar_t* installed[] = {L"C:\\Program Files\\PHPVPN\\phpvpn-tray.exe", L"--installation-complete"};
    PHPVPN_CHECK(!requestsInstallationNotice(1, ordinary));
    PHPVPN_CHECK(!requestsInstallationNotice(3, unrelated));
    PHPVPN_CHECK(requestsInstallationNotice(2, installed));
}

PHPVPN_TEST(InstallationNotice, RunningInstanceReceivesEachInstallationWithoutDuplicateIcon) {
    ShellCounts first, duplicate;
    const auto name = L"Local\\phpvpn-install-test-" + uniqueName();
    const auto pipe = L"\\\\.\\pipe\\phpvpn-install-test-absent";
    const auto instance = GetModuleHandleW(nullptr);
    TrayApplication app({pipe, name, std::make_unique<FixtureShell>(first), {}});
    PHPVPN_CHECK(app.initialise(instance));
    pumpFor(300);
    PHPVPN_CHECK_EQ(first.notices, 0);
    for (int installation = 1; installation <= 2; ++installation) {
        TrayApplication installer({pipe, name, std::make_unique<FixtureShell>(duplicate), {}, true});
        PHPVPN_CHECK(installer.initialise(instance));
        PHPVPN_CHECK(installer.initialise(instance));
        PHPVPN_CHECK(!installer.window());
        PHPVPN_CHECK_EQ(installer.run(), 0);
        pumpFor(300);
        PHPVPN_CHECK_EQ(first.notices, installation);
    }
    PHPVPN_CHECK_EQ(first.adds, 1);
    PHPVPN_CHECK_EQ(duplicate.adds, 0);
    PHPVPN_CHECK_EQ(duplicate.notices, 0);
    TrayApplication ordinary({pipe, name, std::make_unique<FixtureShell>(duplicate), {}});
    PHPVPN_CHECK(ordinary.initialise(instance));
    pumpFor(300);
    PHPVPN_CHECK_EQ(first.notices, 2);
    PHPVPN_CHECK_EQ(first.noticeTitle, L"PHPVPN installed");
    PHPVPN_CHECK_EQ(first.noticeText, L"PHPVPN client is installed. To get started, open Configuration Wizard or Edit Config to set up your connection, then choose Connect when you're ready.");
    PHPVPN_CHECK((first.noticeFlags & NIIF_INFO) != 0);
    PHPVPN_CHECK((first.noticeFlags & NIIF_RESPECT_QUIET_TIME) != 0);
    PHPVPN_CHECK_EQ(first.noticeThread, GetCurrentThreadId());
}

PHPVPN_TEST(InstallationNotice, ConcurrentLaunchQueuesUntilPrimaryIsReadyAndStopsAtSessionEnd) {
    ShellCounts first, duplicate;
    const auto name = L"Local\\phpvpn-install-race-" + uniqueName();
    const auto pipe = L"\\\\.\\pipe\\phpvpn-install-test-absent";
    first.onAdd = [&] {
        TrayApplication installer({pipe, name, std::make_unique<FixtureShell>(duplicate), {}, true});
        PHPVPN_CHECK(installer.initialise(GetModuleHandleW(nullptr)));
        PHPVPN_CHECK(!installer.window());
    };
    TrayApplication app({pipe, name, std::make_unique<FixtureShell>(first), {}});
    PHPVPN_CHECK(app.initialise(GetModuleHandleW(nullptr)));
    pumpFor(300);
    PHPVPN_CHECK_EQ(first.notices, 1);
    PHPVPN_CHECK_EQ(duplicate.adds, 0);
    PHPVPN_CHECK_EQ(first.noticeThread, GetCurrentThreadId());
    app.endSession();
    const auto event = OpenEventW(EVENT_MODIFY_STATE, FALSE, (name + L"-installation").c_str());
    PHPVPN_CHECK(event);
    if (event) { SetEvent(event); CloseHandle(event); }
    pumpFor(300);
    PHPVPN_CHECK_EQ(first.notices, 1);
}

PHPVPN_TEST(InstallationNotice, FreshInstallAndReinstallNotifyButOrdinaryRestartDoesNot) {
    ShellCounts counts;
    const auto name = L"Local\\phpvpn-install-fresh-" + uniqueName();
    for (int start = 0; start != 3; ++start) {
        {
            TrayApplication app({L"\\\\.\\pipe\\phpvpn-install-absent", name,
                std::make_unique<FixtureShell>(counts), {}, start != 2});
            PHPVPN_CHECK(app.initialise(GetModuleHandleW(nullptr)));
            PHPVPN_CHECK(app.initialise(GetModuleHandleW(nullptr)));
            pumpFor(300);
            PHPVPN_CHECK_EQ(counts.notices, std::min(start + 1, 2));
            pumpFor(300);
            PHPVPN_CHECK_EQ(counts.notices, std::min(start + 1, 2));
        }
        drain();
    }
}

PHPVPN_TEST(InstallationNotice, FailedInitialisationDoesNotNotifyOrLeaveRequestForNextStart) {
    ShellCounts failed, next;
    failed.accept = false;
    const auto name = L"Local\\phpvpn-install-fail-" + uniqueName();
    const auto instance = GetModuleHandleW(nullptr);
    {
        TrayApplication app({L"\\\\.\\pipe\\phpvpn-install-absent", name, std::make_unique<FixtureShell>(failed), {}, true});
        PHPVPN_CHECK(!app.initialise(instance));
        pumpFor(300);
        PHPVPN_CHECK_EQ(failed.notices, 0);
    }
    drain();
    TrayApplication app({L"\\\\.\\pipe\\phpvpn-install-absent", name, std::make_unique<FixtureShell>(next), {}});
    PHPVPN_CHECK(app.initialise(instance));
    pumpFor(300);
    PHPVPN_CHECK_EQ(next.notices, 0);
}

PHPVPN_TEST(InstallationNotice, NativeAdapterDoesNotReplayNoticeOnIconUpdates) {
    ShellCounts counts;
    FixtureShell shell(counts);
    ShellIcon icon(shell);
    PHPVPN_CHECK(!phpvpn::showInstallationNotice(icon));
    const auto image = LoadIconW(nullptr, IDI_APPLICATION);
    PHPVPN_CHECK(icon.publish(reinterpret_cast<HWND>(1), WM_APP + 1, image, L"PHPVPN"));
    PHPVPN_CHECK(phpvpn::showInstallationNotice(icon));
    PHPVPN_CHECK_EQ(counts.notices, 1);
    PHPVPN_CHECK(icon.publish(reinterpret_cast<HWND>(1), WM_APP + 1, image, L"Disconnected"));
    PHPVPN_CHECK_EQ(counts.notices, 1);
    PHPVPN_CHECK(!icon.showInformation(std::string(64, 'a'), "body"));
    PHPVPN_CHECK(!icon.showInformation("title", std::string(256, 'a')));
    PHPVPN_CHECK(!icon.showInformation("title", std::string("\xc3\x28")));
    PHPVPN_CHECK_EQ(counts.notices, 1);
    counts.accept = false;
    PHPVPN_CHECK(!phpvpn::showInstallationNotice(icon));
    icon.remove();
    PHPVPN_CHECK(!phpvpn::showInstallationNotice(icon));
}

PHPVPN_TEST(InstallationNotice, SharedContentCanUsePlatformIndependentPresenter) {
    class PortablePresenter final : public phpvpn::InformationNoticePresenter {
    public:
        bool showInformation(std::string_view title, std::string_view body) override {
            PHPVPN_CHECK_EQ(title, phpvpn::kInstallationNoticeTitle);
            PHPVPN_CHECK_EQ(body, phpvpn::kInstallationNoticeText);
            ++calls;
            return true;
        }
        int calls = 0;
    } presenter;
    PHPVPN_CHECK(phpvpn::showInstallationNotice(presenter));
    PHPVPN_CHECK_EQ(presenter.calls, 1);
    PHPVPN_CHECK(std::count(phpvpn::kInstallationNoticeText.begin(), phpvpn::kInstallationNoticeText.end(), ' ') + 1 <= 50);
}
