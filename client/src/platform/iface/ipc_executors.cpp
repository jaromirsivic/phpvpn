#include "platform/iface/ipc_executors.h"
#include <utility>
#include "core/ipc/ipc_protocol.h"

namespace phpvpn {
namespace {
constexpr std::size_t kShortControl = 0;
constexpr std::size_t kFile = 1;
constexpr std::size_t kDiagnostic = 2;
std::size_t indexOf(IpcWorkClass workClass) {
    switch (workClass) {
    case IpcWorkClass::shortControl: return kShortControl;
    case IpcWorkClass::file: return kFile;
    case IpcWorkClass::diagnostic: return kDiagnostic;
    }
    return kShortControl + 3; // Out of range: not a work class.
}
}

IpcExecutors::IpcExecutors(IpcHandler handler, IpcServerOptions hooks, std::size_t maxResponseCapacity,
                           std::function<void()> onJobDone)
    : handler_(std::move(handler)), hooks_(std::move(hooks)),
      maxResponseCapacity_(maxResponseCapacity), onJobDone_(std::move(onJobDone)) {}

IpcExecutors::~IpcExecutors() {
    shutdown(false);
    join();
}

void IpcExecutors::start() {
    for (auto& executor : executors_) {
        std::lock_guard lock(executor.mutex);
        executor.stopping = false;
    }
    accepting_.store(true, std::memory_order_release);
    for (std::size_t i = 0; i < executors_.size(); ++i) {
        executors_[i].thread = std::thread([this, i] { run(i); });
    }
}

void IpcExecutors::closeAdmission() noexcept {
    accepting_.store(false, std::memory_order_release);
}

void IpcExecutors::shutdown(bool cancelHandlers) {
    closeAdmission();
    if (cancelHandlers && hooks_.cancelHandlers) { try { hooks_.cancelHandlers(); } catch (...) {} }
    for (auto& executor : executors_) {
        {
            std::lock_guard lock(executor.mutex);
            executor.stopping = true;
        }
        executor.ready.notify_all();
    }
}

void IpcExecutors::join() {
    // Drain all accepted callbacks after cancellation. In particular, an
    // admitted Finish task must publish its terminal result instead of vanishing.
    for (auto& executor : executors_) if (executor.thread.joinable()) executor.thread.join();
}

bool IpcExecutors::submit(std::size_t index, Work work) {
    if (!accepting_.load(std::memory_order_acquire)) return false;
    auto& executor = executors_[index];
    std::lock_guard lock(executor.mutex);
    if (executor.stopping || !accepting_.load(std::memory_order_acquire)) return false;
    const std::size_t limit = index == kShortControl ? 16 : index == kFile ? 4 : 1;
    const std::size_t used = executor.queue.size() + (index == kDiagnostic && executor.active ? 1 : 0);
    if (used >= limit) { rejected_.fetch_add(1); return false; }
    executor.queue.push_back(std::move(work));
    executor.ready.notify_one();
    return true;
}

void IpcExecutors::run(std::size_t index) {
    auto& executor = executors_[index];
    for (;;) {
        Work work;
        {
            std::unique_lock lock(executor.mutex);
            executor.ready.wait(lock, [&] { return executor.stopping || !executor.queue.empty(); });
            if (executor.queue.empty() && executor.stopping) return;
            work = std::move(executor.queue.front());
            executor.queue.pop_front();
            executor.active = true;
        }
        try {
            if (work.task) work.task();
            else if (work.job) {
                work.job->response = handler_(work.job->request);
                // Capacity is checked too: a shrunk oversized allocation must
                // not exceed the connection's 4 MiB response reservation.
                if (work.job->response.size() > kIpcMaxMessageBytes ||
                    work.job->response.capacity() > maxResponseCapacity_) {
                    std::string{}.swap(work.job->response);
                    work.job->closeOnly = true;
                }
            }
        } catch (...) {
            // Handler exceptions never unwind the event loop or another job.
            if (work.job) work.job->closeOnly = true;
        }
        {
            std::lock_guard lock(executor.mutex);
            executor.active = false;
        }
        if (work.job) {
            work.job->done.store(true, std::memory_order_release);
            if (onJobDone_) onJobDone_();
        }
    }
}

IpcAdmission IpcExecutors::admit(const std::shared_ptr<IpcJob>& job) {
    try {
        std::size_t index = kShortControl;
        if (hooks_.classify) {
            index = indexOf(hooks_.classify(job->request));
            if (index >= executors_.size()) return IpcAdmission::close;
        }
        if (submit(index, Work{job, {}})) return IpcAdmission::queued;
        if (!hooks_.busyResponse) return IpcAdmission::close;
        job->response = hooks_.busyResponse(job->request);
        if (job->response.empty() || job->response.size() > kIpcMaxMessageBytes ||
            job->response.capacity() > maxResponseCapacity_) return IpcAdmission::close;
        return IpcAdmission::respond;
    } catch (...) {
        return IpcAdmission::close;
    }
}

bool IpcExecutors::tryPost(IpcWorkClass workClass, std::function<void()> task) {
    const std::size_t index = indexOf(workClass);
    return task && index < executors_.size() && submit(index, Work{{}, std::move(task)});
}

void IpcExecutors::fillStats(IpcServerStats& stats) const {
    { std::lock_guard lock(executors_[kShortControl].mutex); stats.shortQueued = executors_[kShortControl].queue.size(); }
    { std::lock_guard lock(executors_[kFile].mutex); stats.fileQueued = executors_[kFile].queue.size(); }
    { std::lock_guard lock(executors_[kDiagnostic].mutex);
      stats.diagnosticOutstanding = executors_[kDiagnostic].queue.size() + (executors_[kDiagnostic].active ? 1 : 0); }
    stats.rejected = rejected_.load();
}
}  // namespace phpvpn
