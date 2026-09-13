#include "harness.h"
#include "../../apps/tray/status_reader.h"
#include "../../apps/tray/window_utils.h"
#include "platform/windows/ipc_pipe.h"
#include "core/crypto/crypto_provider.h"
#include <atomic>
#include <condition_variable>

namespace {
using namespace phpvpn;
using namespace phpvpn::tray;
constexpr char id[] = "12345678-1234-4234-8234-123456789abc";
constexpr char otherId[] = "12345678-1234-4234-8234-123456789abd";
SaveOperation operation(std::uint64_t sequence = 1, DWORD timeout = 1000) { return {2, 3, sequence, GetTickCount64() + timeout}; }
std::optional<SaveResult> waitResult(CommandReader& reader) {
    const auto end = GetTickCount64() + 2000;
    do { if (auto result = reader.takeSave()) return result; Sleep(1); } while (GetTickCount64() < end);
    return {};
}
struct Backend {
    std::wstring pipe = L"\\\\.\\pipe\\phpvpn-wp03-save-" + widen(createPlatformCryptoProvider().value()->randomUuidV4().value());
    std::atomic<unsigned> calls{0}, completed{0};
    std::mutex mutex;
    std::condition_variable condition;
    bool hold = false, released = false;
    std::string responseKind = "ok";
    IpcPipeServer server;
    explicit Backend(bool blocked = false) : hold(blocked), server([this](std::string_view bytes) {
        auto request = IpcProtocol::parseRequest(bytes);
        if (!request || request->command != IpcCommand::set_config) return std::string{};
        ++calls;
        if (hold) { std::unique_lock lock(mutex); condition.wait(lock, [&] { return released; }); }
        ++completed;
        if (responseKind == "lost") return std::string{};
        if (responseKind == "invalid") return std::string("{}");
        if (responseKind == "rejected") return IpcProtocol::buildError(request->requestId, Error(ErrorCode::config_invalid_json));
        if (responseKind == "write") return IpcProtocol::buildError(request->requestId, Error(ErrorCode::config_write_failed));
        return IpcProtocol::buildAcknowledgement(responseKind == "foreign" ? otherId : request->requestId);
    }, options()) { PHPVPN_CHECK(server.start().ok()); }
    IpcPipeServerOptions options() { IpcPipeServerOptions result; result.pipeName = pipe; return result; }
    void release() { { std::lock_guard lock(mutex); released = true; } condition.notify_all(); }
    ~Backend() { release(); server.stop(); }
    bool received(unsigned count = 1) {
        const auto end = GetTickCount64() + 1000;
        while (calls < count && GetTickCount64() < end) Sleep(1);
        return calls >= count;
    }
};
}
PHPVPN_TEST(WP03Save, ClosedEnvelopeAndConservativePublicationClassification) {
    const SaveOperation op{1, 2, 3, 100};
    const auto ack = IpcProtocol::buildAcknowledgement(id);
    PHPVPN_CHECK_EQ(classifySaveResponse(op, id, ack, true, 99).outcome, SaveOutcome::confirmed);
    PHPVPN_CHECK_EQ(classifySaveResponse(op, id, ack, true, 100).outcome, SaveOutcome::unknown);
    PHPVPN_CHECK_EQ(classifySaveResponse(op, otherId, ack, true, 99).outcome, SaveOutcome::unknown);
    PHPVPN_CHECK_EQ(classifySaveResponse(op, id, {}, false, 120).outcome, SaveOutcome::notSent);
    for (auto bytes : {std::string{}, std::string("{}"),
        std::string("{\"ipcVersion\":1,\"requestId\":\"") + id + "\",\"result\":\"ok\",\"extra\":0}",
        std::string("{\"ipcVersion\":1,\"ipcVersion\":1,\"requestId\":\"") + id + "\",\"result\":\"ok\"}",
        std::string("{\"ipcVersion\":1.0,\"requestId\":\"") + id + "\",\"result\":\"ok\"}",
        std::string("{\"ipcVersion\":1,\"requestId\":\"") + id + "\",\"result\":true}"})
        PHPVPN_CHECK_EQ(classifySaveResponse(op, id, bytes, true, 99).outcome, SaveOutcome::unknown);
    for (auto code : {ErrorCode::busy, ErrorCode::config_invalid_json, ErrorCode::config_invalid_cidr, ErrorCode::config_too_large})
        PHPVPN_CHECK_EQ(classifySaveResponse(op, id, IpcProtocol::buildError(id, Error(code)), true, 99).outcome, SaveOutcome::rejected);
    for (auto code : {ErrorCode::config_write_failed, ErrorCode::config_unreadable, ErrorCode::journal_ambiguous,
        ErrorCode::internal, ErrorCode::timeout, ErrorCode::config_directory_security_invalid})
        PHPVPN_CHECK_EQ(classifySaveResponse(op, id, IpcProtocol::buildError(id, Error(code)), true, 99).outcome, SaveOutcome::unknown);
    auto error = IpcProtocol::buildError(id, Error(ErrorCode::busy));
    error.insert(error.size() - 2, ",\"extra\":true");
    PHPVPN_CHECK_EQ(classifySaveResponse(op, id, error, true, 99).outcome, SaveOutcome::unknown);
}
PHPVPN_TEST(WP03Save, RealReaderPreservesIdentityAndClassifiesEveryTransportResult) {
    Backend backend;
    CommandReader reader(backend.pipe);
    unsigned sequence = 0;
    for (const auto& [kind, expected] : {std::pair{"ok", SaveOutcome::confirmed}, {"rejected", SaveOutcome::rejected},
        {"write", SaveOutcome::unknown}, {"foreign", SaveOutcome::unknown}, {"invalid", SaveOutcome::unknown}, {"lost", SaveOutcome::unknown}}) {
        backend.responseKind = kind;
        const auto op = operation(++sequence);
        PHPVPN_CHECK(reader.requestSave(op, "{}", nullptr, 0));
        PHPVPN_CHECK(!reader.requestSave(operation(100), "{}", nullptr, 0));
        auto result = waitResult(reader);
        PHPVPN_CHECK(result.has_value());
        if (result) { PHPVPN_CHECK_EQ(result->operation, op); PHPVPN_CHECK_EQ(result->outcome, expected); }
        PHPVPN_CHECK(!reader.takeSave());
    }
    PHPVPN_CHECK_EQ(backend.calls.load(), sequence);
}
PHPVPN_TEST(WP03Save, NotSentAndOriginalDeadlineWithoutRenewalOrRetry) {
    CommandReader absent(L"\\\\.\\pipe\\phpvpn-wp03-absent-" + widen(createPlatformCryptoProvider().value()->randomUuidV4().value()));
    const auto op = operation();
    PHPVPN_CHECK(absent.requestSave(op, "{}", nullptr, 0));
    auto missing = waitResult(absent);
    PHPVPN_CHECK(missing && missing->outcome == SaveOutcome::notSent && missing->operation == op);
    Backend backend(true);
    CommandReader reader(backend.pipe);
    const auto shortOp = operation(2, 100);
    PHPVPN_CHECK(reader.requestSave(shortOp, "{}", nullptr, 0));
    PHPVPN_CHECK(backend.received());
    const auto result = waitResult(reader);
    PHPVPN_CHECK(result && result->outcome == SaveOutcome::unknown && result->operation == shortOp);
    PHPVPN_CHECK(GetTickCount64() < shortOp.deadlineMs + 250);
    backend.release();
    Sleep(30);
    PHPVPN_CHECK(!reader.takeSave());
    PHPVPN_CHECK_EQ(backend.calls.load(), 1u);
}
PHPVPN_TEST(WP03Save, TwentyFourAbandonedWorkersOutliveReadersWithoutLateNotification) {
    const auto target = CreateWindowExW(0, L"STATIC", L"wp03 private target", 0, 0, 0, 1, 1,
        HWND_MESSAGE, nullptr, GetModuleHandleW(nullptr), nullptr);
    PHPVPN_CHECK(target != nullptr);
    std::uint64_t worst = 0;
    for (unsigned i = 0; i < 24; ++i) {
        Backend backend(true);
        {
            CommandReader reader(backend.pipe);
            PHPVPN_CHECK(reader.requestSave(operation(i + 1), "{}", target, WM_APP + 100));
            PHPVPN_CHECK(backend.received());
            const auto started = GetTickCount64();
            reader.abandon();
            worst = (std::max)(worst, GetTickCount64() - started);
            PHPVPN_CHECK(!reader.takeSave());
        }
        backend.release();
        backend.server.stop();
        Sleep(2);
        MSG message{};
        PHPVPN_CHECK(!PeekMessageW(&message, target, WM_APP + 100, WM_APP + 100, PM_REMOVE));
        PHPVPN_CHECK_EQ(backend.calls.load(), 1u);
    }
    DestroyWindow(target);
    std::cout << "24 abandoned Save workers, worst abandon " << worst << " ms\n";
    PHPVPN_CHECK(worst < 100);
}

#include "core/protocol/http_headers.h"
#include "core/ipc/wizard_ipc.h"
PHPVPN_TEST(WP03Save, HttpHeadersErrorsExposeOnlyCuratedValidationDetails) {
    const SaveOperation op{1, 2, 3, 100};
    for (const auto headers : {"Host: secret-marker", "X: first\nx: secret-marker", "X: secret-marker\n\nbody", ""}) {
        auto checked = normalizeHttpHeaders(headers);
        PHPVPN_CHECK(!checked);
        const auto envelope = IpcProtocol::buildError(id, checked.error());
        const auto parsed = Json::parse(envelope).value();
        const auto& description = parsed.find("error")->find("description")->stringValue();
        PHPVPN_CHECK(description.find("masterSetup.httpHeaders line ") != std::string::npos);
        PHPVPN_CHECK(description.find("secret-marker") == std::string::npos);
        auto classified = classifySaveResponse(op, id, envelope, true, 99);
        PHPVPN_CHECK_EQ(classified.outcome, SaveOutcome::rejected);
        PHPVPN_CHECK_EQ(classified.validationDescription, description);
        const auto wizard = WizardIpc::safeError(checked.error());
        PHPVPN_CHECK_EQ(wizard.description, description);
        PHPVPN_REQUIRE_OK(WizardIpc::parseError(*Json::parse(WizardIpc::buildError(id, wizard))->find("error")));
    }
    for (const auto unsafe : {"secret-marker", "masterSetup.httpHeaders line 0: invalid field name",
         "masterSetup.httpHeaders line 2: Host: secret-marker is managed by the application or transport",
         "masterSetup.httpHeaders line 2: invalid field name\nsecret-marker"}) {
        PHPVPN_CHECK(!isHttpHeadersErrorDescription(unsafe));
        auto envelope = Json::parse(IpcProtocol::buildError(id, Error(ErrorCode::config_invalid_value, unsafe))).value();
        PHPVPN_CHECK_EQ(envelope.find("error")->find("description")->stringValue(), errorCodeDescription(ErrorCode::config_invalid_value));
        for (auto& member : envelope.objectValue()) if (member.first == "error")
            for (auto& field : member.second.objectValue()) if (field.first == "description") field.second = JsonValue::makeString(unsafe);
        const auto untrusted = classifySaveResponse(op, id, Json::serialize(envelope), true, 99);
        PHPVPN_CHECK_EQ(untrusted.outcome, SaveOutcome::unknown);
        PHPVPN_CHECK(untrusted.validationDescription.empty());
    }
}
