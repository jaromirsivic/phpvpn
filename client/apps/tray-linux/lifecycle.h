// GTK-free lifecycle contract between the tray application and the editing windows
// (Wizard, Edit Config, Communication contracts; WP10), and the common Exit
// coordinator of tray_UX.md UX03 / client.md "Spolecny Exit a systemovy konec relace".
#pragma once
#include <cstdint>
#include <functional>
#include <optional>
#include <string_view>
#include <vector>

namespace phpvpn::tray {
// Snapshot of one editing window. generation changes with every draft or window
// identity change, so an approval applies to exactly the confirmed state.
struct LifecycleSnapshot {
    bool open = false;
    bool dirty = false;
    // A save or commit whose result is not known (the file may already have changed).
    bool unknown = false;
    // A save or commit is running; Exit waits for its original deadline.
    bool busy = false;
    std::uint64_t generation = 0;
    bool operator==(const LifecycleSnapshot&) const = default;
};

class LifecycleParticipant {
public:
    virtual ~LifecycleParticipant() = default;
    virtual LifecycleSnapshot lifecycleSnapshot() const = 0;
    // Discards exactly the approved state without asking again. False is a temporary
    // veto (for example a confirmed Finish still being presented); Exit retries.
    virtual bool approvedDiscard(const LifecycleSnapshot& approved) = 0;
    // Terminal and non-blocking: revoke deliveries, wipe owned buffers, destroy windows.
    virtual void endSession() = 0;
};

inline constexpr std::string_view kExitTitle = "Exit PHPVPN";
inline constexpr std::string_view kDiscardQuestion = "Discard unsaved changes?";
inline constexpr std::string_view kUnknownQuestion =
    "The file may already have changed. Close without checking the save result?";
inline constexpr std::string_view kDiscardAndUnknownQuestion =
    "Discard unsaved changes?\nThe file may already have changed. Close without checking the save result?";
inline std::optional<std::string_view> exitQuestion(bool dirty, bool unknown) {
    if (dirty && unknown) return kDiscardAndUnknownQuestion;
    if (unknown) return kUnknownQuestion;
    if (dirty) return kDiscardQuestion;
    return std::nullopt;
}

// Port of TrayApplication::advanceExit. The application calls advance() from a
// short timer until it returns finished or cancelled. confirm shows the single
// shared Yes/No question (default No) and may re-enter the main loop.
class ExitCoordinator {
public:
    enum class Step { idle, wait, cancelled, finished };
    using Confirm = std::function<bool(std::string_view question)>;
    void begin() { active_ = true; approval_.reset(); }
    void cancel() { active_ = false; approval_.reset(); }
    bool active() const { return active_; }
    bool confirming() const { return confirming_; }
    // participants returns the currently registered windows. It is queried again
    // after the question, because the nested loop may register or remove windows.
    using Participants = std::function<std::vector<LifecycleParticipant*>()>;
    Step advance(const Participants& current, const Confirm& confirm) {
        if (!active_) return Step::idle;
        if (confirming_) return Step::wait;
        auto participants = current();
        std::vector<LifecycleSnapshot> snapshots;
        for (auto* participant : participants) snapshots.push_back(participant->lifecycleSnapshot());
        if (approval_ && !matchesApproval(snapshots)) approval_.reset();
        bool dirty = false, unknown = false;
        for (const auto& snapshot : snapshots) {
            // Each participant consumes its own original deadline; nothing is extended.
            if (snapshot.busy) return Step::wait;
            dirty = dirty || snapshot.dirty;
            unknown = unknown || snapshot.unknown;
        }
        if (const auto question = exitQuestion(dirty, unknown); question && !approval_) {
            confirming_ = true;
            const bool approved = confirm(*question);
            confirming_ = false;
            if (!active_) return Step::cancelled;
            if (!approved) { cancel(); return Step::cancelled; }
            approval_ = snapshots;
        }
        // Modal re-entry may have edited, saved, closed or recreated any participant.
        if (current() != participants) return Step::wait;
        for (std::size_t i = 0; i < participants.size(); ++i)
            if (participants[i]->lifecycleSnapshot() != snapshots[i]) return Step::wait;
        for (std::size_t i = 0; i < participants.size(); ++i)
            if (snapshots[i].open && !participants[i]->approvedDiscard(snapshots[i])) return Step::wait;
        active_ = false;
        approval_.reset();
        return Step::finished;
    }
private:
    bool matchesApproval(const std::vector<LifecycleSnapshot>& snapshots) const {
        if (approval_->size() != snapshots.size()) return false;
        for (std::size_t i = 0; i < snapshots.size(); ++i)
            if (snapshots[i].open && snapshots[i] != (*approval_)[i]) return false;
        return true;
    }
    bool active_ = false;
    bool confirming_ = false;
    std::optional<std::vector<LifecycleSnapshot>> approval_;
};
}  // namespace phpvpn::tray
