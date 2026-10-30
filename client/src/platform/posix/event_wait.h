// Portable readiness waiting for the POSIX platforms, plan-macos-v001 step 2.
//
// Notifier is the replacement of a Linux eventfd: a descriptor that becomes
// readable when signalled and stays readable until drained. Linux uses an
// eventfd, macOS a non-blocking close-on-exec pipe.
//
// Poller is the level triggered readiness wait of the IPC server loop: fds are
// registered with a 64-bit tag and a read and/or write interest, an optional
// periodic tick fires at once and then every period. Linux uses epoll and a
// timerfd, macOS kqueue with EVFILT_READ, EVFILT_WRITE and EVFILT_TIMER.
// A Poller is used by one thread at a time.
#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>

#include "core/base/result.h"
#include "platform/posix/fd.h"

namespace phpvpn {

class Notifier {
public:
    // An inert notifier: fd() is -1, signal() and drain() do nothing.
    Notifier() noexcept = default;
    Notifier(Notifier&&) noexcept = default;
    Notifier& operator=(Notifier&&) noexcept = default;
    Notifier(const Notifier&) = delete;
    Notifier& operator=(const Notifier&) = delete;

    static Result<Notifier> create();

    // Readable while signalled. Suitable for poll() and for Poller.
    int fd() const noexcept { return readable_.get(); }
    // Thread safe. Repeated signals before a drain coalesce.
    void signal() noexcept;
    // Makes fd() unreadable again until the next signal.
    void drain() noexcept;

private:
    UniqueFd readable_;  // Linux: the eventfd itself; macOS: the read end of the pipe
#if defined(__APPLE__)
    UniqueFd writable_;  // the write end of the pipe
#endif
};

// Interest (add, modify) and result (PollEvent::flags) bits.
inline constexpr std::uint32_t kPollReadable = 1u << 0;
inline constexpr std::uint32_t kPollWritable = 1u << 1;
inline constexpr std::uint32_t kPollError = 1u << 2;   // result only
inline constexpr std::uint32_t kPollHangup = 1u << 3;  // result only; macOS: EV_EOF of the filter

// Reserved for the internal tick; never use it as a registration tag.
inline constexpr std::uint64_t kPollReservedTag = std::numeric_limits<std::uint64_t>::max();

struct PollEvent {
    std::uint64_t tag = 0;
    std::uint32_t flags = 0;
};

struct PollWait {
    std::size_t count = 0;  // events written to the front of the output span
    bool tick = false;      // the periodic tick fired since the previous wait
    int error = 0;          // errno of a failed wait (EINTR: just wait again), 0 on success
};

class Poller {
public:
    // An inert poller; every registration fails.
    Poller() noexcept = default;
    Poller(Poller&&) noexcept = default;
    Poller& operator=(Poller&&) noexcept = default;
    Poller(const Poller&) = delete;
    Poller& operator=(const Poller&) = delete;

    static Result<Poller> create();

    // Registers fd with the interest bits (kPollReadable and/or kPollWritable).
    bool add(int fd, std::uint64_t tag, std::uint32_t interest) noexcept;
    // Replaces the tag and the interest of a registered fd.
    bool modify(int fd, std::uint64_t tag, std::uint32_t interest) noexcept;
    // Unregisters fd; closing an fd unregisters it as well.
    void remove(int fd) noexcept;
    // Starts the periodic tick: the first wait reports it at once, then it
    // fires every periodMs.
    bool startTick(std::int64_t periodMs) noexcept;
    // Waits up to timeoutMs (negative: without limit) for readiness or the
    // tick. Each ready filter is one event; at most 64 events per call, the
    // rest is reported by the next wait (level triggered).
    PollWait wait(std::span<PollEvent> out, std::int64_t timeoutMs) noexcept;

private:
    UniqueFd queue_;  // the epoll or kqueue descriptor
#if defined(__APPLE__)
    bool tickPending_ = false;  // the first tick is reported by the next wait
#else
    UniqueFd timer_;            // timerfd of the periodic tick
#endif
};

}  // namespace phpvpn
