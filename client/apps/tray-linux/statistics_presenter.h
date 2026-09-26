// GTK-free presentation of the Statistics window (tray_UX.md UX07, client.md menu
// item 8): UTF-8 texts from the shared apps/tray/statistics_model.h (decimal
// units, English thousands grouping, exact uint64 and carry sums) and the layout
// decisions of apps/tray/statistics_layout.h (3/3/4 cards, 760 DIP breakpoint,
// stacked rows, separators) for the GTK containers.
#pragma once
#include <array>
#include <cstddef>
#include <string>
#include <string_view>

#include "statistics_model.h"
#include "tray_text.h"
#include "ui_style.h"

namespace phpvpn::tray {
inline constexpr std::string_view kStatisticsWindowTitle = "PHPVPN Statistics";
inline constexpr std::string_view kStatisticsHeading = "Statistics";
inline constexpr std::string_view kStatisticsDescription =
    "Read-only global service counters for the current VPN connection.";
inline constexpr std::array<std::string_view, 3> kStatisticsCardTitles{"Data polling", "Serialized data", "IPv4 traffic"};
// Placeholder width of the connection status, as the Windows layout measures it.
inline constexpr std::string_view kStatisticsDisconnected = "(Disconnected)";

struct StatisticsTexts {
    std::array<std::string, statisticsFields.size()> values;
    std::string freshness;
    std::string connectionStatus;
    bool connected = false;
};

inline std::string statisticsLabel(std::size_t index) { return utf8(statisticsFields[index].label); }

inline StatisticsTexts statisticsTexts(const StatisticsModel& model) {
    StatisticsTexts texts;
    for (std::size_t i = 0; i < statisticsFields.size(); ++i) texts.values[i] = utf8(model.value(i));
    texts.freshness = utf8(model.freshness());
    texts.connectionStatus = utf8(model.connectionStatus());
    texts.connected = model.connected();
    return texts;
}

// Card of a field in the 3 / 3 / 4 order of statisticsFields.
constexpr std::size_t statisticsCard(std::size_t index) { return index < 3 ? 0 : index < 6 ? 1 : 2; }
// Below 760 DIP the top cards stack and the IPv4 grid becomes one column.
constexpr bool statisticsNarrow(int widthDip) { return widthDip < ui::statisticsBreakpoint; }
// A long value moves under its label (computeStatisticsLayout).
constexpr bool statisticsStacked(int valueWidth, int labelWidth, int available) {
    return valueWidth + labelWidth + 16 > available;
}
struct StatisticsCell {
    int row = 0, column = 0;
};
// Position in the IPv4 traffic card: a 2 x 2 grid with sent packets and bytes in
// the top row when wide, one column in the same logical order when narrow.
constexpr StatisticsCell statisticsTrafficCell(std::size_t index, bool narrow) {
    const int offset = static_cast<int>(index) - 6;
    return narrow ? StatisticsCell{offset, 0} : StatisticsCell{offset / 2, offset % 2};
}
// Horizontal separator under a row, exactly as StatisticsWindow::paint draws them.
constexpr bool statisticsSeparatorAfter(std::size_t index, bool narrow) {
    switch (index) {
        case 0: case 1: case 3: case 4: case 6: case 7: return true;
        case 8: return narrow;
        default: return false;
    }
}
}  // namespace phpvpn::tray
