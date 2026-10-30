// Statistics window of the macOS tray (tray_UX.md UX07), the counterpart of
// apps/tray/statistics_window and apps/tray-linux/statistics_window. Ten read-only
// values from the nine global service counters of one valid get-status snapshot, in
// the cards Data polling (3), Serialized data (3) and IPv4 traffic (4, a 2 x 2 grid
// when wide). It has no IPC, reader or status timer of its own: the tray performs the
// one-shot get-config-state (menu_model.h requiresConfigState) before open() and
// passes every later snapshot of its five-second polling through addStatusListener.
#pragma once
#include <array>

#include "lifecycle.h"
#include "mac_ui.h"
#include "statistics_model.h"

namespace phpvpn::tray {
// Registered as a lifecycle participant only for the terminal session end.
class StatisticsWindow final : public LifecycleParticipant {
public:
    StatisticsWindow() = default;
    ~StatisticsWindow() override;
    StatisticsWindow(const StatisticsWindow&) = delete;
    StatisticsWindow& operator=(const StatisticsWindow&) = delete;
    void open(const TrayStatus& snapshot, bool unavailable);
    void onSnapshot(const TrayStatus& snapshot, bool unavailable);
    NSWindow* window() const { return window_; }

    LifecycleSnapshot lifecycleSnapshot() const override { return {}; }
    bool approvedDiscard(const LifecycleSnapshot&) override { return true; }
    void endSession() override;

private:
    struct Row {
        PVStackView* box = nil;
        PVStackView* line = nil;
        NSTextField* label = nil;
        NSTextField* value = nil;
        PVLineView* separator = nil;
    };
    void build();
    void refresh();
    void relayout();
    void close();

    PVWindow* window_ = nil;
    PVStackView* root_ = nil;
    PVScrollView* scroller_ = nil;
    PVStackView* content_ = nil;
    PVStackView* header_ = nil;
    NSTextField* status_ = nil;
    NSTextField* freshness_ = nil;
    PVStackView* top_ = nil;
    std::array<PVStackView*, 2> trafficLines_{};
    std::array<Row, statisticsFields.size()> rows_{};
    StatisticsModel model_;
    mac::Timer durationTimer_;
    bool narrow_ = false;
};
}  // namespace phpvpn::tray
