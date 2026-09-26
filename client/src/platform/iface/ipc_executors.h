// Bounded work-class executors shared by the platform IPC servers.
//
// Three single-thread executors run completed requests and posted tasks:
// shortControl (16 waiting), file (4 waiting) and diagnostic (1 outstanding in
// total). A request that finds its queue full receives the correlated busy
// response. At shutdown admission closes first, cancelHandlers runs once and
// every accepted request or task is drained, never silently dropped.
#pragma once
#include <array>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include "platform/iface/ipc_server.h"

namespace phpvpn {
// One complete request moving from the event loop to an executor and back.
struct IpcJob {
    std::string request;
    std::string response;
    // Release-published by the executor after response/closeOnly are final.
    std::atomic<bool> done{false};
    bool closeOnly = false;
};
enum class IpcAdmission {
    queued,  // The job runs on an executor; wait for done.
    respond, // Not queued; job->response holds the busy response to write now.
    close,   // Not queued and no response; close the connection.
};
class IpcExecutors {
public:
    // maxResponseCapacity bounds the string capacity of handler and busy responses,
    // so a transport can keep a fixed per-connection reservation. onJobDone wakes
    // the event loop after a request job completed; it runs on executor threads.
    IpcExecutors(IpcHandler handler, IpcServerOptions hooks, std::size_t maxResponseCapacity,
                 std::function<void()> onJobDone);
    ~IpcExecutors();
    IpcExecutors(const IpcExecutors&) = delete;
    IpcExecutors& operator=(const IpcExecutors&) = delete;

    bool hasHandler() const { return static_cast<bool>(handler_); }
    // Opens admission and starts the three threads. May throw std::system_error;
    // the caller then runs shutdown(false) and join().
    void start();
    // Rejects every later submission. Safe to call more than once.
    void closeAdmission() noexcept;
    // Closes admission, runs cancelHandlers once when requested and lets the
    // executors finish their queues.
    void shutdown(bool cancelHandlers);
    // Waits until every accepted job and task has run.
    void join();

    // Classifies a complete request and queues it, or prepares the busy response.
    IpcAdmission admit(const std::shared_ptr<IpcJob>& job);
    bool tryPost(IpcWorkClass workClass, std::function<void()> task);
    // Counts a request rejected by the transport itself (for example oversized).
    void countRejected() noexcept { rejected_.fetch_add(1); }
    // Fills the queue fields and rejected; transports fill the connection fields.
    void fillStats(IpcServerStats& stats) const;

private:
    struct Work {
        std::shared_ptr<IpcJob> job;
        std::function<void()> task;
    };
    struct Executor {
        mutable std::mutex mutex;
        std::condition_variable ready;
        std::deque<Work> queue;
        bool active = false;
        bool stopping = false;
        std::thread thread;
    };
    bool submit(std::size_t index, Work work);
    void run(std::size_t index);

    IpcHandler handler_;
    IpcServerOptions hooks_;
    std::size_t maxResponseCapacity_;
    std::function<void()> onJobDone_;
    std::atomic<bool> accepting_{false};
    std::array<Executor, 3> executors_;
    std::atomic<std::uint64_t> rejected_{0};
};
}  // namespace phpvpn
