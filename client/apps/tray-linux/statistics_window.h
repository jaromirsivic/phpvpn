// Statistics window of the GTK tray (tray_UX.md UX07), the counterpart of
// apps/tray/statistics_window. Ten read-only values from the nine global service
// counters of one valid get-status snapshot, in the cards Data polling (3),
// Serialized data (3) and IPv4 traffic (4, a 2 x 2 grid when wide). It has no
// IPC, reader or status timer of its own: the tray performs the one-shot
// get-config-state (menu_model.h requiresConfigState) before open() and passes
// every later snapshot of its five-second polling through addStatusListener.
#pragma once
#include <gtk/gtk.h>

#include <array>

#include "lifecycle.h"
#include "statistics_presenter.h"
#include "status_model.h"

namespace phpvpn::tray {
class TrayApp;

// Registered as a lifecycle participant only for the terminal session end.
class StatisticsWindow final : public LifecycleParticipant {
public:
    StatisticsWindow() = default;
    ~StatisticsWindow() override;
    StatisticsWindow(const StatisticsWindow&) = delete;
    StatisticsWindow& operator=(const StatisticsWindow&) = delete;
    void open(const TrayStatus& snapshot, bool unavailable);
    void onSnapshot(const TrayStatus& snapshot, bool unavailable);
    GtkWindow* window() const { return window_ ? GTK_WINDOW(window_) : nullptr; }

    LifecycleSnapshot lifecycleSnapshot() const override { return {}; }
    bool approvedDiscard(const LifecycleSnapshot&) override { return true; }
    void endSession() override;

private:
    // Boxes, not grids: GtkGrid exposes its children to AT-SPI in reverse order,
    // while the accessible order must be the UX07 order. Narrow and stacked
    // layouts only change box orientations, so no widget is re-parented.
    struct Row {
        GtkWidget *box = nullptr, *line = nullptr, *label = nullptr, *value = nullptr, *separator = nullptr;
        bool stacked = false;
    };
    void build();
    void refresh();
    void relayout();
    void scrollToFocus(GtkWidget* focus);
    void onDestroyed();

    GtkWidget* window_ = nullptr;
    GtkWidget *scroller_ = nullptr, *content_ = nullptr, *header_ = nullptr, *heading_ = nullptr;
    GtkWidget *status_ = nullptr, *freshness_ = nullptr, *top_ = nullptr;
    std::array<GtkWidget*, 3> cards_{};
    // IPv4 traffic: two lines (sent, received) of two cells with a divider between.
    std::array<GtkWidget*, 2> trafficLines_{}, dividers_{};
    std::array<Row, statisticsFields.size()> rows_{};
    StatisticsModel model_;
    guint durationTimer_ = 0, placement_ = 0;
    int layoutWidth_ = -1;
    bool narrow_ = false, headerWrapped_ = false;
};

// Creates the window and connects TrayWindowHooks::openStatistics, the status
// listener and the session end.
void installStatisticsWindow(TrayApp& app);
}  // namespace phpvpn::tray
