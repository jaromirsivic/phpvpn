// GTK-free Configuration Wizard of the Linux tray (WP10a): the controller that
// ports the Windows WizardWindow workflow, driven against a scripted Unix-socket
// Wizard service with a fake clock, plus the HTTP error detail and HTTP headers
// dialog texts. No GUI library is linked.
#include "harness.h"

#include <stdlib.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <map>
#include <mutex>
#include <thread>

#include "core/config/config.h"
#include "core/config/config_defaults.h"
#include "core/crypto/crypto_provider.h"
#include "core/ipc/ipc_protocol.h"
#include "core/ipc/wizard_ipc.h"
#include "core/protocol/json.h"
#include "core/protocol/uuid.h"
#include "core/wizard/wizard_defaults.h"
#include "ipc_async.h"
#include "lifecycle.h"
#include "platform/linux/ipc_unix.h"
#include "tray_text.h"
#include "wizard_controller.h"
#include "wizard_dialog_text.h"
#include "wizard_text.h"

namespace phpvpn::tray {
namespace {
std::string uuid() {
    auto crypto = createPlatformCryptoProvider();
    return crypto ? crypto.value()->randomUuidV4().value() : std::string{};
}

// Thread-safe stand-in for the GLib main context.
class UiQueue {
public:
    Dispatcher dispatcher() {
        return [this](std::function<void()> task) {
            { std::lock_guard lock(mutex_); tasks_.push_back(std::move(task)); }
            changed_.notify_all();
        };
    }
    template <class Predicate>
    bool pumpUntil(Predicate done, int limitMs = 4000) {
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

class TempDir {
public:
    TempDir() {
        char pattern[] = "/tmp/phpvpn-wizard-XXXXXX";
        const char* made = mkdtemp(pattern);
        path_ = made ? made : "";
    }
    ~TempDir() { std::error_code error; std::filesystem::remove_all(path_, error); }
    std::string socket() const { return path_ + "/control.sock"; }
private:
    std::string path_;
};

HttpDiagnostic diagnostic() {
    auto message = [](std::string text) {
        BoundedHttpMessage bounded;
        bounded.originalSizeBytes = text.size();
        bounded.prefix = std::move(text);
        return bounded;
    };
    HttpDiagnostic d;
    d.action = HttpAction::info;
    d.responseComplete = true;
    d.requestHeadersSource = RequestHeadersSource::winhttp;
    d.responseHeadersSource = ResponseHeadersSource::winhttp;
    d.request = message("POST /phpvpn.php?action=info HTTP/1.1\r\nHost: 192.0.2.1\r\n\r\nbody");
    d.response = message("HTTP/1.1 200 OK\r\nContent-Length: 3\r\n\r\n\x01ok");
    return d;
}

// Scripted Wizard service. The handler runs on the server's worker threads.
class FakeService {
public:
    enum class Check { success, pending, cryptoMismatch, unauthorized };
    explicit FakeService(const TempDir& dir) : server_([this](std::string_view request) { return handle(request); }, options(dir)) {}
    ~FakeService() { server_.stop(); }
    Status start() { return server_.start(); }

    std::vector<std::string> log() const { std::lock_guard lock(mutex_); return log_; }
    std::size_t count(const std::string& command) const {
        std::lock_guard lock(mutex_);
        return static_cast<std::size_t>(std::count(log_.begin(), log_.end(), command));
    }
    std::optional<WizardRequest> last(WizardCommand command) const {
        std::lock_guard lock(mutex_);
        for (auto it = requests_.rbegin(); it != requests_.rend(); ++it) if (it->command == command) return *it;
        return std::nullopt;
    }
    void setVpn(ConnectionState state, ConnectionPhase phase) { std::lock_guard lock(mutex_); vpn_ = state; phase_ = phase; }
    void setCheck(Check check) { std::lock_guard lock(mutex_); check_ = check; }
    void setKeepaliveBusy(bool busy) { std::lock_guard lock(mutex_); keepaliveBusy_ = busy; }
    void setFinishPending(bool pending) { std::lock_guard lock(mutex_); finishPending_ = pending; }
    void setCloseDelayMs(int ms) { std::lock_guard lock(mutex_); closeDelayMs_ = ms; }

private:
    static IpcSocketServerOptions options(const TempDir& dir) {
        IpcSocketServerOptions result;
        result.socketPath = dir.socket();
        result.expectedOwnerUid = geteuid();
        return result;
    }
    JsonValue view(WizardMode mode) const {
        auto document = makeNewWizardDocument();
        WizardView value;
        value.sessionId = session_;
        value.serviceInstanceId = instance_;
        value.availability = ConfigAvailability::missing;
        value.mode = mode;
        value.fields = document.fields;
        value.context = document.context;
        value.connectionEpoch = epoch_;
        value.vpnState = vpn_;
        value.vpnPhase = phase_;
        return WizardIpc::viewValue(value);
    }
    std::string status(const std::string& id) const {
        StatusSnapshot snapshot;
        snapshot.state = vpn_;
        snapshot.phase = phase_;
        snapshot.stateChangedAtUtc = "2026-09-26T10:00:00.000Z";
        snapshot.clientVersion = "1.0.0";
        return IpcProtocol::buildStatusResponse(id, snapshot);
    }
    std::string handle(std::string_view raw) {
        auto request = WizardIpc::parseRequest(raw);
        if (!request) {
            auto v1 = IpcProtocol::parseRequest(raw);
            if (!v1) return "{}";
            std::lock_guard lock(mutex_);
            if (v1->command == IpcCommand::disconnect) {
                log_.push_back("disconnect");
                vpn_ = ConnectionState::disconnected;
                phase_ = ConnectionPhase::idle;
            } else {
                log_.push_back("get-status");
            }
            return status(v1->requestId);
        }
        int delay = 0;
        std::string reply;
        {
            std::lock_guard lock(mutex_);
            const auto& value = *request;
            log_.push_back(WizardIpc::commandName(value.command));
            requests_.push_back(value);
            const auto& id = value.requestId;
            switch (value.command) {
                case WizardCommand::open:
                    session_ = uuid();
                    reply = WizardIpc::buildResult(id, view(WizardMode::create));
                    break;
                case WizardCommand::resume:
                    reply = WizardIpc::buildResult(id, view(WizardMode::create));
                    break;
                case WizardCommand::reset:
                    reply = WizardIpc::buildResult(id, view(WizardMode::reset));
                    break;
                case WizardCommand::keepalive:
                    reply = keepaliveBusy_ ? WizardIpc::buildError(id, WizardIpc::safeError(Error(ErrorCode::busy)))
                        : WizardIpc::buildResult(id, JsonValue::makeObject({{"alive", JsonValue::makeBool(true)},
                              {"leaseDurationMs", JsonValue::makeUnsignedInteger(kWizardLeaseMs)}}));
                    break;
                case WizardCommand::check_url: case WizardCommand::check_secret: case WizardCommand::check_access: {
                    WizardOperationResult operation;
                    operation.operationId = uuid();
                    operation.kind = value.command == WizardCommand::check_url ? WizardOperationKind::url
                                   : value.command == WizardCommand::check_secret ? WizardOperationKind::secret
                                                                                  : WizardOperationKind::access;
                    operation.connectionEpoch = epoch_;
                    operation.clientAttemptId = value.clientAttemptId;
                    operation.attemptSequence = value.attemptSequence;
                    if (check_ == Check::success) {
                        operation.state = WizardOperationState::succeeded;
                        if (operation.kind == WizardOperationKind::secret) operation.suggestedIp = "10.99.99.7";
                    } else if (check_ == Check::cryptoMismatch) {
                        operation.state = WizardOperationState::failed;
                        operation.error = WizardIpc::safeError(Error(ErrorCode::crypto_tag_mismatch), {},
                            operation.kind == WizardOperationKind::url ? "serverUrl" : "serverSecret");
                        operation.httpDiagnostic = diagnostic();
                    } else if (check_ == Check::unauthorized) {
                        operation.state = WizardOperationState::failed;
                        operation.error = WizardIpc::safeError(Error(ErrorCode::server_unauthorized), 401, "accessKey",
                            "http://192.0.2.1/phpvpn.php", "/etc/phpvpn/config.json");
                    }
                    operations_[operation.operationId] = operation;
                    reply = WizardIpc::buildResult(id, JsonValue::makeObject({{"operationId", JsonValue::makeString(operation.operationId)},
                        {"operationState", JsonValue::makeString("pending")}}));
                    break;
                }
                case WizardCommand::finish: {
                    WizardOperationResult operation;
                    operation.operationId = uuid();
                    operation.kind = WizardOperationKind::finish;
                    operation.connectionEpoch = epoch_;
                    if (finishPending_) operation.state = WizardOperationState::running;
                    else {
                        operation.state = WizardOperationState::succeeded;
                        operation.receipt = WizardReceipt{value.commitId, "rev-2"};
                    }
                    operations_[operation.operationId] = operation;
                    reply = WizardIpc::buildResult(id, JsonValue::makeObject({{"operationId", JsonValue::makeString(operation.operationId)},
                        {"operationState", JsonValue::makeString("pending")}}));
                    break;
                }
                case WizardCommand::get_operation: {
                    const auto found = operations_.find(value.operationId);
                    reply = found == operations_.end()
                        ? WizardIpc::buildError(id, WizardIpc::safeError(Error(ErrorCode::invalid_argument)))
                        : WizardIpc::buildResult(id, WizardIpc::operationValue(found->second));
                    break;
                }
                case WizardCommand::cancel_attempt:
                    reply = WizardIpc::buildResult(id, JsonValue::makeObject({{"attemptSequence", JsonValue::makeUnsignedInteger(value.attemptSequence)},
                        {"attemptState", JsonValue::makeString("cancelled")}}));
                    break;
                case WizardCommand::get_commit:
                    reply = WizardIpc::buildResult(id, JsonValue::makeObject({{"commitState", JsonValue::makeString("confirmed")},
                        {"receipt", WizardIpc::receiptValue({value.commitId, "rev-3"})}, {"error", JsonValue::makeNull()}}));
                    break;
                case WizardCommand::close:
                    delay = closeDelayMs_;
                    reply = WizardIpc::buildResult(id, JsonValue::makeObject({{"closed", JsonValue::makeBool(true)}}));
                    break;
                default:
                    reply = WizardIpc::buildError(id, WizardIpc::safeError(Error(ErrorCode::ipc_unknown_command)));
                    break;
            }
        }
        if (delay) std::this_thread::sleep_for(std::chrono::milliseconds(delay));
        return reply;
    }

    mutable std::mutex mutex_;
    std::vector<std::string> log_;
    std::vector<WizardRequest> requests_;
    std::map<std::string, WizardOperationResult> operations_;
    ConnectionState vpn_ = ConnectionState::disconnected;
    ConnectionPhase phase_ = ConnectionPhase::idle;
    std::string session_ = uuid(), instance_ = uuid();
    std::uint64_t epoch_ = 3;
    Check check_ = Check::success;
    bool keepaliveBusy_ = false, finishPending_ = false;
    int closeDelayMs_ = 0;
    IpcSocketServer server_;
};

class FakeHost final : public WizardHost {
public:
    bool createWindow() override { ++created; return createResult; }
    void presentWindow() override { ++presented; }
    void destroyWindow() override { ++destroyed; }
    void render() override { ++renders; }
    void refreshDiagnostic() override { ++diagnosticRefreshes; }
    void setFieldText(WizardField field, const std::string& text) override { fieldTexts[field] = text; }
    void focusField(WizardField field, WizardField fallback) override { focused = field; focusFallback = fallback; }
    void notify(std::string_view text, std::string_view title, WizardNotice) override {
        notices.emplace_back(std::string(title), std::string(text));
        if (onNotify) onNotify();
    }
    bool confirm(std::string_view text, std::string_view) override { questions.emplace_back(text); return answer; }
    void closeDetail() override { ++detailCloses; }

    int created = 0, presented = 0, destroyed = 0, renders = 0, diagnosticRefreshes = 0, detailCloses = 0;
    bool createResult = true, answer = true;
    std::map<WizardField, std::string> fieldTexts;
    WizardField focused = WizardField::none, focusFallback = WizardField::none;
    std::vector<std::pair<std::string, std::string>> notices;
    std::vector<std::string> questions;
    std::function<void()> onNotify;
};

// One Wizard under test: service, UI queue, fake clock and controller.
struct Rig {
    TempDir dir;
    FakeService service{dir};
    UiQueue ui;
    FakeHost host;
    std::int64_t now = 1'000'000;
    WizardController wizard{IpcEndpoint{dir.socket(), geteuid()}, ui.dispatcher(), host, [this] { return now; }};
    bool started = service.start().ok();

    bool openAt(WizardStep step) {
        if (!started || !wizard.open()) return false;
        return ui.pumpUntil([&] { return wizard.model().step() == step; });
    }
    // One 500 ms poll round: tick, then deliver whatever the tick sent.
    void poll() { now += 500; wizard.tick(); ui.drainFor(150); }
    template <class Predicate>
    bool pollUntil(Predicate done, int rounds = 10) {
        for (int i = 0; i < rounds && !done(); ++i) poll();
        return done();
    }
};
}  // namespace
}  // namespace phpvpn::tray

using namespace phpvpn;
using namespace phpvpn::tray;

PHPVPN_TEST(tray_linux_wizard, exit_snapshot_folds_every_identity) {
    WizardLifecycleSnapshot base;
    base.open = true;
    base.windowGeneration = 4;
    const auto reference = wizardExitSnapshot(base);
    PHPVPN_CHECK(reference.open);
    PHPVPN_CHECK(!reference.dirty && !reference.unknown && !reference.busy);
    PHPVPN_CHECK(wizardExitSnapshot(base) == reference);
    std::vector<WizardLifecycleSnapshot> variants(10, base);
    variants[0].sessionGeneration = 1;
    variants[1].draftGeneration = 1;
    variants[2].attemptGeneration = 1;
    variants[3].operationGeneration = 1;
    variants[4].attemptSequence = 1;
    variants[5].connectionEpoch = 1;
    variants[6].commitDeadlineMs = 0;
    variants[7].windowGeneration = 5;
    variants[8].dirty = true;
    variants[9].committing = true;
    for (const auto& variant : variants) PHPVPN_CHECK(wizardExitSnapshot(variant).generation != reference.generation);
    auto committing = base;
    committing.committing = true;
    PHPVPN_CHECK(wizardExitSnapshot(committing).busy);
    auto unknown = base;
    unknown.commitUnknown = true;
    PHPVPN_CHECK(wizardExitSnapshot(unknown).unknown);
}

PHPVPN_TEST(tray_linux_wizard, http_error_detail_pages_follow_the_windows_notes) {
    auto d = diagnostic();
    auto pages = httpErrorDetailPages(d);
    PHPVPN_CHECK(pages.has_value());
    if (!pages) return;
    PHPVPN_CHECK_EQ((*pages)[0].note, std::string("Request (Original size: ") + diagnosticSizeKiB(d.request.originalSizeBytes) +
                                          " KiB)\nApplication reconstruction.");
    PHPVPN_CHECK_EQ((*pages)[1].note, std::string("Response (Original size: 0.04 KiB)\nInvalid or non-displayable bytes are shown as \\xHH."));
    PHPVPN_CHECK_EQ((*pages)[0].text, d.request.prefix);
    PHPVPN_CHECK_EQ((*pages)[1].text, std::string("HTTP/1.1 200 OK\r\nContent-Length: 3\r\n\r\n\\x01ok"));

    d.request.prefix = "POST /phpvpn.php HTTP/?\r\nHost: x\r\n\r\n";
    d.request.originalSizeBytes = d.request.prefix.size();
    d.requestHeadersSource = RequestHeadersSource::application;
    d.responseComplete = false;
    d.responseHeadersSource = ResponseHeadersSource::available;
    pages = httpErrorDetailPages(d);
    PHPVPN_CHECK(pages.has_value());
    if (!pages) return;
    PHPVPN_CHECK((*pages)[0].note.find("Application reconstruction. HTTP version unavailable (HTTP/?).\n"
                                       "System request headers unavailable. Showing application headers.") != std::string::npos);
    PHPVPN_CHECK((*pages)[1].note.find("\nIncomplete response: size refers to received content.\n"
                                       "Complete system response headers unavailable. Showing available information.") != std::string::npos);
    d.requestHeadersSource = RequestHeadersSource::merged;
    pages = httpErrorDetailPages(d);
    PHPVPN_CHECK(pages && (*pages)[0].note.find("\nSystem headers merged with original application headers.") != std::string::npos);

    // Truncated message: 63 KiB prefix, the dotted separator and the 1 KiB tail.
    std::string body(70000, 'a');
    BoundedHttpCapture capture;
    PHPVPN_CHECK(capture.append(body));
    d.response = capture.finish();
    pages = httpErrorDetailPages(d);
    PHPVPN_CHECK(pages.has_value());
    if (!pages) return;
    PHPVPN_CHECK((*pages)[1].note.find("\nTruncated: first 63 KiB and last 1 KiB, 4464 bytes omitted. "
                                       "The four dotted lines are a display separator.") != std::string::npos);
    PHPVPN_CHECK_EQ((*pages)[1].text.size(), kHttpDiagnosticPrefix + kHttpDiagnosticSeparator.size() + kHttpDiagnosticTail);

    // A malformed message never opens a window.
    d.response.prefix = "short";
    PHPVPN_CHECK(!httpErrorDetailPages(d).has_value());
}

PHPVPN_TEST(tray_linux_wizard, http_headers_dialog_texts) {
    PHPVPN_CHECK_EQ(httpHeadersDisplayText("A: 1\r\nB: 2"), std::optional<std::string>("A: 1\nB: 2"));
    PHPVPN_CHECK_EQ(httpHeadersDisplayText("A: 1\nB: 2\n"), std::optional<std::string>("A: 1\nB: 2"));
    PHPVPN_CHECK(!httpHeadersDisplayText("").has_value());
    PHPVPN_CHECK(defaultHttpHeadersDisplayText().find('\r') == std::string::npos);
    PHPVPN_CHECK(defaultHttpHeadersDisplayText().starts_with("Cache-Control: no-cache\nPragma: no-cache\nUser-Agent: "));
    PHPVPN_CHECK_EQ(httpHeadersErrorText(Error(ErrorCode::config_too_large)),
                    std::string("The HTTP headers cannot fit in the configuration file (512 KiB limit)."));
    PHPVPN_CHECK_EQ(httpHeadersErrorText(Error(ErrorCode::config_encoding_error)),
                    std::string("The text contains an invalid Unicode character."));
    PHPVPN_CHECK_EQ(httpHeadersErrorText(Error(ErrorCode::config_invalid_value, "masterSetup.httpHeaders line 2: invalid field name")),
                    std::string("masterSetup.httpHeaders line 2: invalid field name"));
    PHPVPN_CHECK(httpHeadersErrorText(ok()).empty());
}

PHPVPN_TEST(tray_linux_wizard, complete_flow_url_secret_identity_contracts_finish) {
    Rig rig;
    PHPVPN_CHECK(rig.started);
    bool confirmedCallback = false;
    PHPVPN_CHECK(rig.wizard.open([&] { confirmedCallback = true; }));
    PHPVPN_CHECK_EQ(rig.host.created, 1);
    PHPVPN_CHECK(rig.wizard.isOpen());
    // Before the view arrives: loading, primary disabled, no Reset without a session.
    auto p = rig.wizard.presentation();
    PHPVPN_CHECK(p.step == WizardStep::loading);
    PHPVPN_CHECK_EQ(p.heading, std::string("Configure this computer"));
    PHPVPN_CHECK_EQ(p.text, std::string("Loading configuration…"));
    PHPVPN_CHECK(!p.resetEnabled);
    PHPVPN_CHECK(rig.ui.pumpUntil([&] { return rig.wizard.model().step() == WizardStep::serverUrl; }));
    PHPVPN_CHECK(!rig.wizard.wizardSnapshot().dirty);

    p = rig.wizard.presentation();
    PHPVPN_CHECK_EQ(p.primaryLabel, std::string("Verify & continue"));
    PHPVPN_CHECK(p.primaryEnabled && p.resetEnabled && !p.backVisible && !p.detailVisible && !p.locked);
    PHPVPN_CHECK_EQ(p.url, std::string("http://MODIFY-THIS-URL/phpvpn.php"));
    PHPVPN_CHECK(p.sidebar[0].active && p.sidebar[0].color == ui::Color::primary && p.sidebar[0].status.empty());
    PHPVPN_CHECK_EQ(p.sidebar[1].status, std::string("Upcoming"));
    PHPVPN_CHECK_EQ(p.sidebar[3].name, std::string("Contracts"));
    PHPVPN_CHECK(p.sidebarText.starts_with("CONFIGURE THIS COMPUTER\n\n1  Server URL\n    Upcoming\n\n"));

    // The placeholder is refused locally, before any IPC, with the focus on the URL.
    rig.wizard.next();
    p = rig.wizard.presentation();
    PHPVPN_CHECK_EQ(p.errorText, std::optional<std::string>("Replace MODIFY-THIS-URL with your server address."));
    PHPVPN_CHECK(p.invalidField == WizardField::url);
    PHPVPN_CHECK(rig.host.focused == WizardField::url);
    // A local refusal creates no attempt, so the step stays pending (Windows model).
    PHPVPN_CHECK(p.sidebar[0].status.empty());
    PHPVPN_CHECK_EQ(rig.service.count("wizard-check-url"), 0u);

    rig.wizard.editUrl("http://192.0.2.1/phpvpn.php");
    PHPVPN_CHECK(rig.wizard.wizardSnapshot().dirty);
    rig.wizard.next();
    p = rig.wizard.presentation();
    PHPVPN_CHECK(p.locked && !p.primaryEnabled);
    PHPVPN_CHECK_EQ(p.statusText, std::string("Checking the server…"));
    PHPVPN_CHECK(rig.pollUntil([&] { return rig.wizard.model().step() == WizardStep::serverSecret; }));
    PHPVPN_CHECK_EQ(rig.service.last(WizardCommand::check_url)->fields.serverUrl, std::string("http://192.0.2.1/phpvpn.php"));
    p = rig.wizard.presentation();
    PHPVPN_CHECK_EQ(p.sidebar[0].status, std::string("Completed"));
    PHPVPN_CHECK_EQ(p.sidebar[0].symbol, std::string("✓"));
    PHPVPN_CHECK(p.backVisible && p.backEnabled);
    PHPVPN_CHECK(!p.useDefaultSecret && p.secretEnabled && p.defaultSecretWarning);
    PHPVPN_CHECK_EQ(p.secretLabel, std::string("Server secret (exact text, including any spaces)"));

    // A failed secret check with a MAC mismatch and an HTTP diagnostic.
    rig.service.setCheck(FakeService::Check::cryptoMismatch);
    rig.wizard.next();
    PHPVPN_CHECK(rig.pollUntil([&] { return rig.wizard.model().error().has_value(); }));
    p = rig.wizard.presentation();
    PHPVPN_CHECK(p.errorText && p.errorText->starts_with(utf8(wizardText::secretMismatchPrefix) + "CHANGE_THIS_SERVER_SECRET"));
    PHPVPN_CHECK(p.invalidField == WizardField::secret);
    PHPVPN_CHECK(p.detailVisible && p.detailEnabled);
    PHPVPN_CHECK(!p.defaultSecretWarning);  // the error panel replaces the warning
    PHPVPN_CHECK_EQ(p.sidebar[1].status, std::string("Failed"));
    PHPVPN_CHECK(p.sidebar[1].color == ui::Color::error);
    // Turning the default off keeps the value; the edit retires the diagnostic and
    // its service copy through one cancel-attempt.
    rig.wizard.setDefaultSecret(false);
    rig.wizard.editSecret("my server secret");
    PHPVPN_CHECK(!rig.wizard.presentation().detailVisible);
    PHPVPN_CHECK(rig.host.detailCloses > 0);
    PHPVPN_CHECK(rig.ui.pumpUntil([&] { return rig.service.count("wizard-cancel-attempt") == 1; }));
    rig.ui.drainFor(100);

    rig.service.setCheck(FakeService::Check::success);
    const auto draftBefore = rig.wizard.wizardSnapshot().draftGeneration;
    rig.wizard.next();
    PHPVPN_CHECK(rig.pollUntil([&] { return rig.wizard.model().step() == WizardStep::identity; }));
    PHPVPN_CHECK_EQ(rig.service.last(WizardCommand::check_secret)->fields.serverSecret, std::string("my server secret"));
    p = rig.wizard.presentation();
    // The verified suggestion replaced the automatic IP: a draft change.
    PHPVPN_CHECK_EQ(p.ip, std::string("10.99.99.7"));
    PHPVPN_CHECK_EQ(p.key, defaultAccessKey("10.99.99.7"));
    PHPVPN_CHECK(rig.wizard.wizardSnapshot().draftGeneration > draftBefore);
    PHPVPN_CHECK(!p.noSuggestion && !p.ipEnabled && !p.keyEnabled && p.defaultKeyWarning);
    PHPVPN_CHECK_EQ(p.identityDescription, utf8(wizardText::identityPrefix) + "10.99.99.0/24" + utf8(wizardText::identityDescription));

    // A 401 with field, HTTP status, server URL and configuration path.
    rig.service.setCheck(FakeService::Check::unauthorized);
    rig.wizard.next();
    PHPVPN_CHECK_EQ(rig.wizard.presentation().statusText, std::string("Checking access…"));
    PHPVPN_CHECK(rig.pollUntil([&] { return rig.wizard.model().error().has_value(); }));
    p = rig.wizard.presentation();
    PHPVPN_CHECK(p.invalidField == WizardField::key);
    PHPVPN_CHECK(p.errorText && p.errorText->ends_with("\nHTTP status: 401\nServer URL: http://192.0.2.1/phpvpn.php\n"
                                                        "Configuration: /etc/phpvpn/config.json"));
    PHPVPN_CHECK(p.errorText && p.errorText->find(WizardIpc::safeError(Error(ErrorCode::server_unauthorized)).suggestedAction) !=
                                    std::string::npos);

    // Turning the default key off and back regenerates the key from the IP.
    rig.wizard.setAutoDetectIp(false);
    rig.wizard.editIp("10.99.99.9");
    PHPVPN_CHECK_EQ(rig.host.fieldTexts[WizardField::key], defaultAccessKey("10.99.99.9"));
    rig.service.setCheck(FakeService::Check::success);
    rig.wizard.next();
    PHPVPN_CHECK(rig.pollUntil([&] { return rig.wizard.model().step() == WizardStep::contracts; }));
    p = rig.wizard.presentation();
    PHPVPN_CHECK_EQ(p.primaryLabel, std::string("Finish"));
    PHPVPN_CHECK(p.createContracts && p.contractsEnabled && !p.contractsUnsupported && p.contractsHint);
    PHPVPN_CHECK_EQ(p.contractCounts[0], 0u);
    PHPVPN_CHECK_EQ(p.contractCounts[1], 13u);  // .2 to .15 without self
    PHPVPN_CHECK_EQ(p.contractCounts[2], 13u);
    rig.wizard.setCreateContracts(false);
    PHPVPN_CHECK_EQ(rig.wizard.presentation().contractCounts[2], 0u);
    rig.wizard.setCreateContracts(true);

    // Back keeps the draft and needs no IPC.
    const auto before = rig.service.log().size();
    rig.wizard.back();
    PHPVPN_CHECK(rig.wizard.model().step() == WizardStep::identity);
    rig.ui.drainFor(50);
    PHPVPN_CHECK_EQ(rig.service.log().size(), before);
    rig.wizard.next();
    PHPVPN_CHECK(rig.pollUntil([&] { return rig.wizard.model().step() == WizardStep::contracts; }));

    // Finish: commitId, 15 s deadline, receipt, one notification, one close.
    rig.wizard.next();
    auto snapshot = rig.wizard.wizardSnapshot();
    PHPVPN_CHECK(snapshot.committing);
    PHPVPN_CHECK_EQ(snapshot.commitDeadlineMs, std::optional<std::int64_t>(rig.now + 15000));
    PHPVPN_CHECK(rig.wizard.lifecycleSnapshot().busy);
    PHPVPN_CHECK_EQ(rig.wizard.presentation().statusText, std::string("Saving configuration…"));
    PHPVPN_CHECK(rig.pollUntil([&] { return !rig.wizard.isOpen(); }));
    const auto finish = rig.service.last(WizardCommand::finish);
    PHPVPN_CHECK(finish && Uuid::isValidV4(finish->commitId) && finish->createDefaultContracts);
    PHPVPN_CHECK_EQ(finish->fields.virtualIp, std::string("10.99.99.9"));
    PHPVPN_CHECK_EQ(rig.host.notices.size(), 1u);
    PHPVPN_CHECK_EQ(rig.host.notices[0].first, std::string("Configuration saved"));
    PHPVPN_CHECK_EQ(rig.host.notices[0].second, std::string("The configuration was saved. To connect to the VPN server, click Connect."));
    PHPVPN_CHECK_EQ(rig.service.count("wizard-close"), 1u);
    PHPVPN_CHECK_EQ(rig.host.destroyed, 1);
    PHPVPN_CHECK(confirmedCallback);
    PHPVPN_CHECK_EQ(rig.service.count("wizard-finish"), 1u);
}

PHPVPN_TEST(tray_linux_wizard, keepalive_every_15_s_and_lease_expiry_after_60_s) {
    Rig rig;
    PHPVPN_CHECK(rig.openAt(WizardStep::serverUrl));
    rig.now += 14999;
    rig.wizard.tick();
    rig.ui.drainFor(100);
    PHPVPN_CHECK_EQ(rig.service.count("wizard-keepalive"), 0u);
    rig.now += 1;
    rig.wizard.tick();
    PHPVPN_CHECK(rig.ui.pumpUntil([&] { return rig.service.count("wizard-keepalive") == 1; }));
    rig.ui.drainFor(100);
    // A refused keepalive (busy) is ignored and does not count as contact.
    rig.service.setKeepaliveBusy(true);
    for (int i = 0; i < 4; ++i) { rig.now += 15000; rig.wizard.tick(); rig.ui.drainFor(100); }
    // Keepalives at 30, 45 and 60 s; at 75 s the 60 s lease since the last contact is over.
    PHPVPN_CHECK_EQ(rig.service.count("wizard-keepalive"), 4u);
    PHPVPN_CHECK(rig.wizard.model().step() == WizardStep::sessionExpired);
    const auto p = rig.wizard.presentation();
    PHPVPN_CHECK_EQ(p.primaryLabel, std::string("Open again"));
    PHPVPN_CHECK_EQ(p.heading, std::string("Open a new wizard session"));
    PHPVPN_CHECK(!p.resetEnabled);
    // No keepalive after expiry; Open again starts a new session.
    rig.now += 15000; rig.wizard.tick(); rig.ui.drainFor(100);
    PHPVPN_CHECK_EQ(rig.service.count("wizard-keepalive"), 4u);
    rig.service.setKeepaliveBusy(false);
    rig.wizard.next();
    PHPVPN_CHECK(rig.ui.pumpUntil([&] { return rig.wizard.model().step() == WizardStep::serverUrl; }));
    PHPVPN_CHECK_EQ(rig.service.count("wizard-open"), 2u);
}

PHPVPN_TEST(tray_linux_wizard, disconnect_gate_disconnect_and_continue_then_resume) {
    Rig rig;
    rig.service.setVpn(ConnectionState::connected, ConnectionPhase::running);
    PHPVPN_CHECK(rig.openAt(WizardStep::disconnectGate));
    auto p = rig.wizard.presentation();
    PHPVPN_CHECK_EQ(p.primaryLabel, std::string("Disconnect & continue"));
    PHPVPN_CHECK_EQ(p.eyebrow, std::string("Before setup"));
    PHPVPN_CHECK_EQ(p.heading, std::string("Disconnect the VPN before continuing"));
    PHPVPN_CHECK(p.gateCard && !p.offline && !p.backVisible && p.resetEnabled);
    PHPVPN_CHECK(p.sidebarText.starts_with("CONFIGURE THIS COMPUTER\n\nDisconnect VPN\n\n"));
    // The gate reads the status every 500 ms (one outstanding read).
    rig.poll();
    PHPVPN_CHECK(rig.service.count("get-status") >= 1u);
    rig.wizard.next();
    PHPVPN_CHECK_EQ(rig.wizard.presentation().statusText, std::string("Waiting for the VPN to stop…"));
    PHPVPN_CHECK(rig.ui.pumpUntil([&] { return rig.wizard.model().step() == WizardStep::serverUrl; }));
    PHPVPN_CHECK_EQ(rig.service.count("disconnect"), 1u);
    PHPVPN_CHECK_EQ(rig.service.count("wizard-resume"), 1u);
    PHPVPN_CHECK(!rig.wizard.presentation().locked);

    // A second gate where the VPN is already down offers Continue: resume only.
    Rig second;
    second.service.setVpn(ConnectionState::connected, ConnectionPhase::running);
    PHPVPN_CHECK(second.openAt(WizardStep::disconnectGate));
    second.service.setVpn(ConnectionState::disconnected, ConnectionPhase::idle);
    PHPVPN_CHECK(second.pollUntil([&] { return second.wizard.presentation().primaryLabel == "Continue"; }));
    PHPVPN_CHECK(second.wizard.presentation().offline);
    second.wizard.next();
    PHPVPN_CHECK(second.ui.pumpUntil([&] { return second.wizard.model().step() == WizardStep::serverUrl; }));
    PHPVPN_CHECK_EQ(second.service.count("disconnect"), 0u);
}

PHPVPN_TEST(tray_linux_wizard, reset_asks_first_and_close_sends_wizard_close) {
    Rig rig;
    PHPVPN_CHECK(rig.openAt(WizardStep::serverUrl));
    rig.wizard.editUrl("http://192.0.2.1/phpvpn.php");
    rig.host.answer = false;
    rig.wizard.cancel(true);
    PHPVPN_CHECK_EQ(rig.host.questions.size(), 1u);
    PHPVPN_CHECK_EQ(rig.host.questions[0],
                    std::string("Reset this entire wizard to the default settings? Your file will change only after Finish."));
    rig.ui.drainFor(100);
    PHPVPN_CHECK_EQ(rig.service.count("wizard-reset"), 0u);
    rig.host.answer = true;
    rig.wizard.cancel(true);
    PHPVPN_CHECK_EQ(rig.wizard.presentation().statusText, std::string("Stopping the current check and resetting…"));
    PHPVPN_CHECK(rig.ui.pumpUntil([&] { return rig.service.count("wizard-reset") == 1 && !rig.wizard.presentation().locked; }));
    PHPVPN_CHECK_EQ(rig.wizard.presentation().url, std::string("http://MODIFY-THIS-URL/phpvpn.php"));
    PHPVPN_CHECK(rig.wizard.wizardSnapshot().dirty);  // a reset draft is not the file

    // X / Escape: wizard-close, then the window goes away.
    rig.wizard.cancel(false);
    PHPVPN_CHECK_EQ(rig.wizard.presentation().statusText, std::string("Closing this session…"));
    PHPVPN_CHECK(rig.ui.pumpUntil([&] { return !rig.wizard.isOpen(); }));
    PHPVPN_CHECK_EQ(rig.service.count("wizard-close"), 1u);
    PHPVPN_CHECK_EQ(rig.host.destroyed, 1);
}

PHPVPN_TEST(tray_linux_wizard, cancel_during_a_check_cancels_the_attempt_first) {
    Rig rig;
    PHPVPN_CHECK(rig.openAt(WizardStep::serverUrl));
    rig.service.setCheck(FakeService::Check::pending);
    rig.wizard.editUrl("http://192.0.2.1/phpvpn.php");
    rig.wizard.next();
    PHPVPN_CHECK(rig.ui.pumpUntil([&] { return rig.service.count("wizard-check-url") == 1; }));
    rig.ui.drainFor(100);
    rig.wizard.cancel(false);
    PHPVPN_CHECK(rig.ui.pumpUntil([&] { return !rig.wizard.isOpen(); }));
    const auto log = rig.service.log();
    const auto cancel = std::find(log.begin(), log.end(), "wizard-cancel-attempt");
    const auto close = std::find(log.begin(), log.end(), "wizard-close");
    PHPVPN_CHECK(cancel != log.end() && close != log.end() && cancel < close);
}

PHPVPN_TEST(tray_linux_wizard, commit_deadline_gives_unknown_and_verify_save_reads_the_receipt) {
    Rig rig;
    PHPVPN_CHECK(rig.openAt(WizardStep::serverUrl));
    rig.wizard.editUrl("http://192.0.2.1/phpvpn.php");
    for (auto step : {WizardStep::serverSecret, WizardStep::identity, WizardStep::contracts}) {
        rig.wizard.next();
        PHPVPN_CHECK(rig.pollUntil([&] { return rig.wizard.model().step() == step; }));
    }
    rig.service.setFinishPending(true);
    rig.wizard.next();
    // While committing, Cancel only explains and the Exit coordinator waits.
    rig.ui.drainFor(100);
    rig.wizard.cancel(false);
    PHPVPN_CHECK_EQ(rig.host.notices.size(), 1u);
    PHPVPN_CHECK_EQ(rig.host.notices.back().second,
                    std::string("The service is saving the configuration. Wait for its receipt before closing or resetting."));
    ExitCoordinator exit;
    exit.begin();
    auto participants = [&] { return std::vector<LifecycleParticipant*>{&rig.wizard}; };
    PHPVPN_CHECK(exit.advance(participants, [](std::string_view) { return true; }) == ExitCoordinator::Step::wait);
    exit.cancel();
    // The original 15 s deadline passes without a terminal result.
    for (int i = 0; i < 31 && rig.wizard.model().step() != WizardStep::commitUnknown; ++i) rig.poll();
    PHPVPN_CHECK(rig.wizard.model().step() == WizardStep::commitUnknown);
    auto p = rig.wizard.presentation();
    PHPVPN_CHECK_EQ(p.primaryLabel, std::string("Verify save"));
    PHPVPN_CHECK(!p.resetEnabled);
    PHPVPN_CHECK(rig.wizard.lifecycleSnapshot().unknown && !rig.wizard.lifecycleSnapshot().busy);
    PHPVPN_CHECK(rig.wizard.wizardSnapshot().commitDeadlineMs.has_value());
    // No keepalive and no second Finish in commitUnknown.
    const auto keepalives = rig.service.count("wizard-keepalive");
    rig.now += 20000; rig.wizard.tick(); rig.ui.drainFor(100);
    PHPVPN_CHECK_EQ(rig.service.count("wizard-keepalive"), keepalives);
    rig.wizard.next();
    PHPVPN_CHECK(rig.ui.pumpUntil([&] { return !rig.wizard.isOpen(); }));
    const auto finish = rig.service.last(WizardCommand::finish);
    const auto commit = rig.service.last(WizardCommand::get_commit);
    PHPVPN_CHECK(finish && commit && commit->commitId == finish->commitId);
    PHPVPN_CHECK_EQ(rig.service.count("wizard-finish"), 1u);
    PHPVPN_CHECK_EQ(rig.service.count("wizard-close"), 1u);
    PHPVPN_CHECK_EQ(rig.host.notices.back().first, std::string("Configuration saved"));
}

PHPVPN_TEST(tray_linux_wizard, approved_discard_and_session_end_send_nothing) {
    Rig rig;
    PHPVPN_CHECK(rig.openAt(WizardStep::serverUrl));
    rig.wizard.editUrl("http://192.0.2.1/phpvpn.php");
    const auto approved = rig.wizard.lifecycleSnapshot();
    PHPVPN_CHECK(approved.open && approved.dirty);
    // A later edit revokes the approval.
    rig.wizard.editUrl("http://192.0.2.2/phpvpn.php");
    PHPVPN_CHECK(!rig.wizard.approvedDiscard(approved));
    PHPVPN_CHECK(rig.wizard.isOpen());
    ExitCoordinator exit;
    exit.begin();
    std::vector<std::string_view> questions;
    auto participants = [&] { return std::vector<LifecycleParticipant*>{&rig.wizard}; };
    const auto step = exit.advance(participants, [&](std::string_view question) { questions.push_back(question); return true; });
    PHPVPN_CHECK(step == ExitCoordinator::Step::finished);
    PHPVPN_CHECK_EQ(questions.size(), 1u);
    PHPVPN_CHECK(questions[0] == kDiscardQuestion);
    PHPVPN_CHECK(!rig.wizard.isOpen());
    rig.ui.drainFor(200);
    PHPVPN_CHECK_EQ(rig.service.count("wizard-close"), 0u);  // the service lease cleans up

    Rig ending;
    PHPVPN_CHECK(ending.openAt(WizardStep::serverUrl));
    ending.wizard.endSession();
    ending.wizard.endSession();
    PHPVPN_CHECK(!ending.wizard.isOpen());
    PHPVPN_CHECK_EQ(ending.host.destroyed, 1);
    PHPVPN_CHECK(!ending.wizard.open());  // terminal
    ending.now += 60000;
    ending.wizard.tick();
    ending.ui.drainFor(200);
    PHPVPN_CHECK_EQ(ending.service.count("wizard-close"), 0u);
    PHPVPN_CHECK_EQ(ending.service.count("wizard-keepalive"), 0u);
}

PHPVPN_TEST(tray_linux_wizard, exit_close_is_bounded_by_one_second) {
    Rig rig;
    PHPVPN_CHECK(rig.openAt(WizardStep::serverUrl));
    rig.service.setCloseDelayMs(3000);
    const auto started = std::chrono::steady_clock::now();
    rig.wizard.closeOnExit();
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started);
    PHPVPN_CHECK(elapsed.count() >= 900 && elapsed.count() < 2000);
    PHPVPN_CHECK_EQ(rig.service.count("wizard-close"), 1u);
    PHPVPN_CHECK(!rig.wizard.isOpen());
    PHPVPN_CHECK_EQ(rig.host.destroyed, 1);

    // Without a session nothing is sent.
    Rig idle;
    PHPVPN_CHECK(idle.started);
    idle.wizard.closeOnExit();
    PHPVPN_CHECK_EQ(idle.service.count("wizard-close"), 0u);
}

PHPVPN_TEST(tray_linux_wizard, http_headers_accept_validates_and_invalidates_checks) {
    Rig rig;
    PHPVPN_CHECK(rig.openAt(WizardStep::serverUrl));
    rig.wizard.editUrl("http://192.0.2.1/phpvpn.php");
    rig.wizard.next();
    PHPVPN_CHECK(rig.pollUntil([&] { return rig.wizard.model().step() == WizardStep::serverSecret; }));
    rig.wizard.back();
    PHPVPN_CHECK(rig.wizard.model().step() == WizardStep::serverUrl);
    PHPVPN_CHECK_EQ(rig.wizard.presentation().sidebar[0].status, std::string("Completed"));
    const auto generation = rig.wizard.wizardSnapshot().draftGeneration;
    // Invalid headers are refused and change nothing.
    const auto refused = rig.wizard.acceptHttpHeaders("Host: example.com");
    PHPVPN_CHECK(!refused);
    PHPVPN_CHECK_EQ(httpHeadersErrorText(refused), std::string("masterSetup.httpHeaders line 1: Host is managed by the application or transport"));
    PHPVPN_CHECK_EQ(rig.wizard.wizardSnapshot().draftGeneration, generation);
    // Identical headers (only line ends differ) are not a change.
    PHPVPN_CHECK(rig.wizard.acceptHttpHeaders(defaultHttpHeadersDisplayText()).ok());
    PHPVPN_CHECK_EQ(rig.wizard.wizardSnapshot().draftGeneration, generation);
    PHPVPN_CHECK(rig.wizard.acceptHttpHeaders("Cookie: a=b\nUser-Agent: test").ok());
    PHPVPN_CHECK_EQ(rig.wizard.model().fields().httpHeaders, std::string("Cookie: a=b\r\nUser-Agent: test"));
    PHPVPN_CHECK(rig.wizard.wizardSnapshot().draftGeneration > generation);
    PHPVPN_CHECK(rig.wizard.presentation().sidebar[0].status.empty());  // active and pending again
    rig.wizard.next();
    PHPVPN_CHECK(rig.pollUntil([&] { return rig.wizard.model().step() == WizardStep::serverSecret; }));
    PHPVPN_CHECK_EQ(rig.service.last(WizardCommand::check_url)->fields.httpHeaders, std::string("Cookie: a=b\r\nUser-Agent: test"));
}
