#include "server_info_presenter.h"

#include <ctime>

#include <algorithm>

#include "core/config/config_document.h"
#include "ipc_timeouts.h"

namespace phpvpn::tray {
std::string serverInfoLocalTime(std::int64_t unixMs) {
    const std::int64_t seconds = unixMs >= 0 ? unixMs / 1000 : -((-unixMs + 999) / 1000);
    const auto value = static_cast<time_t>(seconds);
    if (static_cast<std::int64_t>(value) != seconds) return {};
    std::tm local{};
    if (!localtime_r(&value, &local)) return {};
    char date[128]{}, timeText[128]{};
    if (!std::strftime(date, sizeof(date), "%x", &local) || !std::strftime(timeText, sizeof(timeText), "%X", &local)) return {};
    std::string text = std::string(date) + " " + timeText;
    if (isStrictUtf8(text)) return text;
    // Labels must be UTF-8; a legacy locale charset falls back to a neutral form.
    if (!std::strftime(date, sizeof(date), "%Y-%m-%d %H:%M:%S", &local)) return {};
    return date;
}

std::vector<ServerInfoPagerButton> serverInfoPager(int count, int current) {
    std::vector<ServerInfoPagerButton> buttons;
    if (count <= 1) return buttons;
    current = std::clamp(current, 0, count - 1);
    auto add = [&](std::string label, int page, bool enabled, std::string name) {
        if (name.empty() && page >= 0)
            name = "Page " + std::to_string(page + 1) + " of " + std::to_string(count) +
                   (page == current ? ", current page" : "");
        // As on Windows the primary role follows page == current for every button;
        // an arrow can only match while it is disabled.
        buttons.push_back({std::move(label), page, enabled, page == current, std::move(name)});
    };
    add("«", 0, current > 0, "First page");
    add("‹", std::max(0, current - 1), current > 0, "Previous page");
    int previous = -1;
    for (int page : tablePageNumbers(count, current)) {
        if (page > previous + 1) add("…", -1, false, "");
        add(std::to_string(page + 1), page, true, "");
        previous = page;
    }
    add("›", std::min(count - 1, current + 1), current < count - 1, "Next page");
    add("»", count - 1, current < count - 1, "Last page");
    return buttons;
}

std::optional<std::uint64_t> ServerInfoSession::begin() {
    if (pending_) return std::nullopt;
    pending_ = true;
    autoDeadline_.reset();
    requestGeneration_ = generation_;
    return generation_;
}

ServerInfoSession::NotSent ServerInfoSession::notSent(std::int64_t nowMs) {
    pending_ = false;
    NotSent result{kServerInfoBusyRefresh, std::nullopt};
    if (!model_) result.fatal = kServerInfoBusyOpen;
    schedule(nowMs);
    return result;
}

ServerInfoSession::Finished ServerInfoSession::finish(std::uint64_t token, std::string_view response,
                                                      std::optional<std::size_t> selectedRow, std::int64_t nowMs) {
    Finished result;
    if (!pending_ || token != generation_ || requestGeneration_ != generation_) return result;
    pending_ = false;
    schedule(nowMs);
    auto snapshot = parseServerInfoResult(response);
    if (!snapshot) {
        result.outcome = model_ ? Outcome::retained : Outcome::fatal;
        result.text = model_ ? kServerInfoRefreshFailed : kServerInfoUnverified;
        return result;
    }
    auto model = buildServerInfoModel(std::move(snapshot).value());
    if (!model) {
        result.outcome = model_ ? Outcome::retained : Outcome::fatal;
        result.text = model_ ? kServerInfoRefreshFailed : kServerInfoModelUnverified;
        return result;
    }
    std::optional<Ipv4Address> selected;
    if (model_ && selectedRow) {
        const auto index = static_cast<std::size_t>(page_) * kTablePageSize + *selectedRow;
        if (index < model_->rows.size()) selected = model_->rows[index].peer;
    }
    result.outcome = model_ ? Outcome::replaced : Outcome::first;
    result.rawChanged = !model_ || model_->snapshot.infoPlaintext != model->snapshot.infoPlaintext;
    model_ = std::move(model).value();
    page_ = std::clamp(page_, 0, pageCount() - 1);
    if (selected)
        for (std::size_t i = 0; i < model_->rows.size(); ++i)
            if (model_->rows[i].peer == *selected) {
                page_ = static_cast<int>(i / kTablePageSize);
                result.selectedRow = i % kTablePageSize;
                break;
            }
    return result;
}

void ServerInfoSession::invalidate() {
    ++generation_;
    pending_ = false;
    stopAutoRefresh();
}

void ServerInfoSession::reset() {
    invalidate();
    model_.reset();
    page_ = 0;
}

void ServerInfoSession::setAutoRefresh(bool enabled, std::int64_t nowMs) {
    autoRefresh_ = enabled;
    schedule(nowMs);
}

void ServerInfoSession::stopAutoRefresh() {
    autoRefresh_ = false;
    autoDeadline_.reset();
}

bool ServerInfoSession::autoDue(std::int64_t nowMs) {
    // An early or stale tick is not a deadline (Windows checks GetTickCount64).
    if (!autoDeadline_ || nowMs < *autoDeadline_) return false;
    autoDeadline_.reset();
    return autoRefresh_ && !pending_;
}

void ServerInfoSession::schedule(std::int64_t nowMs) {
    autoDeadline_.reset();
    if (!autoRefresh_ || pending_) return;
    autoDeadline_ = nowMs + static_cast<std::int64_t>(kServerInfoAutoRefreshMs);
}

std::string ServerInfoSession::countText() const { return model_ ? serverInfoCountText(*model_) : std::string{}; }

std::string ServerInfoSession::title() const {
    return model_ ? std::string(kServerInfoTitle) + " " + countText() : std::string(kServerInfoTitle);
}

int ServerInfoSession::pageCount() const { return tablePageCount(model_ ? model_->rows.size() : 0); }

void ServerInfoSession::setPage(int page) {
    if (!model_) return;
    page_ = std::clamp(page, 0, pageCount() - 1);
}

std::vector<ServerInfoTableRow> ServerInfoSession::pageRows() const {
    std::vector<ServerInfoTableRow> rows;
    if (!model_) return rows;
    const auto start = static_cast<std::size_t>(page_) * kTablePageSize;
    const auto end = std::min(model_->rows.size(), start + kTablePageSize);
    for (auto i = start; i < end; ++i) {
        const auto& row = model_->rows[i];
        rows.push_back({row.contract, serverInfoLocalTime(row.lastSeenMs), serverClientStateText(row.state), row.state});
    }
    return rows;
}
}  // namespace phpvpn::tray
