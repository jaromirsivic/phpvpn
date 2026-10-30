// Server information window of the macOS tray (menu "Show Server Info", tray_UX.md
// UX06), the counterpart of apps/tray/server_info_window and
// apps/tray-linux/server_info_window. The GTK-free ServerInfoSession of
// apps/tray-linux/server_info_presenter keeps the generation, pending request,
// retained snapshot, page and Auto refresh deadline; this adapter shows the tabs
// "Who is online" (20-row pages) and "Raw server information" (exact plaintext),
// Refresh, Auto refresh and OK. The window appears only after the first verified
// snapshot; re-choosing Show Info activates it without IPC and switches Auto refresh off.
#pragma once
#include <cstdint>
#include <optional>
#include <string>

#include "ipc_async.h"
#include "lifecycle.h"
#include "mac_ui.h"
#include "server_info_presenter.h"

@class PVServerInfoTable;

namespace phpvpn::tray {
// Registered as a lifecycle participant only for the terminal session end.
class ServerInfoWindow final : public LifecycleParticipant {
public:
    ServerInfoWindow(IpcEndpoint endpoint, Dispatcher dispatcher);
    ~ServerInfoWindow() override;
    ServerInfoWindow(const ServerInfoWindow&) = delete;
    ServerInfoWindow& operator=(const ServerInfoWindow&) = delete;
    void open();
    NSWindow* window() const { return window_; }

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
    std::optional<std::size_t> selectedRow() const;
    void selectRow(std::size_t row);
    void close();

    IpcChannel channel_;
    ServerInfoSession session_;
    PVWindow* window_ = nil;
    PVStackView* root_ = nil;
    NSTextField* count_ = nil;
    NSTabView* tabs_ = nil;
    NSTableView* table_ = nil;
    PVServerInfoTable* tableData_ = nil;
    NSScrollView* pagerScroll_ = nil;
    PVStackView* pager_ = nil;
    NSTextView* raw_ = nil;
    NSTextField* notice_ = nil;
    NSButton* refresh_ = nil;
    NSSwitch* autoRefresh_ = nil;
    mac::Timer autoTimer_;
    mac::Deferred deferred_;
};
}  // namespace phpvpn::tray
