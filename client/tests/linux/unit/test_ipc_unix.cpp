// Linux platform tests owned by WP05 (plan-v001 section 9.4): Unix-socket IPC
// framing, deadlines, slots, shared executors, socket directory policy and the
// client's peer verification and failure classification. Shared with macOS
// (plan-macos-v001 step 2); the platform differences are guarded below.
#include "harness.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "core/ipc/ipc_protocol.h"
#include "platform/posix/event_wait.h"
#include "platform/posix/fd.h"
#include "platform/posix/ipc_unix.h"

namespace phpvpn {
namespace {
using Clock = std::chrono::steady_clock;

std::int64_t elapsedMs(Clock::time_point since) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - since).count();
}
template <class Predicate>
bool waitUntil(Predicate predicate, int limitMs = 1000) {
    const auto end = Clock::now() + std::chrono::milliseconds(limitMs);
    do {
        if (predicate()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    } while (Clock::now() < end);
    return predicate();
}
struct Gate {
    std::mutex mutex;
    std::condition_variable changed;
    bool released = false;
    void wait() {
        std::unique_lock lock(mutex);
        changed.wait(lock, [&] { return released; });
    }
    void release() {
        { std::lock_guard lock(mutex); released = true; }
        changed.notify_all();
    }
};
// Private 0700 directory owned by the test user.
class TempDir {
public:
    TempDir() {
        char pattern[] = "/tmp/phpvpn-ipc-XXXXXX";
        if (!mkdtemp(pattern)) throw std::runtime_error("mkdtemp failed");
        path_ = pattern;
    }
    ~TempDir() {
        std::error_code ignored;
        std::filesystem::permissions(path_, std::filesystem::perms::owner_all, ignored);
        std::filesystem::remove_all(path_, ignored);
    }
    TempDir(const TempDir&) = delete;
    const std::string& path() const { return path_; }
    std::string socket() const { return path_ + "/control.sock"; }

private:
    std::string path_;
};
std::string header(std::uint32_t length) {
    std::string out(4, '\0');
    out[0] = static_cast<char>(length >> 24);
    out[1] = static_cast<char>(length >> 16);
    out[2] = static_cast<char>(length >> 8);
    out[3] = static_cast<char>(length);
    return out;
}
std::string frame(std::string_view payload) {
    return header(static_cast<std::uint32_t>(payload.size())) + std::string(payload);
}
sockaddr_un address(const std::string& path) {
    sockaddr_un result{};
    result.sun_family = AF_UNIX;
    std::memcpy(result.sun_path, path.c_str(), path.size() + 1);
    return result;
}
// Close-on-exec Unix stream socket. macOS has no SOCK_CLOEXEC, SOCK_NONBLOCK or
// MSG_NOSIGNAL: the flags are set with fcntl and SIGPIPE is off per socket.
#if defined(__APPLE__)
constexpr int kTestSendFlags = 0;
bool configureTestSocket(int fd, bool nonBlocking) {
    const int one = 1;
    if (fcntl(fd, F_SETFD, FD_CLOEXEC) != 0 ||
        setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one)) != 0) return false;
    return !nonBlocking || fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK) == 0;
}
UniqueFd streamSocket(bool nonBlocking = false) {
    UniqueFd fd(socket(AF_UNIX, SOCK_STREAM, 0));
    if (fd && !configureTestSocket(fd.get(), nonBlocking)) fd.reset();
    return fd;
}
UniqueFd acceptBlocking(int listener) {
    UniqueFd fd(accept(listener, nullptr, nullptr));
    if (fd && !configureTestSocket(fd.get(), false)) fd.reset();
    return fd;
}
#else
constexpr int kTestSendFlags = MSG_NOSIGNAL;
UniqueFd streamSocket(bool nonBlocking = false) {
    return UniqueFd(socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | (nonBlocking ? static_cast<int>(SOCK_NONBLOCK) : 0), 0));
}
UniqueFd acceptBlocking(int listener) {
    return UniqueFd(accept4(listener, nullptr, nullptr, SOCK_CLOEXEC));
}
#endif
bool sendAll(int fd, std::string_view bytes) {
    std::size_t sent = 0;
    while (sent < bytes.size()) {
        const ssize_t count = send(fd, bytes.data() + sent, bytes.size() - sent, kTestSendFlags);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) return false;
        sent += static_cast<std::size_t>(count);
    }
    return true;
}
bool readExact(int fd, char* target, std::size_t size, int timeoutMs) {
    const auto start = Clock::now();
    std::size_t filled = 0;
    while (filled < size) {
        pollfd entry{fd, POLLIN, 0};
        const int left = timeoutMs - static_cast<int>(elapsedMs(start));
        if (left <= 0 || poll(&entry, 1, left) <= 0) return false;
        const ssize_t count = recv(fd, target + filled, size - filled, 0);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) return false;
        filled += static_cast<std::size_t>(count);
    }
    return true;
}
std::optional<std::string> readFrame(int fd, int timeoutMs = 2000) {
    unsigned char bytes[4]{};
    if (!readExact(fd, reinterpret_cast<char*>(bytes), 4, timeoutMs)) return std::nullopt;
    const std::uint32_t length = (std::uint32_t(bytes[0]) << 24) | (std::uint32_t(bytes[1]) << 16) |
        (std::uint32_t(bytes[2]) << 8) | std::uint32_t(bytes[3]);
    std::string payload(length, '\0');
    if (length && !readExact(fd, payload.data(), length, timeoutMs)) return std::nullopt;
    return payload;
}
// Blocking client that speaks raw bytes, for incomplete, slow and oversized frames.
class RawClient {
public:
    explicit RawClient(const std::string& path) : fd_(streamSocket()) {
        const auto target = address(path);
        if (!fd_ || connect(fd_.get(), reinterpret_cast<const sockaddr*>(&target), sizeof(target)) != 0)
            fd_.reset();
    }
    bool connected() const { return fd_.valid(); }
    bool send(std::string_view bytes) { return fd_ && sendAll(fd_.get(), bytes); }
    std::optional<std::string> read(int timeoutMs = 2000) { return readFrame(fd_.get(), timeoutMs); }
    // True when the server closed the connection within the limit; data is discarded.
    bool closedWithin(int timeoutMs) {
        const auto start = Clock::now();
        char buffer[65536];
        for (;;) {
            pollfd entry{fd_.get(), POLLIN, 0};
            const int left = timeoutMs - static_cast<int>(elapsedMs(start));
            if (left <= 0 || poll(&entry, 1, left) <= 0) return false;
            const ssize_t count = recv(fd_.get(), buffer, sizeof(buffer), 0);
            if (count == 0 || (count < 0 && errno != EINTR && errno != EAGAIN)) return true;
        }
    }
    void close() { fd_.reset(); }

private:
    UniqueFd fd_;
};
// Minimal misbehaving service: accepts one connection, reads one request frame
// and then lets the test decide what to answer.
class FakeServer {
public:
    FakeServer(const std::string& path, std::function<void(int)> answer)
        : path_(path), listener_(streamSocket()) {
        const auto target = address(path);
        if (!listener_ || bind(listener_.get(), reinterpret_cast<const sockaddr*>(&target), sizeof(target)) != 0 ||
            listen(listener_.get(), 4) != 0) throw std::runtime_error("fake server could not listen");
        thread_ = std::thread([this, answer = std::move(answer)] {
            pollfd entry{listener_.get(), POLLIN, 0};
            if (poll(&entry, 1, 3000) <= 0) return;
            UniqueFd connection = acceptBlocking(listener_.get());
            if (!connection) return;
            if (readFrame(connection.get())) answer(connection.get());
            // Let the client read everything before the close.
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        });
    }
    ~FakeServer() {
        if (thread_.joinable()) thread_.join();
        unlink(path_.c_str());
    }
    FakeServer(const FakeServer&) = delete;

private:
    std::string path_;
    UniqueFd listener_;
    std::thread thread_;
};
IpcSocketServerOptions serverOptions(const TempDir& dir, std::int64_t ioDeadlineMs = 5000) {
    IpcSocketServerOptions options;
    options.socketPath = dir.socket();
    options.expectedOwnerUid = geteuid();
    options.ioDeadlineMs = ioDeadlineMs;
    return options;
}
IpcSocketClient client(const TempDir& dir, uid_t serverUid = geteuid()) {
    return IpcSocketClient(IpcSocketClientOptions{dir.socket(), serverUid});
}
}  // namespace

PHPVPN_TEST(ipc_unix, complete_messages_roundtrip_restart_limits_and_client_deadline) {
    TempDir dir;
    std::atomic<unsigned> calls{0};
    IpcSocketServer server([&](std::string_view request) {
        ++calls;
        if (request == "slow") std::this_thread::sleep_for(std::chrono::milliseconds(150));
        return std::string(request);
    }, serverOptions(dir));
    PHPVPN_REQUIRE_OK(server.start());
    PHPVPN_REQUIRE_ERROR(server.start(), ErrorCode::service_already_running);
    struct stat info{};
    PHPVPN_CHECK(lstat(dir.socket().c_str(), &info) == 0 && S_ISSOCK(info.st_mode));
    PHPVPN_CHECK_EQ(info.st_mode & 07777, mode_t(0666));
    PHPVPN_CHECK_EQ(server.stats().connectionSlots, std::size_t(32));
    const auto peer = client(dir);
    for (unsigned i = 0; i < 30; ++i) {
        const std::string message = std::string(100000, 'a') + std::to_string(i);
        auto reply = peer.exchange(message, 2000);
        PHPVPN_REQUIRE_OK(reply);
        PHPVPN_CHECK_EQ(reply.value(), message);
    }
    IpcExchangeOutcome outcome;
    PHPVPN_REQUIRE_ERROR(peer.exchange("slow", 25, -1, &outcome), ErrorCode::timeout);
    PHPVPN_CHECK(outcome.failure == IpcSocketFailure::timeout);
    PHPVPN_CHECK(outcome.requestSent);
    PHPVPN_CHECK(waitUntil([&] { return calls.load() == 31; }));
    server.stop();
    PHPVPN_CHECK(lstat(dir.socket().c_str(), &info) != 0); // The name leaves with the server.
    PHPVPN_CHECK_EQ(server.stats().connectionSlots, std::size_t(0));
    PHPVPN_REQUIRE_OK(server.start());
    auto reply = peer.exchange("restarted", 2000);
    PHPVPN_REQUIRE_OK(reply);
    PHPVPN_CHECK_EQ(reply.value(), std::string("restarted"));
    // Exactly 4 MiB in both directions.
    const std::string maximum(kIpcMaxMessageBytes, 'm');
    auto maximumReply = peer.exchange(maximum, 5000);
    PHPVPN_REQUIRE_OK(maximumReply);
    PHPVPN_CHECK(maximumReply.value() == maximum);
    PHPVPN_REQUIRE_ERROR(peer.exchange("x", 0), ErrorCode::invalid_argument);
    PHPVPN_REQUIRE_ERROR(peer.exchange(std::string(kIpcMaxMessageBytes + 1, 'x'), 1000, -1, &outcome),
                         ErrorCode::ipc_message_too_large);
    PHPVPN_CHECK(outcome.failure == IpcSocketFailure::requestTooLarge);
    PHPVPN_CHECK(!outcome.requestSent);
    PHPVPN_CHECK_EQ(calls.load(), unsigned(33));
}

PHPVPN_TEST(ipc_unix, oversized_and_empty_frames_are_rejected_in_both_directions) {
    TempDir dir;
    std::atomic<unsigned> calls{0};
    IpcSocketServer server([&](std::string_view request) {
        ++calls;
        if (request == "oversized") return std::string(kIpcMaxMessageBytes + 1, 'r');
        return std::string(request);
    }, serverOptions(dir));
    PHPVPN_REQUIRE_OK(server.start());
    // Request header one byte over the limit: closed before any payload is read.
    RawClient oversized(dir.socket());
    PHPVPN_CHECK(oversized.send(header(kIpcMaxMessageBytes + 1)));
    PHPVPN_CHECK(oversized.closedWithin(2000));
    PHPVPN_CHECK(server.stats().rejected >= 1);
    RawClient empty(dir.socket());
    PHPVPN_CHECK(empty.send(header(0)));
    PHPVPN_CHECK(empty.closedWithin(2000));
    // A raw exact-limit frame is accepted by the server.
    RawClient exact(dir.socket());
    PHPVPN_CHECK(exact.send(frame(std::string(kIpcMaxMessageBytes, 'e'))));
    auto echoed = exact.read(5000);
    PHPVPN_CHECK(echoed && echoed->size() == kIpcMaxMessageBytes);
    PHPVPN_CHECK_EQ(calls.load(), unsigned(1));
    // Handler response one byte over the limit: the connection closes without a frame.
    IpcExchangeOutcome outcome;
    PHPVPN_REQUIRE_ERROR(client(dir).exchange("oversized", 3000, -1, &outcome), ErrorCode::ipc_unavailable);
    PHPVPN_CHECK(outcome.failure == IpcSocketFailure::connectionClosed);
    PHPVPN_CHECK(outcome.requestSent);
    auto after = client(dir).exchange("status", 1000);
    PHPVPN_REQUIRE_OK(after);
    PHPVPN_CHECK_EQ(after.value(), std::string("status"));
}

PHPVPN_TEST(ipc_unix, read_and_write_deadlines_reclaim_slots) {
    TempDir dir;
    IpcSocketServer server([](std::string_view request) {
        return request == "large" ? std::string(kIpcMaxMessageBytes, 'x') : std::string("ok");
    }, serverOptions(dir, 300));
    PHPVPN_REQUIRE_OK(server.start());
    RawClient silent(dir.socket());
    RawClient partialHeader(dir.socket());
    PHPVPN_CHECK(partialHeader.send(std::string("\0\0", 2)));
    RawClient partialPayload(dir.socket());
    PHPVPN_CHECK(partialPayload.send(header(10) + "12345"));
    RawClient nonReader(dir.socket());
    PHPVPN_CHECK(nonReader.send(frame("large")));
    const auto start = Clock::now();
    PHPVPN_CHECK(silent.closedWithin(2000));
    PHPVPN_CHECK(partialHeader.closedWithin(2000));
    PHPVPN_CHECK(partialPayload.closedWithin(2000));
    PHPVPN_CHECK(waitUntil([&] { return server.stats().ioTimeouts >= 4; }, 2000));
    PHPVPN_CHECK(elapsedMs(start) >= 250);
    auto after = client(dir).exchange("status", 1000);
    PHPVPN_REQUIRE_OK(after);
    PHPVPN_CHECK_EQ(after.value(), std::string("ok"));
    // A client that reads the response but never closes is released by the same deadline.
    RawClient lingering(dir.socket());
    PHPVPN_CHECK(lingering.send(frame("status")));
    auto response = lingering.read();
    PHPVPN_CHECK(response && *response == "ok");
    PHPVPN_CHECK(lingering.closedWithin(2000));
    PHPVPN_CHECK(waitUntil([&] { return server.stats().ioTimeouts >= 5; }, 2000));
}

PHPVPN_TEST(ipc_unix, default_read_deadline_is_five_seconds) {
    TempDir dir;
    IpcSocketServer server([](std::string_view) { return std::string("ok"); }, serverOptions(dir));
    PHPVPN_REQUIRE_OK(server.start());
    RawClient stalled(dir.socket());
    PHPVPN_CHECK(stalled.send(std::string("\0\0\0", 3)));
    const auto start = Clock::now();
    PHPVPN_CHECK(!stalled.closedWithin(4500));
    PHPVPN_CHECK(stalled.closedWithin(2500));
    const auto closedMs = elapsedMs(start);
    PHPVPN_CHECK(closedMs >= 4500 && closedMs < 7000);
    PHPVPN_CHECK_EQ(server.stats().ioTimeouts, std::uint64_t(1));
    std::cout << "[ MEASURE ] stalled client closed after " << closedMs << " ms\n";
}

PHPVPN_TEST(ipc_unix, thirty_two_slots_and_the_thirty_third_connection_waits) {
    TempDir dir;
    IpcSocketServer server([](std::string_view) { return std::string("ok"); }, serverOptions(dir, 3000));
    PHPVPN_REQUIRE_OK(server.start());
    std::vector<std::unique_ptr<RawClient>> idle;
    for (unsigned i = 0; i < 32; ++i) {
        idle.push_back(std::make_unique<RawClient>(dir.socket()));
        PHPVPN_CHECK(idle.back()->connected());
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    PHPVPN_CHECK_EQ(server.stats().connectionSlots, std::size_t(32));
    // The 33rd connection waits in the backlog until its own deadline.
    IpcExchangeOutcome outcome;
    const auto start = Clock::now();
    PHPVPN_REQUIRE_ERROR(client(dir).exchange("status", 300, -1, &outcome), ErrorCode::timeout);
    PHPVPN_CHECK(outcome.failure == IpcSocketFailure::timeout);
    PHPVPN_CHECK(elapsedMs(start) >= 250);
    // A waiting connection is served as soon as one slot is released.
    auto waiting = std::async(std::launch::async, [&] { return client(dir).exchange("status", 2000); });
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    idle.front()->close();
    auto served = waiting.get();
    PHPVPN_REQUIRE_OK(served);
    PHPVPN_CHECK_EQ(served.value(), std::string("ok"));
    PHPVPN_CHECK_EQ(server.stats().ioTimeouts, std::uint64_t(0));
    // With every slot and the listen backlog full, connect retries until the deadline.
    idle.push_back(std::make_unique<RawClient>(dir.socket()));
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    std::vector<UniqueFd> backlog;
    const auto target = address(dir.socket());
    for (unsigned i = 0; i < 256; ++i) {
        UniqueFd pending = streamSocket(true);
        if (connect(pending.get(), reinterpret_cast<const sockaddr*>(&target), sizeof(target)) != 0) {
            const int error = errno;
#if defined(__APPLE__)
            // The macOS kernel refuses a connection beyond the backlog.
            PHPVPN_CHECK_EQ(error, ECONNREFUSED);
#else
            PHPVPN_CHECK_EQ(error, EAGAIN);
#endif
            break;
        }
        backlog.push_back(std::move(pending));
    }
    PHPVPN_CHECK(backlog.size() < 256);
#if defined(__APPLE__)
    // A refused connection cannot be told apart from a stale socket, so the
    // macOS client reports the service as not running (ipc_unix.h).
    PHPVPN_REQUIRE_ERROR(client(dir).exchange("status", 300, -1, &outcome), ErrorCode::ipc_unavailable);
    PHPVPN_CHECK(outcome.failure == IpcSocketFailure::serviceNotRunning);
    PHPVPN_CHECK(!outcome.requestSent);
#else
    const auto full = Clock::now();
    PHPVPN_REQUIRE_ERROR(client(dir).exchange("status", 300, -1, &outcome), ErrorCode::timeout);
    PHPVPN_CHECK(outcome.failure == IpcSocketFailure::timeout);
    PHPVPN_CHECK(!outcome.requestSent);
    PHPVPN_CHECK(elapsedMs(full) >= 250);
#endif
    backlog.clear();
    idle.clear();
    PHPVPN_CHECK(waitUntil([&] { return client(dir).exchange("status", 200).ok(); }, 2000));
}

PHPVPN_TEST(ipc_unix, queues_are_bounded_busy_is_correlated_and_accepted_tasks_drain_on_stop) {
    TempDir dir;
    Gate gate;
    std::atomic<unsigned> fileStarted{0}, diagnosticStarted{0}, shortStarted{0}, finished{0}, cancels{0};
    auto options = serverOptions(dir);
    options.busyResponse = [](std::string_view request) { return "busy:" + std::string(request); };
    options.cancelHandlers = [&] { ++cancels; gate.release(); };
    options.classify = [](std::string_view command) {
        if (command == "file") return IpcWorkClass::file;
        return command == "diagnostic" ? IpcWorkClass::diagnostic : IpcWorkClass::shortControl;
    };
    IpcSocketServer server([&](std::string_view command) {
        if (command == "block") { ++shortStarted; gate.wait(); }
        if (command == "diagnostic") { ++diagnosticStarted; gate.wait(); }
        return std::string("ok");
    }, options);
    PHPVPN_CHECK(!server.tryPostFile([] {})); // Not started.
    PHPVPN_REQUIRE_OK(server.start());
    const auto peer = client(dir);
    // File executor: one running, four waiting, then busy.
    PHPVPN_CHECK(server.tryPostFile([&] { ++fileStarted; gate.wait(); ++finished; }));
    PHPVPN_CHECK(waitUntil([&] { return fileStarted == 1; }));
    for (unsigned i = 0; i < 4; ++i) PHPVPN_CHECK(server.tryPostFile([&] { ++finished; }));
    PHPVPN_CHECK(!server.tryPostFile([&] { ++finished; }));
    PHPVPN_CHECK_EQ(server.stats().fileQueued, std::size_t(4));
    auto overflowFile = peer.exchange("file", 1000);
    PHPVPN_REQUIRE_OK(overflowFile);
    PHPVPN_CHECK_EQ(overflowFile.value(), std::string("busy:file"));
    // Diagnostic executor: one outstanding in total.
    RawClient diagnostic(dir.socket());
    PHPVPN_CHECK(diagnostic.send(frame("diagnostic")));
    PHPVPN_CHECK(waitUntil([&] { return diagnosticStarted == 1; }));
    PHPVPN_CHECK_EQ(server.stats().diagnosticOutstanding, std::size_t(1));
    PHPVPN_CHECK(!server.tryPostDiagnostic([] {}));
    auto secondDiagnostic = peer.exchange("diagnostic", 1000);
    PHPVPN_REQUIRE_OK(secondDiagnostic);
    PHPVPN_CHECK_EQ(secondDiagnostic.value(), std::string("busy:diagnostic"));
    // Short control executor: one running, sixteen waiting, then busy.
    RawClient blocked(dir.socket());
    PHPVPN_CHECK(blocked.send(frame("block")));
    PHPVPN_CHECK(waitUntil([&] { return shortStarted == 1; }));
    std::vector<std::unique_ptr<RawClient>> queued;
    for (unsigned i = 0; i < 16; ++i) {
        queued.push_back(std::make_unique<RawClient>(dir.socket()));
        PHPVPN_CHECK(queued.back()->send(frame("queued")));
    }
    PHPVPN_CHECK(waitUntil([&] { return server.stats().shortQueued == 16; }));
    auto overflow = peer.exchange("overflow", 1000);
    PHPVPN_REQUIRE_OK(overflow);
    PHPVPN_CHECK_EQ(overflow.value(), std::string("busy:overflow"));
    PHPVPN_CHECK(server.stats().rejected >= 5);
    const auto begin = Clock::now();
    server.stop();
    PHPVPN_CHECK(elapsedMs(begin) < 5000);
    PHPVPN_CHECK_EQ(cancels.load(), unsigned(1));
    PHPVPN_CHECK_EQ(finished.load(), unsigned(5));
    PHPVPN_CHECK(!server.tryPostFile([] {}));
    PHPVPN_CHECK(!server.tryPostDiagnostic([] {}));
    for (auto& connection : queued) PHPVPN_CHECK(connection->closedWithin(1000));
}

PHPVPN_TEST(ipc_unix, socket_directory_policy_and_stale_socket) {
    TempDir dir;
    const auto handler = [](std::string_view) { return std::string("ok"); };
    // A stale socket of the expected owner is replaced.
    {
        UniqueFd stale = streamSocket();
        const auto target = address(dir.socket());
        PHPVPN_CHECK(bind(stale.get(), reinterpret_cast<const sockaddr*>(&target), sizeof(target)) == 0);
    }
    struct stat info{};
    PHPVPN_CHECK(lstat(dir.socket().c_str(), &info) == 0 && S_ISSOCK(info.st_mode));
    {
        IpcSocketServer server(handler, serverOptions(dir));
        PHPVPN_REQUIRE_OK(server.start());
        auto reply = client(dir).exchange("status", 1000);
        PHPVPN_REQUIRE_OK(reply);
    }
    // Anything else at the socket path is left alone and refused.
    {
        std::ofstream(dir.socket()) << "keep";
        IpcSocketServer server(handler, serverOptions(dir));
        PHPVPN_REQUIRE_ERROR(server.start(), ErrorCode::ipc_unavailable);
        PHPVPN_CHECK(lstat(dir.socket().c_str(), &info) == 0 && S_ISREG(info.st_mode));
        unlink(dir.socket().c_str());
        PHPVPN_CHECK(symlink("/tmp", dir.socket().c_str()) == 0);
        PHPVPN_REQUIRE_ERROR(server.start(), ErrorCode::ipc_unavailable);
        PHPVPN_CHECK(lstat(dir.socket().c_str(), &info) == 0 && S_ISLNK(info.st_mode));
        unlink(dir.socket().c_str());
    }
    // Foreign directory owner, group or world writable directory, missing directory.
    {
        auto foreign = serverOptions(dir);
        foreign.expectedOwnerUid = geteuid() + 1;
        IpcSocketServer server(handler, foreign);
        PHPVPN_REQUIRE_ERROR(server.start(), ErrorCode::ipc_unavailable);
        PHPVPN_CHECK(lstat(dir.socket().c_str(), &info) != 0);
    }
    for (const mode_t mode : {mode_t(0777), mode_t(0770), mode_t(0703)}) {
        PHPVPN_CHECK(chmod(dir.path().c_str(), mode) == 0);
        IpcSocketServer server(handler, serverOptions(dir));
        PHPVPN_REQUIRE_ERROR(server.start(), ErrorCode::ipc_unavailable);
    }
    PHPVPN_CHECK(chmod(dir.path().c_str(), 0755) == 0);
    {
        IpcSocketServer server(handler, serverOptions(dir));
        PHPVPN_REQUIRE_OK(server.start());
    }
    {
        auto missing = serverOptions(dir);
        missing.socketPath = dir.path() + "/absent/control.sock";
        IpcSocketServer server(handler, missing);
        PHPVPN_REQUIRE_ERROR(server.start(), ErrorCode::ipc_unavailable);
        missing.socketPath = "relative.sock";
        IpcSocketServer relative(handler, missing);
        PHPVPN_REQUIRE_ERROR(relative.start(), ErrorCode::invalid_argument);
        missing.socketPath = "/" + std::string(sizeof(sockaddr_un{}.sun_path), 'n');
        IpcSocketServer tooLong(handler, missing);
        PHPVPN_REQUIRE_ERROR(tooLong.start(), ErrorCode::invalid_argument);
    }
}

PHPVPN_TEST(ipc_unix, client_rejects_a_peer_with_an_unexpected_uid_before_sending) {
    TempDir dir;
    std::atomic<unsigned> calls{0};
    IpcSocketServer server([&](std::string_view) { ++calls; return std::string("ok"); }, serverOptions(dir));
    PHPVPN_REQUIRE_OK(server.start());
    IpcExchangeOutcome outcome;
    PHPVPN_REQUIRE_ERROR(client(dir, geteuid() + 1).exchange("{\"secret\":1}", 1000, -1, &outcome),
                         ErrorCode::ipc_unavailable);
    PHPVPN_CHECK(outcome.failure == IpcSocketFailure::invalidPeer);
    PHPVPN_CHECK(!outcome.requestSent);
    auto reply = client(dir).exchange("status", 1000);
    PHPVPN_REQUIRE_OK(reply);
    PHPVPN_CHECK_EQ(calls.load(), unsigned(1));
    // The production default expects root.
    if (geteuid() != 0) {
        IpcSocketClient production(IpcSocketClientOptions{dir.socket()});
        PHPVPN_REQUIRE_ERROR(production.exchange("status", 1000, -1, &outcome), ErrorCode::ipc_unavailable);
        PHPVPN_CHECK(outcome.failure == IpcSocketFailure::invalidPeer);
    }
}

PHPVPN_TEST(ipc_unix, client_classifies_truncated_oversized_and_missing_responses) {
    TempDir dir;
    struct Case {
        std::function<void(int)> answer;
        ErrorCode code;
        IpcSocketFailure failure;
    };
    const std::vector<Case> cases{
        {[](int fd) { sendAll(fd, header(100) + std::string(10, 't')); }, ErrorCode::ipc_malformed,
         IpcSocketFailure::truncatedResponse},
        {[](int fd) { sendAll(fd, std::string("\0\0", 2)); }, ErrorCode::ipc_malformed,
         IpcSocketFailure::truncatedResponse},
        {[](int) {}, ErrorCode::ipc_unavailable, IpcSocketFailure::connectionClosed},
        {[](int fd) { sendAll(fd, header(kIpcMaxMessageBytes + 1)); }, ErrorCode::ipc_message_too_large,
         IpcSocketFailure::responseTooLarge},
        {[](int fd) { sendAll(fd, header(0)); }, ErrorCode::ipc_malformed, IpcSocketFailure::invalidResponse},
    };
    for (const auto& item : cases) {
        FakeServer fake(dir.socket(), item.answer);
        IpcExchangeOutcome outcome;
        PHPVPN_REQUIRE_ERROR(client(dir).exchange("request", 2000, -1, &outcome), item.code);
        PHPVPN_CHECK(outcome.failure == item.failure);
        PHPVPN_CHECK(outcome.requestSent);
    }
    // A complete frame from the same fake peer is a success.
    FakeServer fake(dir.socket(), [](int fd) { sendAll(fd, frame("complete")); });
    auto complete = client(dir).exchange("request", 2000);
    PHPVPN_REQUIRE_OK(complete);
    PHPVPN_CHECK_EQ(complete.value(), std::string("complete"));
}

PHPVPN_TEST(ipc_unix, client_reports_service_not_running_access_denied_and_cancel) {
    TempDir dir;
    IpcExchangeOutcome outcome;
    // No socket at all.
    PHPVPN_REQUIRE_ERROR(client(dir).exchange("status", 1000, -1, &outcome), ErrorCode::ipc_unavailable);
    PHPVPN_CHECK(outcome.failure == IpcSocketFailure::serviceNotRunning);
    PHPVPN_CHECK(!outcome.requestSent);
    // A stale socket nobody listens on.
    {
        UniqueFd stale = streamSocket();
        const auto target = address(dir.socket());
        PHPVPN_CHECK(bind(stale.get(), reinterpret_cast<const sockaddr*>(&target), sizeof(target)) == 0);
    }
    PHPVPN_REQUIRE_ERROR(client(dir).exchange("status", 1000, -1, &outcome), ErrorCode::ipc_unavailable);
    PHPVPN_CHECK(outcome.failure == IpcSocketFailure::serviceNotRunning);
    unlink(dir.socket().c_str());
    // Access denied needs an unprivileged caller; root bypasses the socket mode.
    if (geteuid() != 0) {
        UniqueFd listener = streamSocket();
        const auto target = address(dir.socket());
        PHPVPN_CHECK(bind(listener.get(), reinterpret_cast<const sockaddr*>(&target), sizeof(target)) == 0);
        PHPVPN_CHECK(listen(listener.get(), 1) == 0);
        PHPVPN_CHECK(chmod(dir.socket().c_str(), 0) == 0);
        PHPVPN_REQUIRE_ERROR(client(dir).exchange("status", 1000, -1, &outcome), ErrorCode::ipc_unavailable);
        PHPVPN_CHECK(outcome.failure == IpcSocketFailure::accessDenied);
        unlink(dir.socket().c_str());
    }
    // Cancellation through a readable descriptor.
    Gate gate;
    auto options = serverOptions(dir);
    options.cancelHandlers = [&] { gate.release(); };
    IpcSocketServer server([&](std::string_view) { gate.wait(); return std::string("late"); }, options);
    PHPVPN_REQUIRE_OK(server.start());
    auto cancel = Notifier::create();
    PHPVPN_REQUIRE_OK(cancel);
    auto cancelled = std::async(std::launch::async, [&] {
        IpcExchangeOutcome local;
        auto result = client(dir).exchange("slow", 5000, cancel->fd(), &local);
        return std::make_pair(result.code(), local);
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    cancel->signal();
    const auto [code, local] = cancelled.get();
    PHPVPN_CHECK(code == ErrorCode::cancelled);
    PHPVPN_CHECK(local.failure == IpcSocketFailure::cancelled);
    PHPVPN_CHECK(local.requestSent);
    server.stop();
}

}  // namespace phpvpn
