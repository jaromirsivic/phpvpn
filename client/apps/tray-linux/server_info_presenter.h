// GTK-free state and presentation of the Server information window (tray_UX.md
// UX06, client.md menu item 7), the Linux counterpart of the request, finish,
// display and auto refresh logic of apps/tray/server_info_window.cpp. The shared
// core model (presentation/server_info_model, table_pagination.h) validates the
// snapshot; this layer keeps the window generation, the pending request, the
// retained snapshot, the page and the Auto refresh deadline, and produces the
// exact Windows texts. It never performs IPC itself.
#pragma once
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/presentation/server_info_model.h"
#include "core/presentation/table_pagination.h"

namespace phpvpn::tray {
inline constexpr std::string_view kServerInfoTitle = "Server information";
inline constexpr std::string_view kServerInfoWhoTab = "Who is online";
inline constexpr std::string_view kServerInfoRawTab = "Raw server information";
inline constexpr std::string_view kServerInfoColumns[3]{"Contract", "Last seen", "Status"};
inline constexpr std::string_view kServerInfoRefresh = "Refresh";
inline constexpr std::string_view kServerInfoAutoRefresh = "Auto refresh";
inline constexpr std::string_view kServerInfoOk = "OK";
inline constexpr std::string_view kServerInfoLoading = "Loading...";
inline constexpr std::string_view kServerInfoBusyRefresh = "Server information is busy. Try Refresh again.";
inline constexpr std::string_view kServerInfoBusyOpen = "Server information is busy. Try opening it again.";
inline constexpr std::string_view kServerInfoRefreshFailed = "Refresh failed. Previous information is retained.";
inline constexpr std::string_view kServerInfoUnverified =
    "Server information could not be verified. Check the service, configuration and server compatibility.";
inline constexpr std::string_view kServerInfoModelUnverified = "Server information could not be verified.";

// Local date and time of a server lastSeen value in the user's locale and time
// zone ("%x %X", the counterpart of the Windows short date and time format).
// Empty when the instant is not representable; ISO-like text when the locale
// does not produce UTF-8.
std::string serverInfoLocalTime(std::int64_t unixMs);

struct ServerInfoTableRow {
    std::string contract, lastSeen, status;
    ServerClientState state = ServerClientState::offline;
};

struct ServerInfoPagerButton {
    std::string label;
    int page = -1;  // -1: the disabled ellipsis
    bool enabled = true;
    bool current = false;
    std::string accessibleName;
};
// First, previous, numbered (with ellipses), next and last page, exactly as the
// Windows pager. Empty when everything fits on one page.
std::vector<ServerInfoPagerButton> serverInfoPager(int pageCount, int current);

class ServerInfoSession {
public:
    // --- request lifecycle, UI thread only ------------------------------------
    bool pending() const { return pending_; }
    std::uint64_t generation() const { return generation_; }
    // Starts one request when none is pending and returns the token the result
    // must carry. Clears the Auto refresh deadline: no deadline runs during a request.
    std::optional<std::uint64_t> begin();

    struct NotSent {
        std::string_view notice;
        // Without a verified snapshot the window reports this and closes.
        std::optional<std::string_view> fatal;
    };
    // The request could not be handed to the IPC channel.
    NotSent notSent(std::int64_t nowMs);

    enum class Outcome { ignored, first, replaced, retained, fatal };
    struct Finished {
        Outcome outcome = Outcome::ignored;
        std::string_view text;  // retained: notice, fatal: error message
        bool rawChanged = false;
        // Row on the (possibly changed) current page that keeps the selection.
        std::optional<std::size_t> selectedRow;
    };
    // Result of the request with token. selectedRow is the selected row of the
    // current page before the update; the selection follows the same peer.
    Finished finish(std::uint64_t token, std::string_view response, std::optional<std::size_t> selectedRow,
                    std::int64_t nowMs);
    // Close or hide: revokes the pending request (a late result is ignored) and
    // stops Auto refresh. The retained snapshot stays until reset().
    void invalidate();
    // A new window: invalidate() plus no snapshot and the first page.
    void reset();

    // --- Auto refresh (off at every open, memory only) --------------------------
    bool autoRefresh() const { return autoRefresh_; }
    // The switch changed: a full new interval starts when enabled and idle.
    void setAutoRefresh(bool enabled, std::int64_t nowMs);
    // Reactivation by Show Info and hiding switch it off.
    void stopAutoRefresh();
    std::optional<std::int64_t> autoDeadline() const { return autoDeadline_; }
    // The timer fired. True: start the refresh now (through the Refresh path).
    // False before the deadline (the deadline stays) or while not applicable.
    bool autoDue(std::int64_t nowMs);

    // --- presentation -----------------------------------------------------------
    const std::optional<ServerInfoModel>& model() const { return model_; }
    std::string countText() const;
    // "Server information (N online clients, M valid contracts)".
    std::string title() const;
    int page() const { return page_; }
    int pageCount() const;
    void setPage(int page);
    std::vector<ServerInfoTableRow> pageRows() const;
    std::vector<ServerInfoPagerButton> pager() const { return serverInfoPager(pageCount(), page_); }

private:
    void schedule(std::int64_t nowMs);
    std::optional<ServerInfoModel> model_;
    int page_ = 0;
    bool pending_ = false;
    std::uint64_t generation_ = 1, requestGeneration_ = 0;
    bool autoRefresh_ = false;
    std::optional<std::int64_t> autoDeadline_;
};
}  // namespace phpvpn::tray
