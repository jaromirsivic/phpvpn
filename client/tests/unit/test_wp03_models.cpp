#include "../../apps/tray/progress_details.h"
#include "../../apps/tray/progress_layout.h"
#include "../../apps/tray/progress_model.h"
#include "../../apps/tray/statistics_layout.h"
#include "../../apps/tray/statistics_model.h"
#include "contracts_fixture.h"
#include "harness.h"
#include <limits>

namespace {
using namespace phpvpn;
using namespace phpvpn::tray;
TrayStatus ready() {
    TrayStatus s;
    s.valid = true;
    s.state = "connected";
    s.phase = "running";
    s.tunnelReady = "yes";
    s.serverUrl = s.serverSecret = s.accessKey = phpvpn::tray::CheckpointResult::success;
    return s;
}
} // namespace
PHPVPN_TEST(WP03Models, DeadlineStartsAtPresentationAndUsesCeilingWithoutRestart) {
    ProgressModel model;
    model.onSnapshot(ready(), 100);
    model.onTick(90000);
    PHPVPN_CHECK(!model.successDeadlineMs());
    PHPVPN_CHECK(!model.view().closeRequested);
    model.onSuccessPresented(90000);
    PHPVPN_CHECK_EQ(*model.successDeadlineMs(), 93000);
    for (const auto [time, seconds] :
         {std::pair{90000, 3u}, {90999, 3u}, {91000, 2u}, {91999, 2u}, {92000, 1u}, {92999, 1u}}) {
        model.onTick(time);
        PHPVPN_CHECK_EQ(model.view().closingSeconds, seconds);
        model.onSnapshot(ready(), time);
        model.onSuccessPresented(time);
        PHPVPN_CHECK_EQ(*model.successDeadlineMs(), 93000);
        PHPVPN_CHECK(!model.view().closeRequested);
    }
    model.onTick(93000);
    PHPVPN_CHECK(model.view().closeRequested);
    model = ProgressModel{};
    model.onSnapshot(ready(), 0);
    model.onSuccessPresented(100);
    model.onTick(10000000); // Resume after deadline must not restart the countdown.
    PHPVPN_CHECK(model.view().closeRequested);
}
PHPVPN_TEST(WP03Models, ConnectedRequiresRunningAndTunnelAndKeepsAuthoritativeTriple) {
    for (int missing = 0; missing < 3; ++missing) {
        auto s = ready();
        if (missing == 0)
            s.state = "connecting";
        if (missing == 1)
            s.phase = "tunnel";
        if (missing == 2)
            s.tunnelReady = "unknown";
        ProgressModel model;
        model.onSnapshot(s, 0);
        model.onSuccessPresented(1);
        PHPVPN_CHECK_EQ(model.view().stage, ProgressStage::connecting);
        PHPVPN_CHECK(!model.successDeadlineMs());
    }
    auto s = ready();
    s.accessKey = phpvpn::tray::CheckpointResult::pending;
    ProgressModel model;
    model.onSnapshot(s, 0);
    PHPVPN_CHECK_EQ(model.view().accessKey, phpvpn::tray::CheckpointResult::pending);
}
PHPVPN_TEST(WP03Models, SafeCatalogDiscardsRawDescriptionsAndUnknownCodes) {
    const char *codes[]{"endpoint_unavailable_or_invalid",
                        "server_verification_failed",
                        "access_key_rejected",
                        "tunnel_start_failed",
                        "communication_failed",
                        "communication_timeout",
                        "status_unavailable"};
    for (const auto code : codes) {
        const auto &text = progressErrorText(code);
        PHPVPN_CHECK_EQ(text.code, code);
        PHPVPN_CHECK(!text.description.empty() && !text.panel.empty() && !text.action.empty());
        auto s = ready();
        s.hasError = true;
        s.errorCode = code;
        s.errorDescription = "DO NOT DISPLAY raw secret/path/URL";
        ProgressModel model;
        model.onSnapshot(s, 0);
        PHPVPN_CHECK_EQ(model.view().errorDescription, text.description);
        PHPVPN_CHECK_EQ(model.view().errorCode, text.code);
        model.onTick(10000000);
        PHPVPN_CHECK(!model.view().closeRequested);
    }
    PHPVPN_CHECK_EQ(progressErrorText("DO NOT DISPLAY").code, "communication_failed");
    ProgressModel transient;
    transient.onSnapshot({}, 0);
    PHPVPN_CHECK(transient.view().statusUnavailable);
    PHPVPN_CHECK_EQ(transient.view().serverUrl, phpvpn::tray::CheckpointResult::pending);
    transient.onSnapshot(ready(), 1);
    PHPVPN_CHECK_EQ(transient.view().stage, ProgressStage::succeeded);
}
PHPVPN_TEST(WP03Models, ConfigRetainsOnlyValidatedSavedDetailsIndependentOfActiveStatus) {
    auto document = contracts_test::document("10.99.99.3");
    const auto details = consumeProgressConfig(document);
    PHPVPN_CHECK(details.available);
    PHPVPN_CHECK_EQ(details.virtualIp, "10.99.99.3");
    PHPVPN_CHECK(!details.serverUrl.empty());
    ProgressModel model;
    model.onSnapshot(ready(), 0);
    PHPVPN_CHECK_EQ(model.view().stage, ProgressStage::succeeded);
    PHPVPN_CHECK(!consumeProgressConfig("{}").available);
    PHPVPN_CHECK(!consumeProgressConfig(std::string(524289, 'x')).available);
    PHPVPN_CHECK(!consumeProgressConfig(std::string("\xff", 1)).available);
    document[0] = '!';
    PHPVPN_CHECK(!consumeProgressConfig(std::move(document)).available);
    PHPVPN_CHECK_EQ(model.view().stage, ProgressStage::succeeded);
}
PHPVPN_TEST(WP03Models, StatisticsPreservesExactNineUint64AndStaleSnapshot) {
    StatisticsModel model;
    PHPVPN_CHECK_EQ(std::wstring(model.freshness()), L"Waiting for service status");
    for (std::size_t i = 0; i < 9; ++i)
        PHPVPN_CHECK_EQ(model.value(i), L"-");
    auto s = ready();
    for (std::size_t i = 0; i < 9; ++i)
        s.counters.*kCounterFields[i].member = 1000 + i;
    model.update(s, false);
    for (std::size_t i = 0; i < 9; ++i)
        PHPVPN_CHECK_EQ(model.value(i), ui::englishInteger(1000 + statisticsFields[i].counterIndex) +
                                            (statisticsFields[i].bytes ? L" B" : L""));
    model.update({}, true);
    PHPVPN_CHECK_EQ(model.value(0), L"1,000");
    PHPVPN_CHECK_EQ(std::wstring(model.freshness()), L"Service status unavailable. Showing the last received values.");
    for (auto field : kCounterFields)
        s.counters.*field.member = (std::numeric_limits<std::uint64_t>::max)();
    model.update(s, false);
    for (std::size_t i = 0; i < 9; ++i)
        PHPVPN_CHECK_EQ(model.value(i),
                        std::wstring(L"18,446,744,073,709,551,615") + (statisticsFields[i].bytes ? L" B" : L""));
    PHPVPN_CHECK_EQ(std::wstring(model.freshness()), L"");
}
PHPVPN_TEST(WP03Models, NarrowLayoutsAndMeasuredLongCountersDoNotOverlap) {
    for (int dpi : {96, 120, 144, 192}) {
        const auto p = [dpi](int n) { return scaleForDpi(n, dpi); };
        for (int width : {480, 940}) {
            auto l = computeProgressLayout(p(width), dpi, p(84), p(48), p(48), true, p(56));
            PHPVPN_CHECK(l.heading.bottom <= l.description.top);
            PHPVPN_CHECK(l.description.bottom < l.card.top);
            PHPVPN_CHECK(l.card.bottom < l.panel.top);
            PHPVPN_CHECK(l.panelHeading.bottom <= l.panelAction.top);
            PHPVPN_CHECK(l.countdown.bottom < l.height);
        }
        StatisticsMetrics metrics;
        metrics.descriptionHeight = p(48);
        metrics.valueWidths.fill(p(340));
        metrics.keyWidths.fill(p(270));
        metrics.labelWidths.fill(p(140));
        for (int width : {480, 1000}) {
            const auto l = computeStatisticsLayout(p(width), dpi, metrics);
            for (const auto &r : l.rows) {
                PHPVPN_CHECK(r.key.bottom <= r.value.top);
                PHPVPN_CHECK(r.value.right <= p(width));
                PHPVPN_CHECK(r.value.left < r.value.right);
            }
            PHPVPN_CHECK(l.cards[0].bottom < l.cards[2].top);
            PHPVPN_CHECK(l.cards[1].bottom < l.cards[2].top);
        }
    }
}
