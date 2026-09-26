// GTK-free Server information and Statistics logic of the Linux tray (WP10c):
// the request/finish/auto refresh state of apps/tray-linux/server_info_presenter
// against realistic get-server-info results (also through a real Unix-socket
// IpcChannel), the Windows pager and texts, and the Statistics texts and layout
// decisions for large uint64 counters that travel through the get-status JSON
// (client.md items 33-35, tray_UX.md UX06/UX07).
#include "harness.h"

#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <clocale>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <limits>
#include <mutex>

#include "../linux/tray/server_info_fixture.h"
#include "core/ipc/ipc_protocol.h"
#include "core/protocol/json.h"
#include "ipc_async.h"
#include "platform/linux/ipc_unix.h"
#include "server_info_presenter.h"
#include "statistics_presenter.h"

namespace phpvpn::tray {
namespace {
constexpr char kRequestId[] = "8a1f9a26-6d0c-4c58-9f1d-2f0a7f6b1c30";
constexpr std::int64_t kGeneratedAt = 1790416800000;  // 2026-09-26T10:00:00Z

std::string infoResponse(std::size_t peers, bool crlf = false) {
    fixture::ServerInfoOptions options;
    options.peers = peers;
    options.generatedAtMs = kGeneratedAt;
    options.crlf = crlf;
    return IpcProtocol::buildServerInfoResponse(kRequestId, fixture::serverInfoSnapshot(options));
}

ServerInfoSession::Finished load(ServerInfoSession& session, const std::string& response,
                                 std::optional<std::size_t> selected = std::nullopt, std::int64_t now = 0) {
    const auto token = session.begin();
    if (!token) return {};
    return session.finish(*token, response, selected, now);
}

std::vector<std::string> labels(const std::vector<ServerInfoPagerButton>& buttons) {
    std::vector<std::string> result;
    for (const auto& button : buttons) result.push_back(button.label);
    return result;
}

class QueueDispatcher {
public:
    Dispatcher dispatcher() {
        return [this](std::function<void()> task) {
            { std::lock_guard lock(mutex_); tasks_.push_back(std::move(task)); }
            changed_.notify_all();
        };
    }
    template <class Predicate>
    bool pumpUntil(Predicate done, int limitMs = 3000) {
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
private:
    std::mutex mutex_;
    std::condition_variable changed_;
    std::deque<std::function<void()>> tasks_;
};

class TempSocketDir {
public:
    TempSocketDir() {
        char pattern[] = "/tmp/phpvpn-info-XXXXXX";
        const char* made = mkdtemp(pattern);
        path_ = made ? made : "";
    }
    ~TempSocketDir() { std::error_code error; std::filesystem::remove_all(path_, error); }
    std::string socket() const { return path_ + "/control.sock"; }
private:
    std::string path_;
};

std::string statusJson(const TrafficCountersSnapshot& counters, const char* requestId = kRequestId) {
    StatusSnapshot snapshot;
    snapshot.state = ConnectionState::connected;
    snapshot.phase = ConnectionPhase::running;
    snapshot.tunnelReady = Tristate::yes;
    snapshot.stateChangedAtUtc = "2026-09-26T10:00:00.000Z";
    snapshot.connectionStartedAtUtc = "2026-09-26T10:00:00.000Z";
    snapshot.clientVersion = "1.0.0";
    snapshot.connectionEpoch = 3;
    snapshot.connectAttemptId = "3f2a7c1e-9b44-4b0e-8d2f-5c6a1e7b9d03";
    snapshot.counters = counters;
    return IpcProtocol::buildStatusResponse(requestId, snapshot);
}

// Reverses the member order of the status object (the parser must not care).
std::string reversedStatusJson(const std::string& json) {
    auto document = Json::parse(json).value();
    for (auto& member : document.objectValue())
        if (member.first == "status") std::reverse(member.second.objectValue().begin(), member.second.objectValue().end());
    return Json::serialize(document);
}

class ScopedUtcCLocale {
public:
    ScopedUtcCLocale() {
        const char* tz = getenv("TZ");
        if (tz) previous_ = tz;
        hadTz_ = tz != nullptr;
        setenv("TZ", "UTC", 1);
        tzset();
        std::setlocale(LC_TIME, "C");
    }
    ~ScopedUtcCLocale() {
        if (hadTz_) setenv("TZ", previous_.c_str(), 1);
        else unsetenv("TZ");
        tzset();
    }
private:
    std::string previous_;
    bool hadTz_ = false;
};
}  // namespace
}  // namespace phpvpn::tray

using namespace phpvpn;
using namespace phpvpn::tray;

PHPVPN_TEST(tray_linux_info, first_snapshot_title_rows_and_windows_texts) {
    ScopedUtcCLocale utc;
    ServerInfoSession session;
    PHPVPN_CHECK_EQ(session.title(), "Server information");
    const auto result = load(session, infoResponse(6));
    PHPVPN_CHECK(result.outcome == ServerInfoSession::Outcome::first);
    PHPVPN_CHECK(result.rawChanged);
    PHPVPN_CHECK(!session.pending());
    // Peers .3 .6 valid, .4 .7 invalid, .5 .8 offline: 4 online, 2 valid.
    PHPVPN_CHECK_EQ(session.countText(), "(4 online clients, 2 valid contracts)");
    PHPVPN_CHECK_EQ(session.title(), "Server information (4 online clients, 2 valid contracts)");
    const auto rows = session.pageRows();
    PHPVPN_CHECK_EQ(rows.size(), std::size_t(6));
    PHPVPN_CHECK_EQ(rows[0].contract, "10.99.99.2_10.99.99.3");
    PHPVPN_CHECK_EQ(rows[0].status, "Online - valid communication contract");
    PHPVPN_CHECK_EQ(rows[1].contract, "10.99.99.2_10.99.99.6");
    PHPVPN_CHECK_EQ(rows[2].status, "Online - invalid communication contract");
    PHPVPN_CHECK_EQ(rows[5].status, "Offline");
    PHPVPN_CHECK(rows[0].state == ServerClientState::valid && rows[3].state == ServerClientState::invalid &&
                 rows[4].state == ServerClientState::offline);
    PHPVPN_CHECK_EQ(rows[0].lastSeen, "09/26/26 09:59:58");
    PHPVPN_CHECK_EQ(serverInfoLocalTime(0), "01/01/70 00:00:00");
    PHPVPN_CHECK_EQ(serverInfoLocalTime(-1), "12/31/69 23:59:59");
    // The exact authenticated plaintext is retained unchanged (pretty JSON, LF).
    const auto& raw = session.model()->snapshot.infoPlaintext;
    PHPVPN_CHECK(raw.find("\n    \"serverVersion\": \"1.0.0-synthetic\"") != std::string::npos);
    PHPVPN_CHECK(raw.find("maskedCommunicationContracts") != std::string::npos);
    PHPVPN_CHECK(session.pager().empty());
    PHPVPN_CHECK_EQ(std::string(kServerInfoWhoTab), "Who is online");
    PHPVPN_CHECK_EQ(std::string(kServerInfoRawTab), "Raw server information");
    PHPVPN_CHECK_EQ(std::string(kServerInfoColumns[0]) + "|" + std::string(kServerInfoColumns[1]) + "|" +
                        std::string(kServerInfoColumns[2]), "Contract|Last seen|Status");
}

PHPVPN_TEST(tray_linux_info, crlf_plaintext_and_singular_count) {
    ServerInfoSession session;
    PHPVPN_CHECK(load(session, infoResponse(1, true)).outcome == ServerInfoSession::Outcome::first);
    PHPVPN_CHECK_EQ(session.title(), "Server information (1 online client, 1 valid contract)");
    PHPVPN_CHECK(session.model()->snapshot.infoPlaintext.find("\r\n") != std::string::npos);
    ServerInfoSession empty;
    PHPVPN_CHECK(load(empty, infoResponse(0)).outcome == ServerInfoSession::Outcome::first);
    PHPVPN_CHECK_EQ(empty.title(), "Server information (0 online clients, 0 valid contracts)");
    PHPVPN_CHECK(empty.pageRows().empty());
}

PHPVPN_TEST(tray_linux_info, pagination_twenty_rows_and_windows_pager) {
    ServerInfoSession session;
    PHPVPN_CHECK(load(session, infoResponse(252)).outcome == ServerInfoSession::Outcome::first);
    PHPVPN_CHECK_EQ(session.pageCount(), 13);
    PHPVPN_CHECK_EQ(session.pageRows().size(), std::size_t(20));
    PHPVPN_CHECK_EQ(session.title(), "Server information (168 online clients, 84 valid contracts)");
    auto pager = session.pager();
    PHPVPN_CHECK((labels(pager) == std::vector<std::string>{"«", "‹", "1", "2", "3", "4", "5", "6", "…", "13", "›", "»"}));
    PHPVPN_CHECK(!pager[0].enabled && !pager[1].enabled && pager[10].enabled && pager[11].enabled);
    PHPVPN_CHECK_EQ(pager[0].accessibleName, "First page");
    PHPVPN_CHECK_EQ(pager[1].accessibleName, "Previous page");
    PHPVPN_CHECK_EQ(pager[2].accessibleName, "Page 1 of 13, current page");
    PHPVPN_CHECK(pager[2].current && !pager[3].current);
    PHPVPN_CHECK_EQ(pager[3].accessibleName, "Page 2 of 13");
    PHPVPN_CHECK(pager[8].page == -1 && !pager[8].enabled && pager[8].accessibleName.empty());
    PHPVPN_CHECK_EQ(pager[10].accessibleName, "Next page");
    PHPVPN_CHECK_EQ(pager[11].accessibleName, "Last page");
    PHPVPN_CHECK(pager[10].page == 1 && pager[11].page == 12);
    session.setPage(6);
    PHPVPN_CHECK((labels(session.pager()) ==
                  std::vector<std::string>{"«", "‹", "1", "…", "5", "6", "7", "8", "9", "…", "13", "›", "»"}));
    session.setPage(99);
    PHPVPN_CHECK_EQ(session.page(), 12);
    PHPVPN_CHECK_EQ(session.pageRows().size(), std::size_t(12));  // last page, Ctrl+End target
    pager = session.pager();
    PHPVPN_CHECK(!pager[pager.size() - 1].enabled && !pager[pager.size() - 2].enabled && pager[0].enabled);
    session.setPage(-5);
    PHPVPN_CHECK_EQ(session.page(), 0);
    // Sorted valid, invalid, offline, numerically inside each state.
    const auto& all = session.model()->rows;
    for (std::size_t i = 1; i < all.size(); ++i)
        PHPVPN_CHECK(all[i - 1].state < all[i].state || (all[i - 1].state == all[i].state && all[i - 1].peer < all[i].peer));
    PHPVPN_CHECK(serverInfoPager(1, 0).empty());
    PHPVPN_CHECK((labels(serverInfoPager(7, 3)) == std::vector<std::string>{"«", "‹", "1", "2", "3", "4", "5", "6", "7", "›", "»"}));
}

PHPVPN_TEST(tray_linux_info, error_states_match_windows) {
    // Without a verified snapshot every failure closes the window with its message.
    ServerInfoSession first;
    auto result = load(first, "{\"ipcVersion\":1,\"requestId\":\"8a1f9a26-6d0c-4c58-9f1d-2f0a7f6b1c30\",\"result\":{}}");
    PHPVPN_CHECK(result.outcome == ServerInfoSession::Outcome::fatal);
    PHPVPN_CHECK_EQ(std::string(result.text),
                    "Server information could not be verified. Check the service, configuration and server compatibility.");
    ServerInfoSession empty;
    PHPVPN_CHECK(load(empty, "").outcome == ServerInfoSession::Outcome::fatal);  // timeout, no service, uncorrelated
    ServerInfoSession busy;
    PHPVPN_CHECK(load(busy, IpcProtocol::buildError(kRequestId, Error(ErrorCode::busy))).outcome ==
                 ServerInfoSession::Outcome::fatal);
    ServerInfoSession unsent;
    PHPVPN_CHECK(unsent.begin().has_value());
    auto notSent = unsent.notSent(0);
    PHPVPN_CHECK(!unsent.pending());
    PHPVPN_CHECK_EQ(std::string(notSent.notice), "Server information is busy. Try Refresh again.");
    PHPVPN_CHECK(notSent.fatal && *notSent.fatal == "Server information is busy. Try opening it again.");

    // With a snapshot a failure keeps it and shows the notice.
    ServerInfoSession session;
    PHPVPN_CHECK(load(session, infoResponse(30)).outcome == ServerInfoSession::Outcome::first);
    session.setPage(1);
    const auto raw = session.model()->snapshot.infoPlaintext;
    result = load(session, IpcProtocol::buildError(kRequestId, Error(ErrorCode::transport_timeout)));
    PHPVPN_CHECK(result.outcome == ServerInfoSession::Outcome::retained);
    PHPVPN_CHECK_EQ(std::string(result.text), "Refresh failed. Previous information is retained.");
    PHPVPN_CHECK(session.model()->snapshot.infoPlaintext == raw && session.page() == 1 && session.pageRows().size() == 10);
    PHPVPN_CHECK(session.begin().has_value());
    notSent = session.notSent(0);
    PHPVPN_CHECK(!notSent.fatal);
    PHPVPN_CHECK_EQ(std::string(notSent.notice), "Server information is busy. Try Refresh again.");
    PHPVPN_CHECK_EQ(std::string(kServerInfoLoading), "Loading...");
    // A successful refresh replaces table, count and raw text together.
    result = load(session, infoResponse(1));
    PHPVPN_CHECK(result.outcome == ServerInfoSession::Outcome::replaced && result.rawChanged);
    PHPVPN_CHECK_EQ(session.page(), 0);
    PHPVPN_CHECK_EQ(session.title(), "Server information (1 online client, 1 valid contract)");
    // The same plaintext does not replace the raw text (selection and scroll stay).
    result = load(session, infoResponse(1));
    PHPVPN_CHECK(result.outcome == ServerInfoSession::Outcome::replaced && !result.rawChanged);
}

PHPVPN_TEST(tray_linux_info, single_flight_generation_and_selection) {
    ServerInfoSession session;
    const auto token = session.begin();
    PHPVPN_CHECK(token.has_value());
    PHPVPN_CHECK(!session.begin().has_value());  // Refresh is disabled while pending
    session.invalidate();                        // close: the late result must not apply
    PHPVPN_CHECK(!session.pending());
    PHPVPN_CHECK(session.finish(*token, infoResponse(3), std::nullopt, 0).outcome == ServerInfoSession::Outcome::ignored);
    PHPVPN_CHECK(!session.model());
    const auto fresh = session.begin();
    PHPVPN_CHECK(fresh && *fresh != *token);
    PHPVPN_CHECK(session.finish(*token, infoResponse(3), std::nullopt, 0).outcome == ServerInfoSession::Outcome::ignored);
    PHPVPN_CHECK(session.pending());
    PHPVPN_CHECK(session.finish(*fresh, infoResponse(45), std::nullopt, 0).outcome == ServerInfoSession::Outcome::first);
    // Select the third row of page 2 (sorted index 22) and refresh with fewer peers:
    // the selection follows the same peer to its new page and row.
    session.setPage(1);
    const auto peer = session.model()->rows[22].peer;
    const auto result = load(session, infoResponse(40), std::size_t(2));
    PHPVPN_CHECK(result.outcome == ServerInfoSession::Outcome::replaced);
    PHPVPN_CHECK(result.selectedRow.has_value());
    const auto index = static_cast<std::size_t>(session.page()) * kTablePageSize + result.selectedRow.value_or(0);
    PHPVPN_CHECK(index < session.model()->rows.size() && session.model()->rows[index].peer == peer);
    // A peer that disappeared leaves no selection; the page is clamped.
    session.setPage(2);
    PHPVPN_CHECK(!load(session, infoResponse(2), std::size_t(0)).selectedRow);
    PHPVPN_CHECK_EQ(session.page(), 0);
    session.reset();
    PHPVPN_CHECK(!session.model() && session.page() == 0 && !session.autoRefresh());
}

PHPVPN_TEST(tray_linux_info, auto_refresh_full_interval_after_completion) {
    ServerInfoSession session;
    PHPVPN_CHECK(load(session, infoResponse(2), std::nullopt, 0).outcome == ServerInfoSession::Outcome::first);
    PHPVPN_CHECK(!session.autoRefresh() && !session.autoDeadline());  // off at every open
    session.setAutoRefresh(true, 1000);
    PHPVPN_CHECK(session.autoDeadline() == std::optional<std::int64_t>(1000 + kServerInfoAutoRefreshMs));
    PHPVPN_CHECK(!session.autoDue(5999));  // an early tick is not the deadline
    PHPVPN_CHECK(session.autoDeadline().has_value());
    PHPVPN_CHECK(session.autoDue(6000));
    PHPVPN_CHECK(!session.autoDeadline());
    // The refresh runs: no deadline while pending, a manual Refresh cannot overlap.
    auto token = session.begin();
    PHPVPN_CHECK(token && !session.autoDeadline() && !session.begin());
    session.setAutoRefresh(true, 6500);  // toggling during the request does not arm
    PHPVPN_CHECK(!session.autoDeadline());
    PHPVPN_CHECK(!session.autoDue(20000));
    // Completion (also an error) starts a new full interval without catching up.
    session.finish(*token, "", std::nullopt, 9000);
    PHPVPN_CHECK(session.autoDeadline() == std::optional<std::int64_t>(14000));
    // Disabling during a request leaves no timer after completion.
    token = session.begin();
    session.setAutoRefresh(false, 9500);
    session.finish(*token, infoResponse(2), std::nullopt, 12000);
    PHPVPN_CHECK(!session.autoDeadline() && !session.autoDue(20000));
    // A request that could not be sent also restarts the interval.
    session.setAutoRefresh(true, 13000);
    token = session.begin();
    session.notSent(15000);
    PHPVPN_CHECK(session.autoDeadline() == std::optional<std::int64_t>(20000));
    // Re-choosing Show Info and closing switch it off.
    session.stopAutoRefresh();
    PHPVPN_CHECK(!session.autoRefresh() && !session.autoDeadline() && !session.autoDue(30000));
    session.setAutoRefresh(true, 0);
    session.invalidate();
    PHPVPN_CHECK(!session.autoRefresh() && !session.autoDeadline());
    PHPVPN_CHECK_EQ(kServerInfoAutoRefreshMs, 5000u);
}

PHPVPN_TEST(tray_linux_info, get_server_info_through_ipc_channel) {
    TempSocketDir dir;
    QueueDispatcher queue;
    std::atomic<int> requests{0};
    IpcSocketServerOptions options;
    options.socketPath = dir.socket();
    options.expectedOwnerUid = geteuid();
    IpcSocketServer server([&](std::string_view request) {
        auto parsed = IpcProtocol::parseRequest(request);
        if (!parsed || parsed->command != IpcCommand::get_server_info) return std::string("{}");
        ++requests;
        fixture::ServerInfoOptions info;
        info.peers = 25;
        info.generatedAtMs = kGeneratedAt;
        // The second request is answered with a foreign requestId: reported empty.
        const auto id = requests == 2 ? std::string(kRequestId) : parsed->requestId;
        return IpcProtocol::buildServerInfoResponse(id, fixture::serverInfoSnapshot(info));
    }, options);
    PHPVPN_CHECK(server.start().ok());
    IpcChannel channel({dir.socket(), geteuid()}, queue.dispatcher());
    ServerInfoSession session;
    std::optional<ServerInfoSession::Finished> finished;
    auto send = [&] {
        const auto token = session.begin();
        finished.reset();
        return token && channel.request(IpcCommand::get_server_info, {}, [&, value = *token](std::string& response) {
            finished = session.finish(value, response, std::nullopt, 0);
        });
    };
    PHPVPN_CHECK(send());
    PHPVPN_CHECK(queue.pumpUntil([&] { return finished.has_value(); }));
    PHPVPN_CHECK(finished && finished->outcome == ServerInfoSession::Outcome::first);
    PHPVPN_CHECK_EQ(session.pageCount(), 2);
    PHPVPN_CHECK(send());
    PHPVPN_CHECK(queue.pumpUntil([&] { return finished.has_value(); }));
    PHPVPN_CHECK(finished && finished->outcome == ServerInfoSession::Outcome::retained);
    // Closing while pending: abandon revokes the delivery.
    PHPVPN_CHECK(send());
    channel.abandon();
    session.invalidate();
    queue.pumpUntil([] { return false; }, 300);
    PHPVPN_CHECK(!finished.has_value());
    channel.stop();
    server.stop();
    PHPVPN_CHECK_EQ(requests.load() >= 2, true);
}

PHPVPN_TEST(tray_linux_statistics, placeholders_before_first_snapshot) {
    StatisticsModel model;
    auto texts = statisticsTexts(model);
    for (const auto& value : texts.values) PHPVPN_CHECK_EQ(value, "-");
    PHPVPN_CHECK_EQ(texts.freshness, "Waiting for service status");
    PHPVPN_CHECK_EQ(texts.connectionStatus, "(Disconnected)");
    PHPVPN_CHECK(!texts.connected);
    // An invalid first read keeps the dashes and the waiting text.
    model.update(TrayStatus{}, true);
    texts = statisticsTexts(model);
    PHPVPN_CHECK_EQ(texts.values[0], "-");
    PHPVPN_CHECK_EQ(texts.freshness, "Waiting for service status");
    PHPVPN_CHECK_EQ(std::string(kStatisticsWindowTitle), "PHPVPN Statistics");
    PHPVPN_CHECK_EQ(std::string(kStatisticsHeading), "Statistics");
    PHPVPN_CHECK_EQ(std::string(kStatisticsDescription), "Read-only global service counters for the current VPN connection.");
}

PHPVPN_TEST(tray_linux_statistics, large_counters_travel_through_get_status_json) {
    const auto json = statusJson(fixture::largeCounters());
    for (const auto& body : {json, reversedStatusJson(json)}) {
        const auto status = parseStatusResponse(body);
        PHPVPN_CHECK(status.valid);
        PHPVPN_CHECK_EQ(status.counters.dataPollsTotal, std::numeric_limits<std::uint64_t>::max());
        PHPVPN_CHECK_EQ(status.counters.dataPollsEmpty, 9007199254740993ULL);
        StatisticsModel model;
        model.update(status, false);
        const auto texts = statisticsTexts(model);
        const std::array<std::string, 10> expected{
            "9,007,199,254,740,993", "18,437,736,874,454,810,622", "18,446,744,073,709,551,615",
            "18446.74 PB", "18446.74 PB", "36893.49 PB",
            "9,223,372,036,854,775,807", "999 B", "9,223,372,036,854,775,808", "1.00 MB"};
        for (std::size_t i = 0; i < expected.size(); ++i) PHPVPN_CHECK_EQ(texts.values[i], expected[i]);
        PHPVPN_CHECK_EQ(texts.freshness, "");
        PHPVPN_CHECK(texts.connected);
        PHPVPN_CHECK(texts.connectionStatus.rfind("(Connected for ", 0) == 0);
    }
    // A broken polling invariant is not a valid snapshot.
    auto broken = fixture::largeCounters();
    broken.dataPollsTotal -= 1;
    PHPVPN_CHECK(!parseStatusResponse(statusJson(broken)).valid);
}

PHPVPN_TEST(tray_linux_statistics, stale_keeps_the_whole_last_snapshot) {
    StatisticsModel model;
    TrafficCountersSnapshot counters;
    counters.dataPollsTotal = 12;
    counters.dataPollsEmpty = 9;
    counters.dataPollsWithPayload = 3;
    counters.serializedDataRequestBodyBytes = 3458;
    counters.serializedDataResponseBodyBytes = 1000;
    counters.ipv4BytesSent = 999994;
    model.update(parseStatusResponse(statusJson(counters)), false);
    auto texts = statisticsTexts(model);
    PHPVPN_CHECK_EQ(texts.values[0], "9");
    PHPVPN_CHECK_EQ(texts.values[2], "12");
    PHPVPN_CHECK_EQ(texts.values[3], "3.46 kB");
    PHPVPN_CHECK_EQ(texts.values[4], "1.00 kB");
    PHPVPN_CHECK_EQ(texts.values[5], "4.46 kB");
    PHPVPN_CHECK_EQ(texts.values[7], "999.99 kB");
    PHPVPN_CHECK_EQ(texts.values[9], "0 B");
    // The tray passes its retained snapshot with unavailable=true on a failed read.
    model.update(parseStatusResponse("{}"), true);
    texts = statisticsTexts(model);
    PHPVPN_CHECK_EQ(texts.values[3], "3.46 kB");
    PHPVPN_CHECK_EQ(texts.freshness, "Service status unavailable. Showing the last received values.");
    // The next valid snapshot updates all ten values together.
    model.update(parseStatusResponse(statusJson(fixture::largeCounters())), false);
    texts = statisticsTexts(model);
    PHPVPN_CHECK_EQ(texts.values[0], "9,007,199,254,740,993");
    PHPVPN_CHECK_EQ(texts.values[9], "1.00 MB");
    PHPVPN_CHECK_EQ(texts.freshness, "");
}

PHPVPN_TEST(tray_linux_statistics, decimal_units_and_rounding) {
    PHPVPN_CHECK(utf8(formatStatisticsBytes(999)) == "999 B");
    PHPVPN_CHECK(utf8(formatStatisticsBytes(1000)) == "1.00 kB");
    PHPVPN_CHECK(utf8(formatStatisticsBytes(3458)) == "3.46 kB");
    PHPVPN_CHECK(utf8(formatStatisticsBytes(1005)) == "1.01 kB");  // half up
    PHPVPN_CHECK(utf8(formatStatisticsBytes(999994)) == "999.99 kB");
    PHPVPN_CHECK(utf8(formatStatisticsBytes(999995)) == "1.00 MB");  // unit change after rounding
    PHPVPN_CHECK(utf8(formatStatisticsBytes(999995000000000000ULL)) == "1000.00 PB");  // PB is the highest
    PHPVPN_CHECK(utf8(formatStatisticsBytes(addStatisticsBytes(std::numeric_limits<std::uint64_t>::max(), 1))) ==
                 "18446.74 PB");
    PHPVPN_CHECK(utf8(ui::englishInteger(0)) == "0");
    PHPVPN_CHECK(utf8(ui::englishInteger(1000)) == "1,000");
}

PHPVPN_TEST(tray_linux_statistics, cards_order_and_layout_decisions) {
    const std::array<std::string, 10> labels{"Empty polls", "Polls with payload", "Total polls", "Request body",
                                             "Response body", "Total size", "Packets sent", "Bytes sent",
                                             "Packets received", "Bytes received"};
    for (std::size_t i = 0; i < labels.size(); ++i) PHPVPN_CHECK_EQ(statisticsLabel(i), labels[i]);
    PHPVPN_CHECK_EQ(std::string(kStatisticsCardTitles[0]) + "|" + std::string(kStatisticsCardTitles[1]) + "|" +
                        std::string(kStatisticsCardTitles[2]), "Data polling|Serialized data|IPv4 traffic");
    std::array<int, 3> perCard{};
    for (std::size_t i = 0; i < 10; ++i) ++perCard[statisticsCard(i)];
    PHPVPN_CHECK(perCard[0] == 3 && perCard[1] == 3 && perCard[2] == 4);
    PHPVPN_CHECK(statisticsNarrow(759) && !statisticsNarrow(760));
    // Wide IPv4 grid: sent packets/bytes on top, received below.
    PHPVPN_CHECK(statisticsTrafficCell(6, false).row == 0 && statisticsTrafficCell(6, false).column == 0);
    PHPVPN_CHECK(statisticsTrafficCell(7, false).row == 0 && statisticsTrafficCell(7, false).column == 1);
    PHPVPN_CHECK(statisticsTrafficCell(8, false).row == 1 && statisticsTrafficCell(8, false).column == 0);
    PHPVPN_CHECK(statisticsTrafficCell(9, false).row == 1 && statisticsTrafficCell(9, false).column == 1);
    for (std::size_t i = 6; i < 10; ++i)
        PHPVPN_CHECK(statisticsTrafficCell(i, true).row == static_cast<int>(i - 6) && statisticsTrafficCell(i, true).column == 0);
    PHPVPN_CHECK(statisticsSeparatorAfter(0, false) && statisticsSeparatorAfter(1, false) && !statisticsSeparatorAfter(2, false));
    PHPVPN_CHECK(statisticsSeparatorAfter(3, false) && statisticsSeparatorAfter(4, false) && !statisticsSeparatorAfter(5, false));
    PHPVPN_CHECK(statisticsSeparatorAfter(6, false) && statisticsSeparatorAfter(7, false));
    PHPVPN_CHECK(!statisticsSeparatorAfter(8, false) && statisticsSeparatorAfter(8, true) && !statisticsSeparatorAfter(9, true));
    PHPVPN_CHECK(!statisticsStacked(200, 100, 316) && statisticsStacked(200, 101, 316));
}
