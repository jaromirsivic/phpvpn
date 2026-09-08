#include "harness.h"
#include "../../apps/tray/tray_application.h"
#include "../../apps/tray/resource.h"
#include <atomic>
#include <thread>

namespace {
using namespace phpvpn::tray;
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
