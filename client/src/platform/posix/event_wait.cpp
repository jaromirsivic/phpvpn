#include "platform/posix/event_wait.h"

#include <errno.h>
#include <fcntl.h>
#include <time.h>
#include <unistd.h>

#if defined(__APPLE__)
#include <sys/types.h>
// sys/types.h first, as the kqueue manual page orders them.
#include <sys/event.h>
#else
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/timerfd.h>
#endif

#include <algorithm>
#include <array>
#include <utility>

namespace phpvpn {
namespace {

// Events fetched per wait, the tick excluded.
constexpr std::size_t kMaxBatch = 64;

#if defined(__APPLE__)
static_assert(sizeof(std::uintptr_t) >= sizeof(std::uint64_t), "kevent udata carries the 64-bit tag");

// EVFILT_TIMER identifier of the periodic tick; timers have their own namespace.
constexpr std::uintptr_t kTickIdent = 1;

// macOS has no pipe2(), so close-on-exec and non-blocking are set afterwards.
bool closeOnExecNonBlocking(int fd) {
    if (::fcntl(fd, F_SETFD, FD_CLOEXEC) != 0) return false;
    const int flags = ::fcntl(fd, F_GETFL);
    return flags >= 0 && ::fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

// Applies one change. Returns 0 or the errno of the failed change.
int change(int queue, int fd, std::int16_t filter, std::uint16_t flags, std::uint64_t tag) noexcept {
    struct kevent event {};
    event.ident = static_cast<std::uintptr_t>(fd);
    event.filter = filter;
    event.flags = flags;
    event.udata = reinterpret_cast<void*>(static_cast<std::uintptr_t>(tag));
    const timespec immediately{};
    return ::kevent(queue, &event, 1, nullptr, 0, &immediately) == 0 ? 0 : errno;
}

// Read and write are separate kqueue filters. A wanted filter is added (EV_ADD
// also updates the tag of an existing one); for a modification an unwanted one
// is deleted, and a filter that was not registered (ENOENT) is already gone.
bool setFilter(int queue, int fd, std::int16_t filter, bool wanted, bool modification, std::uint64_t tag) noexcept {
    if (wanted) return change(queue, fd, filter, static_cast<std::uint16_t>(EV_ADD | EV_ENABLE), tag) == 0;
    if (!modification) return true;
    const int error = change(queue, fd, filter, static_cast<std::uint16_t>(EV_DELETE), tag);
    return error == 0 || error == ENOENT;
}

bool registerFd(int queue, int fd, std::uint64_t tag, std::uint32_t interest, bool modification) noexcept {
    if (queue < 0 || tag == kPollReservedTag) return false;
    return setFilter(queue, fd, static_cast<std::int16_t>(EVFILT_READ), (interest & kPollReadable) != 0,
                     modification, tag) &&
        setFilter(queue, fd, static_cast<std::int16_t>(EVFILT_WRITE), (interest & kPollWritable) != 0,
                  modification, tag);
}
#else
bool control(int queue, int operation, int fd, std::uint64_t tag, std::uint32_t interest) noexcept {
    if (tag == kPollReservedTag) return false;
    epoll_event event{};
    if ((interest & kPollReadable) != 0) event.events |= EPOLLIN;
    if ((interest & kPollWritable) != 0) event.events |= EPOLLOUT;
    event.data.u64 = tag;
    return ::epoll_ctl(queue, operation, fd, &event) == 0;
}
#endif

}  // namespace

Result<Notifier> Notifier::create() {
    Notifier notifier;
#if defined(__APPLE__)
    int ends[2] = {-1, -1};
    if (::pipe(ends) != 0) return Error(ErrorCode::internal, "the notification pipe could not be created");
    notifier.readable_.reset(ends[0]);
    notifier.writable_.reset(ends[1]);
    if (!closeOnExecNonBlocking(ends[0]) || !closeOnExecNonBlocking(ends[1]))
        return Error(ErrorCode::internal, "the notification pipe could not be configured");
#else
    notifier.readable_.reset(::eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK));
    if (!notifier.readable_) return Error(ErrorCode::internal, "the notification eventfd could not be created");
#endif
    return notifier;
}

void Notifier::signal() noexcept {
#if defined(__APPLE__)
    if (!writable_) return;
    const char byte = 1;
    if (::write(writable_.get(), &byte, 1) < 0) {}  // A full pipe is signalled already.
#else
    if (!readable_) return;
    const std::uint64_t one = 1;
    if (::write(readable_.get(), &one, sizeof(one)) < 0) {}  // A saturated counter still wakes.
#endif
}

void Notifier::drain() noexcept {
    if (!readable_) return;
#if defined(__APPLE__)
    std::array<char, 64> buffer{};
    for (;;) {
        const ssize_t count = ::read(readable_.get(), buffer.data(), buffer.size());
        if (count < 0 && errno == EINTR) continue;
        if (count < static_cast<ssize_t>(buffer.size())) return;  // Empty, closed or failed.
    }
#else
    std::uint64_t value = 0;
    if (::read(readable_.get(), &value, sizeof(value)) < 0) {}
#endif
}

Result<Poller> Poller::create() {
    Poller poller;
#if defined(__APPLE__)
    poller.queue_.reset(::kqueue());
    if (!poller.queue_ || ::fcntl(poller.queue_.get(), F_SETFD, FD_CLOEXEC) != 0)
        return Error(ErrorCode::internal, "the kqueue could not be created");
#else
    poller.queue_.reset(::epoll_create1(EPOLL_CLOEXEC));
    if (!poller.queue_) return Error(ErrorCode::internal, "the epoll instance could not be created");
#endif
    return poller;
}

bool Poller::add(int fd, std::uint64_t tag, std::uint32_t interest) noexcept {
#if defined(__APPLE__)
    return registerFd(queue_.get(), fd, tag, interest, false);
#else
    return control(queue_.get(), EPOLL_CTL_ADD, fd, tag, interest);
#endif
}

bool Poller::modify(int fd, std::uint64_t tag, std::uint32_t interest) noexcept {
#if defined(__APPLE__)
    return registerFd(queue_.get(), fd, tag, interest, true);
#else
    return control(queue_.get(), EPOLL_CTL_MOD, fd, tag, interest);
#endif
}

void Poller::remove(int fd) noexcept {
    if (!queue_) return;
#if defined(__APPLE__)
    change(queue_.get(), fd, static_cast<std::int16_t>(EVFILT_READ), static_cast<std::uint16_t>(EV_DELETE), 0);
    change(queue_.get(), fd, static_cast<std::int16_t>(EVFILT_WRITE), static_cast<std::uint16_t>(EV_DELETE), 0);
#else
    ::epoll_ctl(queue_.get(), EPOLL_CTL_DEL, fd, nullptr);
#endif
}

bool Poller::startTick(std::int64_t periodMs) noexcept {
    if (!queue_ || periodMs <= 0) return false;
#if defined(__APPLE__)
    // Without NOTE_SECONDS, NOTE_USECONDS or NOTE_NSECONDS the period is in
    // milliseconds. kqueue has no immediate first expiry, the next wait
    // reports the first tick instead.
    struct kevent timer {};
    timer.ident = kTickIdent;
    timer.filter = static_cast<std::int16_t>(EVFILT_TIMER);
    timer.flags = static_cast<std::uint16_t>(EV_ADD | EV_ENABLE);
    timer.fflags = 0;
    timer.data = static_cast<std::intptr_t>(periodMs);
    timer.udata = nullptr;
    const timespec immediately{};
    if (::kevent(queue_.get(), &timer, 1, nullptr, 0, &immediately) != 0) return false;
    tickPending_ = true;
    return true;
#else
    UniqueFd timer(::timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK));
    if (!timer) return false;
    epoll_event event{};
    event.events = EPOLLIN;
    event.data.u64 = kPollReservedTag;
    if (::epoll_ctl(queue_.get(), EPOLL_CTL_ADD, timer.get(), &event) != 0) return false;
    // First tick at once, then every period.
    itimerspec period{};
    period.it_value.tv_nsec = 1;
    period.it_interval.tv_sec = static_cast<time_t>(periodMs / 1000);
    period.it_interval.tv_nsec = static_cast<long>((periodMs % 1000) * 1000 * 1000);
    if (::timerfd_settime(timer.get(), 0, &period, nullptr) != 0) return false;
    timer_ = std::move(timer);
    return true;
#endif
}

PollWait Poller::wait(std::span<PollEvent> out, std::int64_t timeoutMs) noexcept {
    PollWait result;
    // One extra slot, so the tick never displaces a readiness event.
    const std::size_t capacity = std::min(out.size(), kMaxBatch) + 1;
#if defined(__APPLE__)
    std::array<struct kevent, kMaxBatch + 1> ready{};
    timespec limit{};
    const timespec* timeout = &limit;  // A pending first tick is reported at once.
    if (!tickPending_) {
        if (timeoutMs < 0) {
            timeout = nullptr;
        } else {
            limit.tv_sec = static_cast<time_t>(timeoutMs / 1000);
            limit.tv_nsec = static_cast<long>((timeoutMs % 1000) * 1000 * 1000);
        }
    }
    const int count = ::kevent(queue_.get(), nullptr, 0, ready.data(), static_cast<int>(capacity), timeout);
    if (count < 0) {
        result.error = errno;
        return result;
    }
    if (tickPending_) {
        tickPending_ = false;
        result.tick = true;
    }
    for (std::size_t i = 0; i < static_cast<std::size_t>(count); ++i) {
        const struct kevent& event = ready[i];
        if (event.filter == EVFILT_TIMER) {
            result.tick = true;
            continue;
        }
        if (result.count == out.size()) continue;  // Level triggered: reported again.
        std::uint32_t flags = 0;
        if ((event.flags & EV_ERROR) != 0) {
            flags |= kPollError;
        } else {
            if (event.filter == EVFILT_READ) flags |= kPollReadable;
            if (event.filter == EVFILT_WRITE) flags |= kPollWritable;
            // EV_EOF: the peer closed that direction; fflags holds a socket error.
            if ((event.flags & EV_EOF) != 0) flags |= kPollHangup;
            if ((event.flags & EV_EOF) != 0 && event.fflags != 0) flags |= kPollError;
        }
        out[result.count++] = PollEvent{static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(event.udata)), flags};
    }
#else
    std::array<epoll_event, kMaxBatch + 1> ready{};
    const int timeout = timeoutMs < 0 ? -1 :
        static_cast<int>(std::min<std::int64_t>(timeoutMs, std::numeric_limits<int>::max()));
    const int count = ::epoll_wait(queue_.get(), ready.data(), static_cast<int>(capacity), timeout);
    if (count < 0) {
        result.error = errno;
        return result;
    }
    for (std::size_t i = 0; i < static_cast<std::size_t>(count); ++i) {
        const std::uint64_t tag = ready[i].data.u64;
        const std::uint32_t events = ready[i].events;
        if (tag == kPollReservedTag) {
            std::uint64_t expirations = 0;
            if (::read(timer_.get(), &expirations, sizeof(expirations)) < 0) {}
            result.tick = true;
            continue;
        }
        if (result.count == out.size()) continue;  // Level triggered: reported again.
        std::uint32_t flags = 0;
        if ((events & EPOLLIN) != 0) flags |= kPollReadable;
        if ((events & EPOLLOUT) != 0) flags |= kPollWritable;
        if ((events & EPOLLERR) != 0) flags |= kPollError;
        if ((events & EPOLLHUP) != 0) flags |= kPollHangup;
        out[result.count++] = PollEvent{tag, flags};
    }
#endif
    return result;
}

}  // namespace phpvpn
