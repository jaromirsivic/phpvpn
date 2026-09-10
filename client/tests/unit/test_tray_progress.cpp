#include "harness.h"
#include "../../apps/tray/progress_model.h"
#include "../../apps/tray/poll_schedule.h"
#include <vector>

namespace {
using namespace phpvpn::tray;
TrayStatus connecting() { TrayStatus status; status.valid = true; status.state = "connecting"; return status; }
}

PHPVPN_TEST(TrayProgress, ReconstructsSkippedPhasesButSucceedsOnlyWhenConnected) {
    ProgressModel model;
    PHPVPN_CHECK_EQ(model.view().shield, Shield::orange);
    auto status = connecting();
    status.serverUrl = status.serverSecret = CheckpointResult::success;
    model.onSnapshot(status, 500);
    PHPVPN_CHECK_EQ(model.view().serverSecret, CheckpointResult::success);
    PHPVPN_CHECK_EQ(model.view().accessKey, CheckpointResult::pending);
    status.accessKey = CheckpointResult::success;
    status.state = "recovering";
    model.onSnapshot(status, 1000);
    PHPVPN_CHECK_EQ(model.view().stage, ProgressStage::connecting);
    status.state = "connected";
    status.phase = "running";
    status.tunnelReady = "yes";
    model.onSnapshot(status, 1500);
    PHPVPN_CHECK_EQ(model.view().stage, ProgressStage::succeeded);
    PHPVPN_CHECK_EQ(model.view().shield, Shield::green);
    model.onTick(9000);
    PHPVPN_CHECK(!model.view().closeRequested);
    model.onSuccessPresented(1500);
    model.onTick(4499);
    PHPVPN_CHECK(!model.view().closeRequested);
    model.onTick(4500);
    PHPVPN_CHECK(model.view().closeRequested);
}

PHPVPN_TEST(TrayProgress, ReplacesWholeCheckpointTripleIncludingNewPending) {
    ProgressModel model;
    auto status = connecting();
    status.serverUrl = status.serverSecret = CheckpointResult::success;
    model.onSnapshot(status, 0);
    model.onSnapshot(TrayStatus{}, 50);
    PHPVPN_CHECK_EQ(model.view().serverSecret, CheckpointResult::success);
    status.serverUrl = status.serverSecret = CheckpointResult::pending;
    model.onSnapshot(status, 100);
    PHPVPN_CHECK_EQ(model.view().serverUrl, CheckpointResult::pending);
    PHPVPN_CHECK_EQ(model.view().serverSecret, CheckpointResult::pending);
    status.state = "recovering";
    status.accessKey = CheckpointResult::failed;
    model.onSnapshot(status, 200);
    PHPVPN_CHECK_EQ(model.view().stage, ProgressStage::connecting);
}

PHPVPN_TEST(TrayProgress, AllSafeFailureCategoriesFreezeUntilManualClose) {
    struct Failure { const char* code; const char* text; int failedIndex; };
    const Failure failures[]{
        {"endpoint_unavailable_or_invalid", "The server endpoint is not reachable or not valid.", 0},
        {"server_verification_failed", "The server could not be verified with the configured SERVER_SECRET.", 1},
        {"access_key_rejected", "The server rejected the configured ACCESS_KEY.", 2},
        {"tunnel_start_failed", "The local tunnel could not be started.", 3},
        {"communication_failed", "Communication with the server could not be completed.", 2},
        {"communication_timeout", "The server did not respond in time. Verification could not be completed.", 0},
        {"communication_timeout", "The server did not respond in time. Verification could not be completed.", 1},
        {"communication_timeout", "The server did not respond in time. Verification could not be completed.", 2}
    };
    for (const auto& failure : failures) {
        ProgressModel model;
        auto status = connecting();
        status.state = "recovering";
        status.hasError = true;
        status.errorCode = failure.code;
        status.errorDescription = failure.text;
        CheckpointResult* checks[]{&status.serverUrl, &status.serverSecret, &status.accessKey};
        for (int index = 0; index < 3; ++index)
            *checks[index] = index < failure.failedIndex ? CheckpointResult::success :
                            index == failure.failedIndex ? CheckpointResult::failed : CheckpointResult::pending;
        model.onSnapshot(status, 500);
        PHPVPN_CHECK_EQ(model.view().stage, ProgressStage::failed);
        PHPVPN_CHECK_EQ(model.view().errorDescription, failure.text);
        PHPVPN_CHECK_EQ(model.view().serverUrl, status.serverUrl);
        PHPVPN_CHECK_EQ(model.view().serverSecret, status.serverSecret);
        PHPVPN_CHECK_EQ(model.view().accessKey, status.accessKey);
        model.onTick(99999999);
        PHPVPN_CHECK(!model.view().closeRequested);
        status.state = "connected";
        status.hasError = false;
        model.onSnapshot(status, 100000000);
        PHPVPN_CHECK_EQ(model.view().stage, ProgressStage::failed);
        PHPVPN_CHECK_EQ(model.view().errorDescription, failure.text);
        for (const char* forbidden : {"https://", "ciphertext", "C:\\", "DNS", "TLS", "10.0."})
            PHPVPN_CHECK(model.view().errorDescription.find(forbidden) == std::string::npos);
        model.onClose();
        PHPVPN_CHECK(model.view().closeRequested);
    }
}

PHPVPN_TEST(TrayProgress, FinalSuccessFreezesAndNewConnectResetsPresentation) {
    ProgressModel model;
    auto status = connecting();
    status.state = "connected";
    status.phase = "running";
    status.tunnelReady = "yes";
    model.onSnapshot(status, 1000);
    status.state = "error";
    model.onSnapshot(status, 1500);
    PHPVPN_CHECK_EQ(model.view().stage, ProgressStage::succeeded);
    model = ProgressModel{};
    PHPVPN_CHECK_EQ(model.view().stage, ProgressStage::connecting);
    PHPVPN_CHECK_EQ(model.view().serverUrl, CheckpointResult::pending);
    PHPVPN_CHECK(!model.view().closeRequested);
}

PHPVPN_TEST(TrayProgress, ClosingStopsOnlyLocalReadsAndRemoteDisconnectClosesPendingWindow) {
    ProgressModel model;
    PollSchedule progress(kProgressIntervalMs);
    progress.start(0);
    model.onSnapshot(connecting(), 0);
    model.onClose();
    progress.stop();
    PHPVPN_CHECK(model.view().closeRequested);
    PHPVPN_CHECK(!progress.onTimer(500));
    PHPVPN_CHECK(!progress.onTimer(5000));
    // The model and schedule expose no command or transport writer capability.
    model = ProgressModel{};
    auto disconnected = connecting();
    disconnected.state = "disconnected";
    disconnected.phase = "idle";
    model.onSnapshot(disconnected, 500);
    PHPVPN_CHECK(model.view().closeRequested);
    model = ProgressModel{true};
    model.onSnapshot(TrayStatus{}, 500);
    model.onTick(999999);
    PHPVPN_CHECK(!model.view().closeRequested);
    model.onCommandFailure();
    disconnected.tunnelReady = "no";
    model.onSnapshot(disconnected, 1000000);
    PHPVPN_CHECK_EQ(model.view().stage, ProgressStage::failed);
    PHPVPN_CHECK(!model.successDeadlineMs());
}

PHPVPN_TEST(TrayPolling, ProgressAndExtraStatisticsReadNeverMoveFiveSecondDeadlines) {
    PollSchedule tray(kStatusIntervalMs);
    PollSchedule progress(kProgressIntervalMs);
    tray.start(0);
    progress.start(1200);
    std::vector<std::int64_t> baseRequests;
    std::vector<std::int64_t> progressRequests;
    int extraStatisticsRequests = 0;
    for (std::int64_t now = 0; now <= 15000; now += 100) {
        if (now == 2300) ++extraStatisticsRequests; // Same direct read used by menu.
        if (now == 7200) progress.stop();
        if (tray.onTimer(now)) baseRequests.push_back(now);
        if (progress.onTimer(now)) progressRequests.push_back(now);
    }
    PHPVPN_CHECK_EQ(baseRequests, (std::vector<std::int64_t>{5000, 10000, 15000}));
    PHPVPN_CHECK_EQ(progressRequests, (std::vector<std::int64_t>{1700, 2200, 2700, 3200, 3700, 4200, 4700, 5200, 5700, 6200, 6700}));
    PHPVPN_CHECK_EQ(extraStatisticsRequests, 1);
    PHPVPN_CHECK_EQ(tray.nextMs(), 20000);
    PHPVPN_CHECK(!progress.active());
}

PHPVPN_TEST(TrayPolling, StatisticsAndCommandReadsQueueBehindBusyPeriodicRead) {
    ReadRequestQueue queue;
    PollSchedule tray(kStatusIntervalMs);
    tray.start(0);
    PHPVPN_CHECK(tray.onTimer(5000));
    PHPVPN_CHECK(queue.request()); // Worker starts for regular status.
    PHPVPN_CHECK(!queue.request()); // Statistics opening queues a real read.
    PHPVPN_CHECK(!queue.request()); // A command completion queues another.
    PHPVPN_CHECK(queue.finish()); // Worker must perform Statistics read.
    PHPVPN_CHECK(queue.finish()); // Worker must perform post-command read.
    PHPVPN_CHECK(!queue.finish());
    PHPVPN_CHECK_EQ(tray.nextMs(), 10000);
    PHPVPN_CHECK(queue.request()); // Next periodic read starts normally.
    queue.stop();
    PHPVPN_CHECK(queue.request()); // Reopening gets a fresh queue.
    PHPVPN_CHECK(!queue.finish());
}
