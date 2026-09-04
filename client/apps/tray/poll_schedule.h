#pragma once
#include <cstdint>
#include <cstddef>

namespace phpvpn::tray {
// Queue all explicit extra reads even when a periodic read is still in flight.
// StatusReader protects this pure accounting model with its mutex.
class ReadRequestQueue {
public:
    bool request() {
        if (running_) { ++pending_; return false; }
        running_ = true;
        return true;
    }
    bool finish() {
        if (pending_ != 0) { --pending_; return true; }
        running_ = false;
        return false;
    }
    void stop() { pending_ = 0; running_ = false; }
private:
    std::size_t pending_ = 0;
    bool running_ = false;
};
// Each window owns one schedule. Extra reads never reset its fixed deadlines.
class PollSchedule {
public:
    explicit PollSchedule(std::int64_t intervalMs) : intervalMs_(intervalMs) {}
    void start(std::int64_t nowMs) { active_ = true; nextMs_ = nowMs + intervalMs_; }
    void stop() { active_ = false; }
    bool onTimer(std::int64_t nowMs) {
        if (!active_ || nowMs < nextMs_) return false;
        nextMs_ += ((nowMs - nextMs_) / intervalMs_ + 1) * intervalMs_;
        return true;
    }
    std::int64_t nextMs() const { return nextMs_; }
    bool active() const { return active_; }
private:
    std::int64_t intervalMs_;
    std::int64_t nextMs_ = 0;
    bool active_ = false;
};
}  // namespace phpvpn::tray
