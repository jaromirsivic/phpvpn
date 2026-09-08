#pragma once
#include "status_model.h"
#include "ui_style.h"

namespace phpvpn::tray {
struct StatisticsField {
    const wchar_t *label;
    std::size_t counterIndex;
    bool bytes;
};
// Visual/tab order is 3 / 2 / (sent packets, sent bytes, received packets, received bytes).
inline constexpr std::array<StatisticsField, 9> statisticsFields{{{L"Total polls", 0, false},
                                                                  {L"Empty polls", 1, false},
                                                                  {L"Polls with payload", 2, false},
                                                                  {L"Request body", 3, true},
                                                                  {L"Response body", 4, true},
                                                                  {L"Packets sent", 5, false},
                                                                  {L"Bytes sent", 7, true},
                                                                  {L"Packets received", 6, false},
                                                                  {L"Bytes received", 8, true}}};
class StatisticsModel {
  public:
    void update(const TrayStatus &snapshot, bool unavailable) {
        if (snapshot.valid) {
            counters_ = snapshot.counters;
            valid_ = true;
        }
        stale_ = unavailable || !snapshot.valid;
    }
    std::wstring value(std::size_t index) const {
        if (!valid_)
            return L"-";
        const auto &field = statisticsFields[index];
        return ui::englishInteger(counters_.*kCounterFields[field.counterIndex].member) + (field.bytes ? L" B" : L"");
    }
    const wchar_t *freshness() const {
        return !valid_  ? L"Waiting for service status"
               : stale_ ? L"Service status unavailable. Showing the last received values."
                        : L"";
    }
    bool valid() const { return valid_; }
    bool stale() const { return stale_; }

  private:
    TrafficCountersView counters_;
    bool valid_ = false, stale_ = false;
};
} // namespace phpvpn::tray
