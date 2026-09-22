#include "harness.h"
#include "core/wizard/wizard_model.h"
#include "core/wizard/wizard_defaults.h"
#include "core/config/config_defaults.h"
#include "core/ipc/wizard_ipc.h"

namespace phpvpn {
namespace {
constexpr char id[] = "12345678-1234-4234-8234-123456789abc";
constexpr char opid[] = "12345678-1234-4234-8234-123456789abd";
WizardView modelView() {
    auto draft = makeNewWizardDocument();
    WizardView view;
    view.sessionId = id; view.serviceInstanceId = opid;
    view.fields = draft.fields; view.context = draft.context;
    view.fields->serverUrl = "http://localhost/phpvpn.php";
    return view;
}
WizardOperationResult success(WizardModel& model, std::int64_t now) {
    auto start = model.beginCheck(id, now);
    PHPVPN_CHECK(start.ok());
    if (!start) return {};
    PHPVPN_CHECK(model.acceptStart(opid, id, start->sequence));
    WizardOperationResult result;
    result.operationId = opid; result.kind = start->kind;
    result.state = WizardOperationState::succeeded;
    result.clientAttemptId = id; result.attemptSequence = start->sequence;
    result.connectionEpoch = model.view().connectionEpoch;
    return result;
}
}
PHPVPN_TEST(WizardModel, DefaultKeyTransitionsAndExistingValue) {
    WizardModel model;
    auto view = modelView();
    model.load(view);
    PHPVPN_CHECK(model.useDefaultAccessKey());
    PHPVPN_CHECK(model.setAutoDetectIpAddress(false));
    PHPVPN_CHECK(model.setIp("10.99.99.10"));
    PHPVPN_CHECK_EQ(model.fields().accessKey, defaultAccessKey("10.99.99.10"));
    PHPVPN_CHECK(!model.setAccessKey(std::string(64, 'a')));
    PHPVPN_CHECK(model.setDefaultAccessKey(false));
    PHPVPN_CHECK(model.setAccessKey(std::string(64, 'a')));
    PHPVPN_CHECK(model.setIp("10.99.99.11"));
    PHPVPN_CHECK_EQ(model.fields().accessKey, std::string(64, 'a'));
    PHPVPN_CHECK(model.setDefaultAccessKey(true));
    PHPVPN_CHECK_EQ(model.fields().accessKey, defaultAccessKey("10.99.99.11"));
    PHPVPN_CHECK(model.setDefaultAccessKey(false));
    PHPVPN_CHECK_EQ(model.fields().accessKey, defaultAccessKey("10.99.99.11"));
    PHPVPN_CHECK(model.setDefaultAccessKey(true));
    PHPVPN_CHECK(model.setIp("invalid"));
    PHPVPN_CHECK(model.fields().accessKey.empty());
    view.mode = WizardMode::edit;
    view.fields->virtualIp = "10.99.99.2";
    view.fields->accessKey = defaultAccessKey(view.fields->virtualIp);
    view.fields->useDefaultAccessKeyInWizard = false;
    model.load(view);
    PHPVPN_CHECK(!model.useDefaultAccessKey());
}
PHPVPN_TEST(WizardModel, HttpHeadersCanonicalDraftInvalidatesChecksAndIsolatesSessions) {
    WizardModel model;
    auto view = modelView();
    model.load(view);
    PHPVPN_CHECK_EQ(model.fields().httpHeaders, kDefaultHttpHeaders);
    auto result = success(model, 1);
    PHPVPN_CHECK(model.applyOperation(result, 2));
    PHPVPN_CHECK_EQ(model.checks()[0], CheckpointResult::success);
    PHPVPN_REQUIRE_OK(model.setHttpHeaders(kDefaultHttpHeaders));
    PHPVPN_CHECK_EQ(model.checks()[0], CheckpointResult::success);
    PHPVPN_REQUIRE_OK(model.setHttpHeaders("Cookie: synthetic=1\nUser-Agent: test\n"));
    PHPVPN_CHECK_EQ(model.fields().httpHeaders, "Cookie: synthetic=1\r\nUser-Agent: test");
    PHPVPN_CHECK_EQ(model.checks()[0], CheckpointResult::pending);
    PHPVPN_CHECK_EQ(model.step(), WizardStep::serverUrl);
    PHPVPN_CHECK(!model.hasSuggestedIp());
    PHPVPN_CHECK_EQ(model.view().context->httpHeaders, kDefaultHttpHeaders);
    for (const auto bad : {"", "Host: secret", "X: a\n\nY: b", "Cookie: a\ncOoKiE: b"}) {
        PHPVPN_CHECK(!model.setHttpHeaders(bad));
        PHPVPN_CHECK_EQ(model.fields().httpHeaders, "Cookie: synthetic=1\r\nUser-Agent: test");
    }
    model.load(view, true);
    PHPVPN_CHECK_EQ(model.fields().httpHeaders, "Cookie: synthetic=1\r\nUser-Agent: test");
    model.setCommitting(true);
    PHPVPN_REQUIRE_ERROR(model.setHttpHeaders(kDefaultHttpHeaders), ErrorCode::busy);
    model.commitUnknown();
    PHPVPN_REQUIRE_ERROR(model.setHttpHeaders(kDefaultHttpHeaders), ErrorCode::busy);
    view.sessionId = opid;
    model.load(view, true);
    PHPVPN_CHECK_EQ(model.fields().httpHeaders, kDefaultHttpHeaders);
}
namespace {
WizardOperationResult failedDiagnostic(WizardModel& model) {
    auto result = success(model, 0);
    result.state = WizardOperationState::failed;
    result.error = WizardIpc::safeError(Error(ErrorCode::response_invalid), 200,
        result.kind == WizardOperationKind::url ? "serverUrl" : "serverSecret");
    HttpDiagnostic d;
    d.action = HttpAction::health; d.responseComplete = true;
    d.request = {4, "req!", {}}; d.response = {4, "bad!", {}};
    result.httpDiagnostic = d; return result;
}
}
PHPVPN_TEST(WizardModel, DiagnosticCorrelatesEveryIdentityAndClearsOnAllInvalidations) {
    for (int mismatch = 0; mismatch < 6; ++mismatch) {
        WizardModel model; model.load(modelView());
        auto result = failedDiagnostic(model);
        if (mismatch == 0) result.operationId = id;
        if (mismatch == 1) result.clientAttemptId = opid;
        if (mismatch == 2) result.attemptSequence = 2;
        if (mismatch == 3) result.kind = WizardOperationKind::secret;
        if (mismatch == 4) result.connectionEpoch = 1;
        model.applyOperation(result, mismatch == 5 ? 1000000 : 1);
        PHPVPN_CHECK(!model.httpDiagnostic());
    }
    for (int invalidation = 0; invalidation < 11; ++invalidation) {
        WizardModel model; model.load(modelView());
        auto result = failedDiagnostic(model); PHPVPN_CHECK(model.applyOperation(result, 1));
        PHPVPN_CHECK(model.httpDiagnostic());
        PHPVPN_CHECK(!model.applyOperation(result, 2));
        if (invalidation == 0) { const auto next = model.beginCheck(id, 2); PHPVPN_CHECK(next.ok()); }
        if (invalidation == 1) model.setUrl("bad-url");
        if (invalidation == 2) model.fail(WizardIpc::safeError(Error(ErrorCode::ipc_unavailable)));
        if (invalidation == 3) model.interrupt();
        if (invalidation == 4) model.sessionExpired();
        if (invalidation == 5) model.cancellationFinished();
        if (invalidation == 6) model.load(modelView());
        if (invalidation == 7) model.load(modelView(), true);
        if (invalidation == 8) model.setDefaultSecret(false);
        if (invalidation == 9) { model.setUrl("bad-url"); PHPVPN_CHECK(!model.beginCheck(id, 2)); }
        if (invalidation == 10) model.clearHttpDiagnostic();
        PHPVPN_CHECK(!model.httpDiagnostic());
        PHPVPN_CHECK(!model.applyOperation(result, 3));
    }
    for (bool back : {false, true}) {
        WizardModel model; model.load(modelView());
        const auto good = success(model, 0); PHPVPN_CHECK(model.applyOperation(good, 1));
        auto result = failedDiagnostic(model); PHPVPN_CHECK(model.applyOperation(result, 1));
        PHPVPN_CHECK(model.httpDiagnostic());
        if (back) model.back(); else { model.setDefaultSecret(false); model.setSecretEditor("changed"); }
        PHPVPN_CHECK(!model.httpDiagnostic());
    }
}
PHPVPN_TEST(WizardModel, ExactSecretAndLiteralRoundTrip) {
    for (const auto& secret : {std::string{}, std::string(32, '0'), std::string(64, '0'), std::string(" a "), std::string("a\0b\n", 4)}) {
        WizardModel model;
        auto view = modelView(); view.fields->serverSecret = secret;
        view.fields->useDefaultServerSecretInWizard = false;
        model.load(view);
        PHPVPN_CHECK(!model.useDefaultSecret());
        PHPVPN_CHECK_EQ(model.fields().serverSecret, secret);
        PHPVPN_CHECK(model.setSecretEditor(model.secretEditorText()));
        PHPVPN_CHECK_EQ(model.fields().serverSecret, secret);
        if (model.secretLiteralMode()) {
            PHPVPN_CHECK(model.setSecretEditor("\"incomplete"));
            auto result = success(model, 0);
            PHPVPN_CHECK(model.applyOperation(result, 1));
            PHPVPN_CHECK(!model.validateCurrent());
        }
        PHPVPN_CHECK(model.setDefaultSecret(true));
        PHPVPN_CHECK_EQ(model.fields().serverSecret, std::string(kDefaultServerSecret));
        PHPVPN_CHECK(model.setDefaultSecret(false));
        PHPVPN_CHECK_EQ(model.fields().serverSecret, std::string(kDefaultServerSecret));
    }
}
PHPVPN_TEST(WizardModel, CorrelationDeadlineEpochAndNoAutomaticRetry) {
    WizardModel model; auto view = modelView(); model.load(view);
    auto result = success(model, 100);
    result.attemptSequence = 99;
    PHPVPN_CHECK(!model.applyOperation(result, 101));
    result.attemptSequence = model.attempt()->sequence;
    const auto deadline = model.attempt()->deadlineMs;
    PHPVPN_CHECK(!model.applyOperation(result, deadline));
    PHPVPN_CHECK(!model.busy());
    PHPVPN_CHECK_EQ(model.step(), WizardStep::serverUrl);
    PHPVPN_CHECK(!model.applyOperation(result, deadline + 1));
    result = success(model, deadline + 2);
    PHPVPN_CHECK_EQ(*result.attemptSequence, 2u);
    result.connectionEpoch = 1;
    PHPVPN_CHECK(model.applyOperation(result, deadline + 3));
    PHPVPN_CHECK_EQ(model.step(), WizardStep::disconnectGate);
    PHPVPN_CHECK(!model.back());
    model.load(view, true);
    result = success(model, deadline + 4);
    PHPVPN_CHECK_EQ(*result.attemptSequence, 3u);
    PHPVPN_CHECK(model.applyOperation(result, deadline + 5));
    PHPVPN_CHECK_EQ(model.step(), WizardStep::serverSecret);
    result = success(model, deadline + 6);
    result.suggestedIp = "10.99.99.10";
    PHPVPN_CHECK(model.applyOperation(result, deadline + 7));
    PHPVPN_CHECK_EQ(model.fields().accessKey, defaultAccessKey("10.99.99.10"));
    result = success(model, deadline + 8);
    PHPVPN_CHECK(model.applyOperation(result, deadline + 9));
    PHPVPN_CHECK_EQ(model.step(), WizardStep::contracts);
    PHPVPN_CHECK_EQ(model.contractCount(), 13u);
    PHPVPN_CHECK(model.back()); PHPVPN_CHECK(model.back()); PHPVPN_CHECK(model.back());
    PHPVPN_CHECK(!model.back());
    PHPVPN_CHECK_EQ(model.step(), WizardStep::serverUrl);
}
PHPVPN_TEST(WizardModel, ContractsAndResumePreserveDraft) {
    WizardModel model; auto view = modelView(); model.load(view);
    model.setAutoDetectIpAddress(false);
    model.setIp("10.99.99.20");
    PHPVPN_CHECK_EQ(model.contractCount(), 14u);
    model.setCreateContracts(false);
    PHPVPN_CHECK_EQ(model.contractCount(), 0u);
    model.interrupt();
    view.connectionEpoch = 2;
    model.load(view, true);
    PHPVPN_CHECK_EQ(model.fields().virtualIp, std::string("10.99.99.20"));
    PHPVPN_CHECK(!model.createContracts());
    view.mode = WizardMode::repair; view.contractCount = 7;
    model.load(view);
    PHPVPN_CHECK(model.contractsAllowed());
    PHPVPN_CHECK_EQ(model.contractCount(), 7u);
    view.mode = WizardMode::reset;
    view.contractCount = 0;
    view.context->vpnCidr = Ipv4Cidr::parse("192.168.0.0/24").value();
    model.load(view);
    PHPVPN_CHECK(!model.contractsAllowed());
    model.setCreateContracts(true);
    PHPVPN_CHECK_EQ(model.contractCount(), 0u);
    model.setCommitting(true); model.interrupt();
    PHPVPN_CHECK(model.committing());
    model.commitUnknown();
    PHPVPN_CHECK_EQ(model.step(), WizardStep::commitUnknown);
    model.confirmed(); PHPVPN_CHECK_EQ(model.step(), WizardStep::finished);
}
} // namespace phpvpn
