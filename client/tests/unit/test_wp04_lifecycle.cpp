#include "harness.h"
#include "../../apps/tray/wizard_reader.h"
#include "../../apps/tray/wizard_window.h"
#include "../../apps/tray/window_utils.h"
#include "core/crypto/crypto_provider.h"
#include "platform/windows/ipc_pipe.h"
#include <condition_variable>
#include <atomic>

namespace {
using namespace phpvpn;
using namespace phpvpn::tray;
struct HeldWizardBackend {
    std::wstring pipe = L"\\\\.\\pipe\\phpvpn-wp04-lifecycle-" + widen(createPlatformCryptoProvider().value()->randomUuidV4().value());
    std::mutex mutex;
    std::condition_variable changed;
    bool released = false;
    std::atomic<unsigned> received{0}, completed{0}, unexpected{0};
    IpcPipeServer server;
    HeldWizardBackend() : server([this](std::string_view bytes) {
        const auto request = WizardIpc::parseRequest(bytes);
        if (!request || request->command != WizardCommand::get_default_config) { ++unexpected; return std::string{}; }
        ++received;
        { std::unique_lock lock(mutex); changed.wait(lock, [&] { return released; }); }
        ++completed;
        return WizardIpc::buildError(request->requestId, WizardIpc::safeError(Error(ErrorCode::ipc_unavailable)));
    }, options()) { PHPVPN_CHECK(server.start().ok()); }
    IpcPipeServerOptions options() { IpcPipeServerOptions value; value.pipeName = pipe; return value; }
    void release() { { std::lock_guard lock(mutex); released = true; } changed.notify_all(); }
    ~HeldWizardBackend() { release(); server.stop(); }
};
template<class Predicate> bool waitFor(Predicate predicate) {
    const auto end = GetTickCount64() + 2000;
    do { if (predicate()) return true; Sleep(1); } while (GetTickCount64() < end);
    return false;
}
}
PHPVPN_TEST(WP04Lifecycle, ReaderAbandonRevokesNotificationAndAllowsNewGeneration) {
    const auto target = CreateWindowExW(0, L"STATIC", L"WP04 private reader target", 0, 0, 0, 1, 1,
        HWND_MESSAGE, nullptr, GetModuleHandleW(nullptr), nullptr);
    PHPVPN_CHECK(target != nullptr);
    ULONGLONG worst = 0;
    for (unsigned i = 0; i < 24; ++i) {
        HeldWizardBackend backend;
        auto reader = std::make_unique<WizardReader>(backend.pipe);
        WizardRequest request; request.command = WizardCommand::get_default_config;
        PHPVPN_CHECK(reader->request(request, target, WM_APP + 111, i + 1));
        PHPVPN_CHECK(!reader->request(request, target, WM_APP + 111, i + 2));
        PHPVPN_CHECK(waitFor([&] { return backend.received == 1; }));
        const auto start = GetTickCount64();
        reader->abandon();
        PHPVPN_CHECK(!reader->busy() && !reader->take());
        reader.reset();
        worst = std::max(worst, GetTickCount64() - start);
        PHPVPN_CHECK(worst < 250 && backend.completed == 0);
        backend.release(); backend.server.stop();
        Sleep(2);
        MSG message{};
        PHPVPN_CHECK(!PeekMessageW(&message, target, WM_APP + 111, WM_APP + 111, PM_REMOVE));
        PHPVPN_CHECK_EQ(backend.received.load(), 1u);
        PHPVPN_CHECK_EQ(backend.unexpected.load(), 0u);
    }
    DestroyWindow(target);
    // Reuse the same reader after abandon. Only the new token may be returned.
    HeldWizardBackend backend;
    WizardReader reader(backend.pipe);
    WizardRequest request; request.command = WizardCommand::get_default_config;
    PHPVPN_CHECK(reader.request(request, nullptr, 0, 100));
    PHPVPN_CHECK(waitFor([&] { return backend.received == 1; }));
    reader.abandon(); backend.release();
    PHPVPN_CHECK(waitFor([&] { return backend.completed == 1; }));
    PHPVPN_CHECK(reader.request(request, nullptr, 0, 101));
    std::optional<WizardReadResult> result;
    PHPVPN_CHECK(waitFor([&] { result = reader.take(); return result.has_value(); }));
    PHPVPN_CHECK(result && result->generation == 101 && result->reply && result->reply->error);
    std::cout << "[ EVIDENCE ] 24 Wizard reader abandon/destroy cycles, worst=" << worst << "ms, late notification=0\n";
}
PHPVPN_TEST(WP04Lifecycle, GateStatusKeepsCorrelationDeadlineAndSingleRequest) {
    auto pipe = L"\\\\.\\pipe\\phpvpn-wp04-status-" + widen(createPlatformCryptoProvider().value()->randomUuidV4().value());
    std::atomic<unsigned> reads{0};
    std::atomic<bool> foreign{true}, hold{false}, released{false};
    std::mutex mutex;
    std::condition_variable changed;
    IpcPipeServerOptions options; options.pipeName = pipe;
    IpcPipeServer server([&](std::string_view bytes) {
        const auto request = IpcProtocol::parseRequest(bytes);
        if (!request || request->command != IpcCommand::get_status) return std::string{};
        ++reads;
        if (hold) { std::unique_lock lock(mutex); changed.wait(lock, [&] { return released.load(); }); }
        StatusSnapshot status; status.stateChangedAtUtc = "2026-09-08T00:00:00.000Z"; status.clientVersion = "wp04-fixture";
        return IpcProtocol::buildStatusResponse(foreign ? "12345678-1234-4234-8234-123456789abc" : request->requestId, status);
    }, options);
    PHPVPN_CHECK(server.start().ok());
    WizardStatusReader reader(pipe);
    std::optional<TrayStatus> result;
    reader.request(nullptr, 0);
    PHPVPN_CHECK(waitFor([&] { result = reader.take(); return result.has_value(); }));
    PHPVPN_CHECK(result && !result->valid);
    foreign = false;
    reader.request(nullptr, 0);
    PHPVPN_CHECK(waitFor([&] { result = reader.take(); return result.has_value(); }));
    PHPVPN_CHECK(result && result->valid);
    hold = true;
    const auto start = GetTickCount64();
    reader.request(nullptr, 0);
    for (int i = 0; i < 10; ++i) reader.request(nullptr, 0);
    PHPVPN_CHECK(waitFor([&] { result = reader.take(); return result.has_value(); }));
    const auto elapsed = GetTickCount64() - start;
    PHPVPN_CHECK(result && !result->valid && elapsed >= 350 && elapsed < 1000);
    PHPVPN_CHECK_EQ(reads.load(), 3u);
    { std::lock_guard lock(mutex); released = true; } changed.notify_all();
    server.stop();
}
PHPVPN_TEST(WP04Lifecycle, ClosedSnapshotAndTerminalSessionEndDoNotStartWorkers) {
    WizardWindow wizard(L"");
    const auto initial = wizard.lifecycleSnapshot();
    PHPVPN_CHECK(!initial.open && !initial.dirty && !initial.committing && !initial.commitUnknown && !initial.commitDeadlineMs);
    PHPVPN_CHECK(!wizard.approvedDiscard(initial));
    wizard.endSession();
    const auto ended = wizard.lifecycleSnapshot();
    PHPVPN_CHECK(!ended.open && ended.windowGeneration != initial.windowGeneration && ended.sessionGeneration != initial.sessionGeneration);
    wizard.endSession();
    PHPVPN_CHECK(wizard.lifecycleSnapshot() == ended);
    PHPVPN_CHECK(!wizard.open(GetModuleHandleW(nullptr)));
}
