#pragma once
#include <windows.h>
#include <atomic>
#include <mutex>
#include <optional>
#include <thread>
#include <deque>
#include "core/ipc/ipc_protocol.h"
#include "status_model.h"
#include "poll_schedule.h"

namespace phpvpn::tray {
// Read-only IPC capability. No connect/disconnect operation is available to a
// progress window. One in-flight read per reader, bounded independently of UI.
class StatusReader {
public:
    ~StatusReader() { stop(); }
    void request(HWND target, UINT message);
    std::optional<TrayStatus> take();
    void stop();
private:
    std::atomic<bool> stopping_{false};
    std::thread worker_;
    std::mutex mutex_;
    std::deque<TrayStatus> results_;
    ReadRequestQueue queue_;
};

class CommandReader {
public:
    CommandReader();
    ~CommandReader();
    bool request(IpcCommand command, const std::string& payload, HWND target, UINT message);
    std::optional<std::string> take();
    void stop();
private:
    HANDLE cancel_ = nullptr;
    std::atomic<bool> busy_{false};
    std::thread worker_;
    std::mutex mutex_;
    std::optional<std::string> result_;
};
}  // namespace phpvpn::tray
