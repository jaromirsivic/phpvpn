// In-process IPC server of the Android client (plan-android-v001 point 8).
//
// UI and service share one process, so there is no socket, no pipe and no
// name another process could open: a request reaches the runtime only through
// submit() from the JNI entry (nativeDispatch). Everything else is the desktop
// contract: the same IpcExecutors with the three work classes (shortControl 16
// waiting, file 4, diagnostic 1 outstanding), the same classifier and
// correlated busy response, onTick every 250 ms on its own thread and the
// shutdown order of posix/ipc_unix.cpp.
//
// A caller waits for its job with its own timeout. A timeout returns no reply
// and leaves the job to finish on its executor, like a desktop client that gave
// up and closed its connection; nobody waits for that job afterwards.
#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "platform/iface/ipc_executors.h"
#include "platform/iface/ipc_server.h"

namespace phpvpn {

// The shared part of the server. The JNI entry keeps its own reference, so a
// dispatch racing with nativeStop never touches a destroyed object; after
// stop() every submit is refused.
class InProcessIpcEndpoint {
public:
    InProcessIpcEndpoint(IpcHandler handler, IpcServerOptions options);
    ~InProcessIpcEndpoint();
    InProcessIpcEndpoint(const InProcessIpcEndpoint&) = delete;
    InProcessIpcEndpoint& operator=(const InProcessIpcEndpoint&) = delete;

    Status start();
    // Ordered shutdown, idempotent: admission closes, cancelHandlers runs once,
    // the tick thread ends and every accepted job and task is drained.
    void stop();
    bool running() const noexcept { return running_.load(std::memory_order_acquire); }

    // Hands one complete request to the executors. Null means the request was
    // refused without a reply (not running, empty, oversized or not
    // classifiable): the counterpart of a closed desktop connection. A job
    // whose `done` is already set carries the busy response.
    std::shared_ptr<IpcJob> submit(std::string request);
    // Waits until the job is done or the timeout passed. True when
    // job->response holds the reply; false on a timeout and for a job the
    // executor closed (handler failure, oversized reply). A timed out job
    // keeps running on its executor. The job wipes its request and response
    // when its last owner releases it.
    bool wait(const std::shared_ptr<IpcJob>& job, std::chrono::milliseconds timeout);
    // A request the JNI entry refused before copying it (kIpcMaxMessageBytes).
    void countRejected() noexcept { executors_.countRejected(); }

    bool tryPostFile(std::function<void()> task);
    bool tryPostDiagnostic(std::function<void()> task);
    // connectionSlots counts the callers waiting right now, ioTimeouts the
    // waits that ended by the caller's timeout.
    IpcServerStats stats() const;

private:
    void runTicks();
    void jobDone();

    IpcServerOptions options_;
    IpcExecutors executors_;
    std::mutex lifecycle_;
    std::atomic<bool> running_{false};
    bool started_ = false;
    // Completion of any job wakes every waiter; each checks its own job.
    std::mutex doneMutex_;
    std::condition_variable doneSignal_;
    // The tick thread sleeps on this pair, so stop() wakes it at once.
    std::mutex tickMutex_;
    std::condition_variable tickSignal_;
    bool tickStopping_ = false;
    std::thread ticker_;
    std::atomic<std::size_t> waiting_{0};
    std::atomic<std::uint64_t> timeouts_{0};
};

// The IpcServer owned by ServiceRuntime; the endpoint outlives it as long as
// the JNI entry holds a reference.
class InProcessIpcServer final : public IpcServer {
public:
    explicit InProcessIpcServer(std::shared_ptr<InProcessIpcEndpoint> endpoint) : endpoint_(std::move(endpoint)) {}
    ~InProcessIpcServer() override { endpoint_->stop(); }
    InProcessIpcServer(const InProcessIpcServer&) = delete;
    InProcessIpcServer& operator=(const InProcessIpcServer&) = delete;

    Status start() override { return endpoint_->start(); }
    void stop() override { endpoint_->stop(); }
    bool tryPostFile(std::function<void()> task) override { return endpoint_->tryPostFile(std::move(task)); }
    bool tryPostDiagnostic(std::function<void()> task) override {
        return endpoint_->tryPostDiagnostic(std::move(task));
    }
    IpcServerStats stats() const override { return endpoint_->stats(); }

private:
    std::shared_ptr<InProcessIpcEndpoint> endpoint_;
};

}  // namespace phpvpn
