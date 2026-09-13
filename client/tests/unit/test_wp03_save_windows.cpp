#include "harness.h"
#include "wp03_dialog_test.h"
#include "tray_fixture.h"
#include "../../apps/tray/tray_application.h"
#include "../../apps/tray/resource.h"
#include "core/config/config_defaults.h"
#include "platform/windows/ipc_pipe.h"
#include <atomic>
#include <array>
#include <condition_variable>

namespace {
using namespace phpvpn;
using namespace phpvpn::tray;
namespace dialogs = phpvpn::tray::test;
// Diagnostic wall/CPU samples complement, but do not replace, the original
// GetTickCount64 wall-time gate. Keep reporting outside the measured interval.
struct LifecycleTime {
    LARGE_INTEGER wall{};
    ULONGLONG threadCycles{};
};
LifecycleTime lifecycleTime() {
    LifecycleTime sample;
    QueryPerformanceCounter(&sample.wall);
    QueryThreadCycleTime(GetCurrentThread(), &sample.threadCycles);
    return sample;
}
void pump() {
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
        if (message.message != WM_QUIT) { TranslateMessage(&message); DispatchMessageW(&message); }
    Sleep(1);
}
bool until(const std::function<bool()>& predicate, DWORD timeout = 1500) {
    const auto end = GetTickCount64() + timeout;
    while (!predicate() && GetTickCount64() < end) pump();
    return predicate();
}
HWND editorWindow() {
    HWND found = nullptr;
    EnumThreadWindows(GetCurrentThreadId(), [](HWND window, LPARAM p) -> BOOL {
        wchar_t name[64]{}; GetClassNameW(window, name, 64);
        if (std::wstring_view(name) == L"PhpVpnTrayEditor") { *reinterpret_cast<HWND*>(p) = window; return FALSE; }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&found));
    return found;
}
std::wstring text(HWND field) {
    const auto n = GetWindowTextLengthW(field);
    std::wstring value(n + 1, L'\0'); GetWindowTextW(field, value.data(), n + 1); value.resize(n); return value;
}
void edit(HWND field) {
    SendMessageW(field, EM_SETSEL, 0, 0);
    SendMessageW(field, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(L" "));
}
class Shell final : public ShellAdapter {
    bool notify(DWORD, NOTIFYICONDATAW&) override { return true; }
};
struct Backend {
    std::wstring suffix = widen(createPlatformCryptoProvider().value()->randomUuidV4().value());
    std::wstring pipe = L"\\\\.\\pipe\\phpvpn-wp03-lifecycle-" + suffix;
    std::atomic<unsigned> saves{0}, resets{0}, reads{0}, unexpected{0}, statuses{0};
    bool holdSave = false, holdReset = false, invalid = false, persist = false;
    std::string storedDocument = makeDefaultEditorDocument();
    std::string document() { std::lock_guard lock(mutex); return storedDocument; }
    std::mutex mutex;
    std::condition_variable condition;
    bool released = false, resetReleased = false;
    IpcPipeServer server;
    Backend(bool save = false, bool reset = false) : holdSave(save), holdReset(reset),
        server([this](std::string_view bytes) { return reply(bytes); }, options()) { PHPVPN_CHECK(server.start().ok()); }
    IpcPipeServerOptions options() {
        IpcPipeServerOptions o; o.pipeName = pipe;
        // Independent synthetic lanes keep a held Reset and Save from blocking reads.
        o.classify = [](std::string_view bytes) {
            if (auto r = IpcProtocol::parseRequest(bytes); r && r->command == IpcCommand::set_config) return IpcWorkClass::file;
            if (auto r = WizardIpc::parseRequest(bytes); r && r->command == WizardCommand::get_default_config) return IpcWorkClass::diagnostic;
            return IpcWorkClass::shortControl;
        };
        return o;
    }
    void release() { { std::lock_guard lock(mutex); released = true; } condition.notify_all(); }
    void releaseReset() { { std::lock_guard lock(mutex); resetReleased = true; } condition.notify_all(); }
    ~Backend() { release(); server.stop(); }
    void wait(bool reset = false) { std::unique_lock lock(mutex); condition.wait(lock, [&] { return released || (reset && resetReleased); }); }
    std::string reply(std::string_view bytes) {
        if (const auto request = IpcProtocol::parseRequest(bytes)) {
            if (request->command == IpcCommand::set_config) {
                if (persist) {
                    ConfigWarnings warnings;
                    auto config = parseAndValidateConfig(request->config, warnings);
                    if (!config) { ++saves; return IpcProtocol::buildError(request->requestId, config.error()); }
                    std::lock_guard lock(mutex); storedDocument = request->config;
                }
                ++saves; if (holdSave) wait();
                return invalid ? "{}" : IpcProtocol::buildAcknowledgement(request->requestId);
            }
            if (request->command == IpcCommand::get_status) {
                ++statuses;
                auto fixture = dialogs::fixture();
                dialogs::member(fixture, "requestId") = JsonValue::makeString(request->requestId);
                return Json::serialize(fixture);
            }
        }
        if (const auto request = WizardIpc::parseRequest(bytes)) {
            if (request->command == WizardCommand::get_config_document) {
                ++reads;
                return WizardIpc::buildResult(request->requestId, JsonValue::makeObject({
                    {"configPath", JsonValue::makeString("Z:\\synthetic\\config.json")},
                    {"availability", JsonValue::makeString("valid")}, {"reasonCode", JsonValue::makeNull()},
                    {"revisionToken", JsonValue::makeString("fixture")},
                    {"document", JsonValue::makeString(document())}}));
            }
            if (request->command == WizardCommand::get_default_config) {
                ++resets; if (holdReset) wait(true);
                return WizardIpc::buildResult(request->requestId, JsonValue::makeObject({
                    {"document", JsonValue::makeString(makeDefaultEditorDocument())}}));
            }
        }
        ++unexpected; return {};
    }
    std::unique_ptr<TrayApplication> app() {
        auto value = std::make_unique<TrayApplication>(TrayRuntime{pipe, L"Local\\wp03-save-" + suffix, std::make_unique<Shell>(), {}});
        PHPVPN_CHECK(value->initialise(GetModuleHandleW(nullptr)));
        SendMessageW(value->window(), WM_COMMAND, IDM_EDIT_CONFIG, 0);
        PHPVPN_CHECK(until([&] { return value->editorLifecycleSnapshot().open; }));
        return value;
    }
};
}
PHPVPN_TEST(WP03SaveWindows, SnapshotGenerationsDiscardAndUnknownPreserveNativeDraft) {
    EditorWindow editor(L"private-unused-endpoint");
    unsigned saves = 0;
    editor.open(GetModuleHandleW(nullptr), L"Edit Config", "abc", [&](const std::string&) { ++saves; return false; });
    const auto clean = editor.lifecycleSnapshot();
    PHPVPN_CHECK(clean.open && !clean.dirty && !clean.saving && !clean.unknown && clean.deadlineMs == 0);
    const auto field = GetDlgItem(editor.window(), IDC_TEXT_AREA);
    edit(field);
    const auto dirty = editor.lifecycleSnapshot();
    PHPVPN_CHECK(dirty.dirty && dirty.draftGeneration > clean.draftGeneration);
    PHPVPN_CHECK(!editor.approvedDiscard(clean));
    const auto op = editor.beginSave(GetTickCount64() + 30000).value();
    const auto saving = editor.lifecycleSnapshot();
    PHPVPN_CHECK(saving.saving && saving.dirty && !saving.unknown && saving.deadlineMs == op.deadlineMs);
    PHPVPN_CHECK(!editor.beginSave(GetTickCount64() + 40000));
    PHPVPN_CHECK_EQ(editor.lifecycleSnapshot().deadlineMs, op.deadlineMs);
    for (auto message : {WM_CLOSE, WM_COMMAND}) SendMessageW(editor.window(), message, message == WM_COMMAND ? IDC_CANCEL : 0, 0);
    PHPVPN_CHECK_EQ(editor.lifecycleSnapshot(), saving);
    PHPVPN_CHECK(!editor.approvedDiscard(saving));
    for (unsigned i = 0; i < 4; ++i) {
        auto foreign = op;
        if (i == 0) ++foreign.windowGeneration;
        if (i == 1) ++foreign.draftGeneration;
        if (i == 2) ++foreign.operationGeneration;
        if (i == 3) ++foreign.deadlineMs;
        PHPVPN_CHECK(!editor.finishSave({foreign, SaveOutcome::confirmed, ErrorCode::none}));
    }
    dialogs::answer(IDOK, [&] { PHPVPN_CHECK(editor.finishSave({op, SaveOutcome::rejected, ErrorCode::config_invalid_json})); });
    PHPVPN_CHECK(!editor.lifecycleSnapshot().saving && !editor.lifecycleSnapshot().unknown && editor.lifecycleSnapshot().dirty);
    PHPVPN_CHECK_EQ(text(field), L" abc");
    PHPVPN_CHECK(SendMessageW(field, EM_CANUNDO, 0, 0));
    const auto op2 = editor.beginSave(GetTickCount64() + 30000).value();
    PHPVPN_CHECK(op2.operationGeneration > op.operationGeneration);
    PHPVPN_CHECK(!editor.finishSave({op, SaveOutcome::confirmed, ErrorCode::none}));
    dialogs::answer(IDOK, [&] { editor.finishSave({op2, SaveOutcome::unknown, ErrorCode::timeout}); });
    const auto unknown = editor.lifecycleSnapshot();
    PHPVPN_CHECK(unknown.unknown && !unknown.saving && unknown.dirty && unknown.deadlineMs == op2.deadlineMs);
    SendMessageW(editor.window(), WM_COMMAND, IDC_SAVE, 0);
    PHPVPN_CHECK_EQ(saves, 0u);
    dialogs::answer(IDNO, [&] { SendMessageW(editor.window(), WM_CLOSE, 0, 0); });
    PHPVPN_CHECK_EQ(editor.lifecycleSnapshot(), unknown);
    dialogs::dialogTexts.clear();
    PHPVPN_CHECK(!editor.approvedDiscard(dirty));
    PHPVPN_CHECK(editor.approvedDiscard(unknown));
    pump();
    PHPVPN_CHECK(dialogs::dialogTexts.empty());
    PHPVPN_CHECK(!editor.lifecycleSnapshot().open);
    editor.open(GetModuleHandleW(nullptr), L"Edit Config", "new draft", [](const std::string&) { return false; });
    PHPVPN_CHECK(!editor.finishSave({op2, SaveOutcome::confirmed, ErrorCode::none}));
    PHPVPN_CHECK_EQ(text(GetDlgItem(editor.window(), IDC_TEXT_AREA)), L"new draft");
    SendMessageW(editor.window(), WM_CLOSE, 0, 0);
    PHPVPN_CHECK(!editor.window());
}
PHPVPN_TEST(WP03SaveWindows, OriginalDeadlineExpiresThroughProductionEditorPolling) {
    Backend backend(true, true);
    CommandReader reader(backend.pipe);
    EditorWindow editor(backend.pipe);
    std::uint64_t deadlineObservedMs = 0, deadlineDialogReturnedMs = 0;
    editor.open(GetModuleHandleW(nullptr), L"Edit Config", "old", [](const std::string&) { return false; });
    dialogs::answer(IDYES, [&] { SendMessageW(editor.window(), WM_COMMAND, 304, 0); });
    PHPVPN_CHECK(until([&] { return backend.resets == 1; }));
    PHPVPN_CHECK(editor.approvedDiscard(editor.lifecycleSnapshot()));
    editor.open(GetModuleHandleW(nullptr), L"Edit Config", "{}", [&](const std::string& bytes) {
        const auto op = editor.beginSave(GetTickCount64() + 150).value();
        PHPVPN_CHECK(reader.requestSave(op, bytes, nullptr, 0)); return false;
    }, {}, [&] {
        if (auto result = reader.takeSave()) {
            deadlineObservedMs = GetTickCount64();
            editor.finishSave(*result);
            deadlineDialogReturnedMs = GetTickCount64();
        }
    }, [&] { reader.abandon(); });
    SendMessageW(editor.window(), WM_COMMAND, IDC_SAVE, 0);
    const auto snapshot = editor.lifecycleSnapshot();
    backend.releaseReset(); // A stale Reset completion must preserve the Save deadline timer.
    dialogs::answer(IDOK, [&] { PHPVPN_CHECK(until([&] { return editor.lifecycleSnapshot().unknown; })); });
    PHPVPN_CHECK_EQ(editor.lifecycleSnapshot().deadlineMs, snapshot.deadlineMs);
    PHPVPN_CHECK(GetTickCount64() < snapshot.deadlineMs + 250);
    std::cout << "deadline observation offset "
              << static_cast<std::int64_t>(deadlineObservedMs) - static_cast<std::int64_t>(snapshot.deadlineMs)
              << " ms, finishSave including native dialog " << deadlineDialogReturnedMs - deadlineObservedMs << " ms\n";
    backend.release(); backend.server.stop();
    pump();
    PHPVPN_CHECK(!reader.takeSave());
    PHPVPN_CHECK(editor.lifecycleSnapshot().unknown);
    PHPVPN_CHECK_EQ(backend.saves.load(), 1u);
    editor.approvedDiscard(editor.lifecycleSnapshot());
}
PHPVPN_TEST(WP03SaveWindows, ProductionTrayRoutesConfirmedAndInvalidRepliesOnlyToLiveEditor) {
    for (bool invalid : {false, true}) {
        Backend backend; backend.invalid = invalid;
        auto app = backend.app();
        const auto window = editorWindow();
        edit(GetDlgItem(window, IDC_TEXT_AREA));
        dialogs::dialogTexts.clear();
        dialogs::answer(IDOK, [&] {
            SendMessageW(window, WM_COMMAND, IDC_SAVE, 0);
            PHPVPN_CHECK(until([&] { return !app->editorLifecycleSnapshot().saving; }));
        });
        PHPVPN_CHECK_EQ(backend.saves.load(), 1u);
        PHPVPN_CHECK_EQ(backend.reads.load(), 1u);
        PHPVPN_CHECK_EQ(backend.unexpected.load(), 0u);
        if (invalid) {
            const auto snapshot = app->editorLifecycleSnapshot();
            PHPVPN_CHECK(snapshot.open && snapshot.unknown && snapshot.dirty);
            PHPVPN_CHECK(app->approvedDiscardEditor(snapshot));
        } else PHPVPN_CHECK(!app->editorLifecycleSnapshot().open);
        PHPVPN_CHECK(!dialogs::dialogTexts.empty());
    }
    pump();
}
PHPVPN_TEST(WP03SaveWindows, TwentyFourCreateSaveResetEndSessionDestroyCycles) {
    std::uint64_t worst = 0;
    struct Cycle { LifecycleTime start, editorEnded, checked, destroyed; std::uint64_t gateMs; };
    std::array<Cycle, 24> cycles{};
    for (unsigned i = 0; i < 24; ++i) {
        Backend backend(true, true);
        auto app = backend.app();
        auto window = editorWindow();
        edit(GetDlgItem(window, IDC_TEXT_AREA));
        dialogs::answer(IDYES, [&] { SendMessageW(window, WM_COMMAND, 304, 0); });
        PHPVPN_CHECK(until([&] { return backend.resets == 1; }));
        PHPVPN_CHECK(app->approvedDiscardEditor(app->editorLifecycleSnapshot()));
        SendMessageW(app->window(), WM_COMMAND, IDM_EDIT_CONFIG, 0);
        PHPVPN_CHECK(until([&] { return app->editorLifecycleSnapshot().open; }));
        window = editorWindow();
        edit(GetDlgItem(window, IDC_TEXT_AREA));
        SendMessageW(window, WM_COMMAND, IDC_SAVE, 0);
        PHPVPN_CHECK(until([&] { return backend.saves == 1; }));
        const auto saving = app->editorLifecycleSnapshot();
        PHPVPN_CHECK(saving.saving);
        PHPVPN_CHECK_EQ(SendMessageW(window, WM_QUERYENDSESSION, 0, ENDSESSION_LOGOFF), TRUE);
        SendMessageW(window, WM_ENDSESSION, FALSE, ENDSESSION_LOGOFF);
        PHPVPN_CHECK_EQ(app->editorLifecycleSnapshot(), saving);
        dialogs::dialogTexts.clear();
        auto& timing = cycles[i];
        const auto start = GetTickCount64();
        timing.start = lifecycleTime();
        SendMessageW(window, WM_ENDSESSION, TRUE, ENDSESSION_LOGOFF);
        timing.editorEnded = lifecycleTime();
        const auto closed = app->editorLifecycleSnapshot();
        PHPVPN_CHECK(!closed.open && !closed.saving && !closed.unknown);
        PHPVPN_CHECK(closed.windowGeneration > saving.windowGeneration && closed.operationGeneration > saving.operationGeneration);
        timing.checked = lifecycleTime();
        app.reset();
        timing.destroyed = lifecycleTime();
        timing.gateMs = GetTickCount64() - start;
        worst = (std::max)(worst, timing.gateMs);
        // Release the backend only after every UI owner has been destroyed.
        backend.release(); backend.server.stop();
        pump();
        PHPVPN_CHECK(dialogs::dialogTexts.empty());
        PHPVPN_CHECK_EQ(backend.saves.load(), 1u);
        PHPVPN_CHECK_EQ(backend.resets.load(), 1u);
        PHPVPN_CHECK_EQ(backend.reads.load(), 2u);
        PHPVPN_CHECK_EQ(backend.unexpected.load(), 0u);
    }
    LARGE_INTEGER frequency{};
    PHPVPN_CHECK(QueryPerformanceFrequency(&frequency));
    for (unsigned i = 0; i < cycles.size(); ++i) {
        const auto& c = cycles[i];
        const auto ms = [&](const LifecycleTime& a, const LifecycleTime& b) {
            return 1000.0 * (b.wall.QuadPart - a.wall.QuadPart) / frequency.QuadPart;
        };
        std::cout << "cycle " << i << ": editor_ms=" << ms(c.start, c.editorEnded)
                  << " checks_ms=" << ms(c.editorEnded, c.checked)
                  << " app_destroy_ms=" << ms(c.checked, c.destroyed)
                  << " wall_ms=" << ms(c.start, c.destroyed)
                  << " ui_thread_cycles=" << c.destroyed.threadCycles - c.start.threadCycles
                  << " gate_ms=" << c.gateMs << '\n';
    }
    std::cout << "24 Editor/Save/Reset end-session + destroy cycles, worst " << worst << " ms\n";
    PHPVPN_CHECK(worst < 100);
}

PHPVPN_TEST(WP03SaveWindows, HttpHeadersEditorSaveReloadAndReset) {
    Backend backend;
    backend.persist = true;
    auto dom = Json::parse(makeDefaultEditorDocument()).value();
    auto& master = dialogs::member(dom, "masterSetup");
    const std::string headers = "X-Editor: first\nX-Trace: second\nX-Quote: \"a\\b\"\nX-Utf8: \xc5\xbe";
    dialogs::member(master, "httpHeaders") = JsonValue::makeString(headers);
    std::reverse(master.objectValue().begin(), master.objectValue().end());
    backend.storedDocument = Json::serialize(dom);
    auto app = backend.app();
    auto field = GetDlgItem(editorWindow(), IDC_TEXT_AREA);
    auto shown = Json::parse(narrow(text(field)));
    PHPVPN_REQUIRE_OK(shown);
    PHPVPN_CHECK_EQ(Json::serialize(*shown->find("masterSetup")), Json::serialize(master));
    const std::string changed = headers + "\nX-Edited: saved";
    dialogs::member(master, "httpHeaders") = JsonValue::makeString(changed);
    const auto input = widen(Json::serialize(dom));
    SendMessageW(field, EM_SETSEL, 0, -1);
    SendMessageW(field, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(input.c_str()));
    SendMessageW(editorWindow(), WM_COMMAND, MAKEWPARAM(IDC_TEXT_AREA, EN_CHANGE), reinterpret_cast<LPARAM>(field));
    dialogs::answer(IDOK, [&] {
        SendMessageW(editorWindow(), WM_COMMAND, IDC_SAVE, 0);
        PHPVPN_CHECK(until([&] { return !app->editorLifecycleSnapshot().open; }, 5000));
    });
    PHPVPN_CHECK_EQ(backend.saves.load(), 1u);
    auto saved = Json::parse(backend.document());
    PHPVPN_REQUIRE_OK(saved);
    PHPVPN_CHECK_EQ(Json::serialize(*saved->find("masterSetup")), Json::serialize(master));
    SendMessageW(app->window(), WM_COMMAND, IDM_EDIT_CONFIG, 0);
    PHPVPN_CHECK(until([&] { return app->editorLifecycleSnapshot().open; }));
    field = GetDlgItem(editorWindow(), IDC_TEXT_AREA);
    auto reopened = Json::parse(narrow(text(field)));
    PHPVPN_REQUIRE_OK(reopened);
    PHPVPN_CHECK_EQ(reopened->find("masterSetup")->find("httpHeaders")->stringValue(), changed);
    dialogs::answer(IDYES, [&] { SendMessageW(editorWindow(), WM_COMMAND, 304, 0); });
    PHPVPN_CHECK(until([&] {
        auto current = Json::parse(narrow(text(field)));
        return current && current->find("masterSetup")->find("httpHeaders")->stringValue() == kDefaultHttpHeaders;
    }, 5000));
    auto reset = Json::parse(narrow(text(field)));
    PHPVPN_REQUIRE_OK(reset);
    PHPVPN_CHECK_EQ(reset->find("masterSetup")->find("httpHeaders")->stringValue(), kDefaultHttpHeaders);
    PHPVPN_CHECK_EQ(backend.saves.load(), 1u);
    PHPVPN_CHECK(app->approvedDiscardEditor(app->editorLifecycleSnapshot()));
}

PHPVPN_TEST(WP03SaveWindows, HttpHeadersRejectedSaveReportsNameAndLineWithoutValue) {
    Backend backend;
    backend.persist = true;
    auto app = backend.app();
    const auto original = backend.document();
    auto dom = Json::parse(original).value();
    dialogs::member(dialogs::member(dom, "masterSetup"), "httpHeaders") = JsonValue::makeString("X-Test: first\nx-test: secret-marker");
    const auto field = GetDlgItem(editorWindow(), IDC_TEXT_AREA);
    const auto input = widen(Json::serialize(dom));
    SendMessageW(field, EM_SETSEL, 0, -1);
    SendMessageW(field, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(input.c_str()));
    dialogs::dialogTexts.clear();
    dialogs::answer(IDOK, [&] {
        SendMessageW(editorWindow(), WM_COMMAND, IDC_SAVE, 0);
        PHPVPN_CHECK(until([&] { return !app->editorLifecycleSnapshot().saving; }, 5000));
    });
    PHPVPN_CHECK_EQ(backend.document(), original);
    PHPVPN_CHECK(app->editorLifecycleSnapshot().open && app->editorLifecycleSnapshot().dirty);
    PHPVPN_CHECK(!app->editorLifecycleSnapshot().unknown);
    PHPVPN_CHECK(std::any_of(dialogs::dialogTexts.begin(), dialogs::dialogTexts.end(), [](const auto& text) {
        return text.find(L"masterSetup.httpHeaders line 2: x-test") != std::wstring::npos;
    }));
    for (const auto& text : dialogs::dialogTexts) PHPVPN_CHECK(text.find(L"secret-marker") == std::wstring::npos);
    PHPVPN_CHECK(app->approvedDiscardEditor(app->editorLifecycleSnapshot()));
}
