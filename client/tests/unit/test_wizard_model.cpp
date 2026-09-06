#include "harness.h"
#include "core/wizard/wizard_model.h"
#include "core/wizard/wizard_defaults.h"
#include "core/config/config_defaults.h"

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
    model.load(view);
    PHPVPN_CHECK(!model.useDefaultAccessKey());
}
PHPVPN_TEST(WizardModel, ExactSecretAndLiteralRoundTrip) {
    for (const auto& secret : {std::string{}, std::string(32, '0'), std::string(64, '0'), std::string(" a "), std::string("a\0b\n", 4)}) {
        WizardModel model;
        auto view = modelView(); view.fields->serverSecret = secret;
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
    PHPVPN_CHECK(!model.contractsAllowed());
    PHPVPN_CHECK_EQ(model.contractCount(), 7u);
    view.mode = WizardMode::reset;
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
