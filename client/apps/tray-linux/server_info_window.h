// Server information window of the GTK tray (menu "Show Server Info", tray_UX.md
// UX06), the counterpart of apps/tray/server_info_window. One correlated
// get-server-info (the service runs it on its diagnostic executor) per Refresh;
// the window is shown only after the first complete snapshot validated. Tabs
// "Who is online" (20-row pages) and "Raw server information" (exact plaintext),
// Refresh, Auto refresh (every kServerInfoAutoRefreshMs after completion) and OK.
// Re-choosing Show Info activates the open window without IPC and switches Auto
// refresh off. Closing revokes the pending request.
#pragma once
#include <gtk/gtk.h>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "ipc_async.h"
#include "lifecycle.h"
#include "server_info_presenter.h"

namespace phpvpn::tray {
class TrayApp;

// Registered as a lifecycle participant only for the terminal session end: it has
// no draft, so its snapshot is never open, dirty or unknown.
class ServerInfoWindow final : public LifecycleParticipant {
public:
    ServerInfoWindow(IpcEndpoint endpoint, Dispatcher dispatcher);
    ~ServerInfoWindow() override;
    ServerInfoWindow(const ServerInfoWindow&) = delete;
    ServerInfoWindow& operator=(const ServerInfoWindow&) = delete;
    void open();
    GtkWindow* window() const { return window_ ? GTK_WINDOW(window_) : nullptr; }

    LifecycleSnapshot lifecycleSnapshot() const override { return {}; }
    bool approvedDiscard(const LifecycleSnapshot&) override { return true; }
    void endSession() override;

private:
    void build();
    void request();
    void finish(std::uint64_t token, std::string& response);
    void display();
    void showPage(int page);
    void rebuildPager();
    void setNotice(std::string_view text);
    void setRaw();
    void syncAutoTimer();
    void onAutoTimer();
    // Wraps the count under the heading and OK under Refresh/Auto refresh when narrow.
    void placeWrapped();
    bool onKey(GdkEventKey* event);
    std::optional<std::size_t> selectedRow() const;
    void selectRow(std::size_t row);
    void onDestroyed();

    IpcChannel channel_;
    ServerInfoSession session_;
    GtkWidget* window_ = nullptr;
    GtkWidget *content_ = nullptr, *header_ = nullptr, *heading_ = nullptr, *count_ = nullptr, *tabs_ = nullptr;
    GtkWidget *table_ = nullptr, *raw_ = nullptr, *pagerBox_ = nullptr, *notice_ = nullptr;
    GtkWidget *footer_ = nullptr, *footerPair_ = nullptr, *refresh_ = nullptr, *autoRefresh_ = nullptr, *ok_ = nullptr;
    GtkListStore* rows_ = nullptr;
    guint autoTimer_ = 0, placement_ = 0;
    bool headerWrapped_ = false, footerWrapped_ = false;
    bool updatingSwitch_ = false;
};

// Creates the window and connects TrayWindowHooks::openServerInfo and the session end.
void installServerInfoWindow(TrayApp& app);
}  // namespace phpvpn::tray
