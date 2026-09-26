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
    for (const auto& [time, seconds] :
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
PHPVPN_TEST(WP03Models, StatisticsFormatsTenValuesAndPreservesStaleSnapshot) {
    StatisticsModel model;
    PHPVPN_CHECK_EQ(std::wstring(model.freshness()), L"Waiting for service status");
    PHPVPN_CHECK_EQ(std::wstring(model.connectionStatus()), L"(Disconnected)");
    for (std::size_t i = 0; i < statisticsFields.size(); ++i)
        PHPVPN_CHECK_EQ(model.value(i), L"-");
    auto s = ready();
    for (std::size_t i = 0; i < 9; ++i)
        s.counters.*kCounterFields[i].member = 1000 + i;
    model.update(s, false);
    const std::array<std::wstring, 10> expected{{L"1,001", L"1,002", L"1,000", L"1.00 kB", L"1.00 kB",
                                                 L"2.01 kB", L"1,005", L"1.01 kB", L"1,006", L"1.01 kB"}};
    for (std::size_t i = 0; i < statisticsFields.size(); ++i)
        PHPVPN_CHECK_EQ(model.value(i), expected[i]);
    PHPVPN_CHECK_EQ(std::wstring(model.connectionStatus()), L"(Connected)");
    model.update({}, true);
    PHPVPN_CHECK_EQ(model.value(0), L"1,001");
    PHPVPN_CHECK_EQ(std::wstring(model.connectionStatus()), L"(Connected)");
    PHPVPN_CHECK_EQ(std::wstring(model.freshness()), L"Service status unavailable. Showing the last received values.");
    for (auto field : kCounterFields)
        s.counters.*field.member = (std::numeric_limits<std::uint64_t>::max)();
    model.update(s, false);
    for (std::size_t i = 0; i < statisticsFields.size(); ++i) {
        const auto expectedMaximum = statisticsFields[i].addCounterIndex != kNoStatisticsCounter
                                         ? L"36893.49 PB"
                                     : statisticsFields[i].bytes ? L"18446.74 PB"
                                                                 : L"18,446,744,073,709,551,615";
        PHPVPN_CHECK_EQ(model.value(i), expectedMaximum);
    }
    PHPVPN_CHECK_EQ(std::wstring(model.freshness()), L"");

    PHPVPN_CHECK_EQ(formatStatisticsBytes(0), L"0 B");
    PHPVPN_CHECK_EQ(formatStatisticsBytes(999), L"999 B");
    PHPVPN_CHECK_EQ(formatStatisticsBytes(1000), L"1.00 kB");
    PHPVPN_CHECK_EQ(formatStatisticsBytes(1500), L"1.50 kB");
    PHPVPN_CHECK_EQ(formatStatisticsBytes(1000000), L"1.00 MB");
    PHPVPN_CHECK_EQ(formatStatisticsBytes(1234567), L"1.23 MB");
    PHPVPN_CHECK_EQ(formatStatisticsBytes(1000000000), L"1.00 GB");
    PHPVPN_CHECK_EQ(formatStatisticsBytes(999994), L"999.99 kB");
    PHPVPN_CHECK_EQ(formatStatisticsBytes(999995), L"1.00 MB");
}
PHPVPN_TEST(WP03Models, StatisticsRoundsHalfUpAcrossDecimalUnits) {
    // Catch truncation, binary divisors and failure to promote after rounding.
    const std::pair<std::uint64_t, const wchar_t *> cases[]{
        {1004ULL, L"1.00 kB"}, {1005ULL, L"1.01 kB"},
        {1004999ULL, L"1.00 MB"}, {1005000ULL, L"1.01 MB"},
        {999994999ULL, L"999.99 MB"}, {999995000ULL, L"1.00 GB"},
        {1004999999ULL, L"1.00 GB"}, {1005000000ULL, L"1.01 GB"},
        {999994999999ULL, L"999.99 GB"}, {999995000000ULL, L"1.00 TB"},
        {1000000000000ULL, L"1.00 TB"},
        {1004999999999ULL, L"1.00 TB"}, {1005000000000ULL, L"1.01 TB"},
        {999994999999999ULL, L"999.99 TB"}, {999995000000000ULL, L"1.00 PB"},
        {1000000000000000ULL, L"1.00 PB"},
        {1004999999999999ULL, L"1.00 PB"}, {1005000000000000ULL, L"1.01 PB"},
        {999995000000000000ULL, L"1000.00 PB"}};
    for (const auto &[bytes, expected] : cases)
        PHPVPN_CHECK_EQ(formatStatisticsBytes(bytes), expected);
}
PHPVPN_TEST(WP03Models, StatisticsSumsExactBytesBeforeRoundingIncludingCarry) {
    // A wrapped/saturated sum or float conversion loses these exact low bits.
    const auto maximum = (std::numeric_limits<std::uint64_t>::max)();
    struct Case { std::uint64_t first, second, upper, lower; };
    const Case cases[]{
        {0, 0, 0, 0}, {maximum, 0, 0, maximum},
        {9007199254740992ULL, 1, 0, 9007199254740993ULL},
        {9223372036854775807ULL, 1, 0, 9223372036854775808ULL},
        {maximum, 1, 1, 0}, {1, maximum, 1, 0},
        {maximum, maximum, 1, 18446744073709551614ULL}};
    for (const auto &c : cases) {
        const auto sum = addStatisticsBytes(c.first, c.second);
        PHPVPN_CHECK_EQ(sum.upper, c.upper);
        PHPVPN_CHECK_EQ(sum.lower, c.lower);
    }
    StatisticsModel model;
    auto s = ready();
    s.counters.serializedDataRequestBodyBytes = 1004;
    s.counters.serializedDataResponseBodyBytes = 1004;
    model.update(s, false);
    PHPVPN_CHECK_EQ(model.value(3), L"1.00 kB");
    PHPVPN_CHECK_EQ(model.value(4), L"1.00 kB");
    PHPVPN_CHECK_EQ(model.value(5), L"2.01 kB"); // Not a sum of rounded display values.
    model.update({}, true);
    PHPVPN_CHECK_EQ(model.value(5), L"2.01 kB");
}
PHPVPN_TEST(WP03Models, NarrowLayoutsAndMeasuredLongCountersDoNotOverlap) {
    for (int dpi : {96, 120, 144, 192}) {
        const auto p = [dpi](int n) { return scaleForDpi(n, dpi); };
        for (int width : {480, 940}) {
            auto l = computeProgressLayout(p(width), dpi, p(48), true, false, p(56));
            PHPVPN_CHECK(l.eyebrow.bottom < l.card.top);
            PHPVPN_CHECK(l.card.bottom < l.panel.top);
            PHPVPN_CHECK(l.panelHeading.bottom <= l.panelAction.top);
            PHPVPN_CHECK(l.countdown.bottom < l.height);
        }
        StatisticsMetrics metrics;
        metrics.headingWidth = p(120);
        metrics.statusWidth = p(150);
        metrics.descriptionHeight = p(48);
        metrics.valueWidths.fill(p(340));
        metrics.labelWidths.fill(p(140));
        for (int width : {480, 1000}) {
            const auto l = computeStatisticsLayout(p(width), dpi, metrics);
            for (const auto &r : l.rows) {
                PHPVPN_CHECK(r.label.bottom <= r.value.top);
                PHPVPN_CHECK(r.value.right <= p(width));
                PHPVPN_CHECK(r.value.left < r.value.right);
            }
            PHPVPN_CHECK(l.heading.right <= l.connectionStatus.left);
            PHPVPN_CHECK(l.cards[0].bottom < l.cards[2].top);
            PHPVPN_CHECK(l.cards[1].bottom < l.cards[2].top);
            if (width == 1000)
                PHPVPN_CHECK_EQ(l.cards[0].bottom, l.cards[1].bottom);
        }
    }
}
PHPVPN_TEST(WP03Models, StatisticsIpv4WideGridPreservesBindingsAndNarrowOrder) {
    PHPVPN_CHECK_EQ(std::wstring(statisticsFields[6].label), L"Packets sent");
    PHPVPN_CHECK_EQ(std::wstring(statisticsFields[7].label), L"Bytes sent");
    PHPVPN_CHECK_EQ(std::wstring(statisticsFields[8].label), L"Packets received");
    PHPVPN_CHECK_EQ(std::wstring(statisticsFields[9].label), L"Bytes received");
    PHPVPN_CHECK_EQ(statisticsFields[6].counterIndex, 5u);
    PHPVPN_CHECK_EQ(statisticsFields[7].counterIndex, 7u);
    PHPVPN_CHECK_EQ(statisticsFields[8].counterIndex, 6u);
    PHPVPN_CHECK_EQ(statisticsFields[9].counterIndex, 8u);

    for (const int dpi : {96, 120, 144, 192}) {
        const auto p = [dpi](int n) { return scaleForDpi(n, dpi); };
        StatisticsMetrics metrics;
        metrics.headingWidth = p(120);
        metrics.statusWidth = p(150);
        metrics.descriptionHeight = p(24);
        metrics.valueWidths.fill(p(80));
        metrics.labelWidths.fill(p(120));
        metrics.valueWidths[6] = p(340);
        metrics.labelWidths[7] = p(180);

        const auto wide = computeStatisticsLayout(p(1000), dpi, metrics);
        const auto cellTop = [&](std::size_t i) { return std::min(wide.rows[i].label.top, wide.rows[i].value.top); };
        const auto cellBottom = [&](std::size_t i) { return std::max(wide.rows[i].label.bottom, wide.rows[i].value.bottom); };
        PHPVPN_CHECK_EQ(wide.rows[6].label.left, wide.rows[8].label.left);
        PHPVPN_CHECK_EQ(wide.rows[7].label.left, wide.rows[9].label.left);
        PHPVPN_CHECK(wide.rows[6].label.left < wide.rows[7].label.left);
        PHPVPN_CHECK_EQ(cellTop(6), cellTop(7));
        PHPVPN_CHECK_EQ(cellTop(8), cellTop(9));
        PHPVPN_CHECK(cellTop(8) > cellBottom(6));
        PHPVPN_CHECK(cellTop(9) > cellBottom(7));

        const auto narrow = computeStatisticsLayout(p(480), dpi, metrics);
        for (std::size_t i = 6; i < 9; ++i)
            PHPVPN_CHECK(narrow.rows[i + 1].label.top >
                         std::max(narrow.rows[i].label.bottom, narrow.rows[i].value.bottom));
    }
}
