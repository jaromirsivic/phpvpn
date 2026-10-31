#include "inprocess_ipc_server.h"

#include <limits>
#include <utility>

#include "core/base/secure_buffer.h"
#include "core/ipc/ipc_protocol.h"

namespace phpvpn {
namespace {

// Same cadence as the event loop of ipc_unix.cpp (kTickMs).
constexpr std::chrono::milliseconds kTickInterval{250};

// set-config and get-config carry the whole configuration with its secrets.
// The last owner of a job, the waiting caller or the executor that finished
// it after the caller timed out, wipes both texts.
struct WipingJobDeleter {
    void operator()(IpcJob* job) const noexcept {
        secureWipe(job->request.data(), job->request.size());
        secureWipe(job->response.data(), job->response.size());
        delete job;
    }
};

}  // namespace

InProcessIpcEndpoint::InProcessIpcEndpoint(IpcHandler handler, IpcServerOptions options)
    : options_(options),
      // No fixed per-caller reservation exists, so only the size bound applies
      // (as in ipc_unix.cpp).
      executors_(std::move(handler), std::move(options), std::numeric_limits<std::size_t>::max(),
                 [this] { jobDone(); }) {}

InProcessIpcEndpoint::~InProcessIpcEndpoint() { stop(); }

Status InProcessIpcEndpoint::start() {
    std::lock_guard lock(lifecycle_);
    if (running_.load()) return Error(ErrorCode::service_already_running);
    // One life per endpoint: the runtime creates a new server for every start.
    if (started_) return Error(ErrorCode::state_conflict, "the in-process IPC server cannot be restarted");
    if (!executors_.hasHandler()) return Error(ErrorCode::invalid_argument);
    started_ = true;
    try {
        {
            std::lock_guard tick(tickMutex_);
            tickStopping_ = false;
        }
        running_.store(true, std::memory_order_release);
        executors_.start();
        ticker_ = std::thread([this] { runTicks(); });
        return Status::success();
    } catch (...) {
        // std::system_error from a thread start: undo what was started below.
    }
    running_.store(false, std::memory_order_release);
    executors_.closeAdmission();
    {
        std::lock_guard tick(tickMutex_);
        tickStopping_ = true;
    }
    tickSignal_.notify_all();
    executors_.shutdown(false);
    if (ticker_.joinable()) ticker_.join();
    executors_.join();
    return Error(ErrorCode::ipc_unavailable);
}

void InProcessIpcEndpoint::stop() {
    std::lock_guard lock(lifecycle_);
    // The order of IpcSocketServer::stopLocked: admission closes first, the
    // maintenance thread is told to end, cancelHandlers runs once, then the
    // executors drain every accepted job and task.
    const bool wasRunning = running_.exchange(false, std::memory_order_acq_rel);
    executors_.closeAdmission();
    {
        std::lock_guard tick(tickMutex_);
        tickStopping_ = true;
    }
    tickSignal_.notify_all();
    executors_.shutdown(wasRunning);
    if (ticker_.joinable()) ticker_.join();
    executors_.join();
    // Every accepted job is done now; wake anyone still waiting for one.
    jobDone();
}

std::shared_ptr<IpcJob> InProcessIpcEndpoint::submit(std::string request) {
    // A refused request may still hold a configuration document.
    if (!running() || request.empty() || request.size() > kIpcMaxMessageBytes) {
        if (request.size() > kIpcMaxMessageBytes) executors_.countRejected();
        secureWipe(request.data(), request.size());
        return nullptr;
    }
    std::shared_ptr<IpcJob> job(new IpcJob, WipingJobDeleter{});
    job->request = std::move(request);
    switch (executors_.admit(job)) {
    case IpcAdmission::queued:
        return job;
    case IpcAdmission::respond:
        // The correlated busy response is final; no executor touches the job.
        job->done.store(true, std::memory_order_release);
        return job;
    case IpcAdmission::close:
        break;
    }
    return nullptr;
}

bool InProcessIpcEndpoint::wait(const std::shared_ptr<IpcJob>& job, std::chrono::milliseconds timeout) {
    if (!job) return false;
    waiting_.fetch_add(1);
    bool done = false;
    {
        std::unique_lock lock(doneMutex_);
        done = doneSignal_.wait_for(lock, timeout < std::chrono::milliseconds::zero() ? std::chrono::milliseconds::zero() : timeout,
                                    [&] { return job->done.load(std::memory_order_acquire); });
    }
    waiting_.fetch_sub(1);
    if (!done) {
        timeouts_.fetch_add(1);
        return false;
    }
    return !job->closeOnly;
}

void InProcessIpcEndpoint::jobDone() {
    // Taking the mutex orders the release of `done` before a waiter's check,
    // so no wakeup is lost between its predicate test and its sleep.
    { std::lock_guard lock(doneMutex_); }
    doneSignal_.notify_all();
}

void InProcessIpcEndpoint::runTicks() {
    // First maintenance immediately, then every 250 ms (Wizard leases and the
    // startup Connect, which Android never arms, plan step 4).
    std::unique_lock lock(tickMutex_);
    while (!tickStopping_) {
        lock.unlock();
        if (options_.onTick) {
            try {
                options_.onTick();
            } catch (...) {
            }
        }
        lock.lock();
        tickSignal_.wait_for(lock, kTickInterval, [this] { return tickStopping_; });
    }
}

bool InProcessIpcEndpoint::tryPostFile(std::function<void()> task) {
    return executors_.tryPost(IpcWorkClass::file, std::move(task));
}

bool InProcessIpcEndpoint::tryPostDiagnostic(std::function<void()> task) {
    return executors_.tryPost(IpcWorkClass::diagnostic, std::move(task));
}

IpcServerStats InProcessIpcEndpoint::stats() const {
    IpcServerStats result;
    result.connectionSlots = waiting_.load();
    executors_.fillStats(result);
    result.ioTimeouts = timeouts_.load();
    return result;
}

}  // namespace phpvpn
