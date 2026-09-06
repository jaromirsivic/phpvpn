#include "harness.h"
#include "platform/windows/ipc_pipe.h"
#include "core/crypto/crypto_provider.h"
#include "core/ipc/ipc_protocol.h"
#include <windows.h>
#include <array>
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <future>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>
#ifdef PHPVPN_RESOURCE_TESTS
#include <psapi.h>
#endif

namespace phpvpn {
namespace {
std::wstring privatePipe() {
    auto crypto = createPlatformCryptoProvider();
    if (!crypto) throw std::runtime_error("private pipe random provider failed");
    auto id = crypto.value()->randomUuidV4();
    if (!id) throw std::runtime_error("private pipe random identity failed");
    return L"\\\\.\\pipe\\phpvpn-test-availability-" +
        std::wstring(id->begin(), id->end());
}
template<class Predicate>
bool waitUntil(Predicate predicate, DWORD limit = 1000) {
    const auto end = GetTickCount64() + limit;
    do {
        if (predicate()) return true;
        Sleep(2);
    } while (GetTickCount64() < end);
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
class RawClient {
public:
    HANDLE pipe = INVALID_HANDLE_VALUE;
    explicit RawClient(const std::wstring& name) {
        const auto end = GetTickCount64() + 1500;
        do {
            pipe = CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
            if (pipe != INVALID_HANDLE_VALUE) break;
            if (GetLastError() != ERROR_PIPE_BUSY || GetTickCount64() >= end) break;
            WaitNamedPipeW(name.c_str(), 50);
        } while (true);
        if (pipe != INVALID_HANDLE_VALUE) {
            DWORD mode = PIPE_READMODE_MESSAGE;
            SetNamedPipeHandleState(pipe, &mode, nullptr, nullptr);
        }
    }
    ~RawClient() { if (pipe != INVALID_HANDLE_VALUE) CloseHandle(pipe); }
    RawClient(const RawClient&) = delete;
    bool send(std::string_view request) {
        if (pipe == INVALID_HANDLE_VALUE) return false;
        OVERLAPPED operation{};
        operation.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!operation.hEvent) return false;
        DWORD count = 0;
        bool success = WriteFile(pipe, request.data(), static_cast<DWORD>(request.size()),
            &count, &operation) != FALSE;
        if (!success && GetLastError() == ERROR_IO_PENDING) {
            if (WaitForSingleObject(operation.hEvent, 1000) != WAIT_OBJECT_0) {
                CancelIoEx(pipe, &operation);
                GetOverlappedResult(pipe, &operation, &count, TRUE);
            } else success = GetOverlappedResult(pipe, &operation, &count, FALSE) != FALSE;
        }
        CloseHandle(operation.hEvent);
        return success && count == request.size();
    }
};
}

PHPVPN_TEST(ipc_availability, complete_messages_roundtrip_restart_and_client_deadline) {
    const auto name = privatePipe();
    std::atomic<unsigned> calls{0};
    IpcPipeServerOptions options;
    options.pipeName = name;
    IpcPipeServer server([&](std::string_view request) {
        ++calls;
        if (request == "slow") Sleep(150);
        return std::string(request);
    }, options);
    PHPVPN_REQUIRE_OK(server.start());
    for (unsigned i = 0; i < 30; ++i) {
        const std::string message = std::string(100000, 'a') + std::to_string(i);
        auto reply = sendIpcRequestTo(name, message, 2000);
        PHPVPN_REQUIRE_OK(reply);
        PHPVPN_CHECK_EQ(reply.value(), message);
    }
    auto timeout = sendIpcRequestTo(name, "slow", 25);
    PHPVPN_REQUIRE_ERROR(timeout, ErrorCode::timeout);
    PHPVPN_CHECK(waitUntil([&] { return calls.load() == 31; }));
    server.stop();
    PHPVPN_REQUIRE_OK(server.start());
    auto reply = sendIpcRequestTo(name, "restarted", 2000);
    PHPVPN_REQUIRE_OK(reply);
    PHPVPN_CHECK_EQ(reply.value(), std::string("restarted"));
    const std::string maximum(kIpcMaxMessageBytes, 'm');
    auto maximumReply = sendIpcRequestTo(name, maximum, 2000);
    PHPVPN_REQUIRE_OK(maximumReply);
    PHPVPN_CHECK_EQ(maximumReply.value(), maximum);
    PHPVPN_REQUIRE_ERROR(sendIpcRequestTo(name, "x", 0), ErrorCode::invalid_argument);
    PHPVPN_REQUIRE_ERROR(sendIpcRequestTo(name, std::string(kIpcMaxMessageBytes + 1, 'x')),
        ErrorCode::ipc_message_too_large);
}

PHPVPN_TEST(ipc_availability, supported_load_keeps_status_and_disconnect_available) {
    const auto name = privatePipe();
    Gate slow;
    std::atomic<unsigned> tick{0}, diagnosticStarted{0}, fileStarted{0}, disconnects{0};
    IpcPipeServerOptions options;
    options.pipeName = name;
    options.cancelHandlers = [&] { slow.release(); };
    options.onTick = [&] { ++tick; };
    options.busyResponse = [](std::string_view) { return std::string("busy"); };
    IpcPipeServer server([&](std::string_view command) {
        if (command == "large") return std::string(1024 * 1024, 'x');
        if (command == "disconnect") ++disconnects;
        return std::string("ok");
    }, options);
    PHPVPN_REQUIRE_OK(server.start());
    PHPVPN_CHECK(server.tryPostDiagnostic([&] { ++diagnosticStarted; slow.wait(); }));
    PHPVPN_CHECK(server.tryPostFile([&] { ++fileStarted; slow.wait(); }));
    PHPVPN_CHECK(waitUntil([&] { return diagnosticStarted == 1 && fileStarted == 1; }));
    std::vector<std::unique_ptr<RawClient>> clients;
    for (unsigned i = 0; i < 8; ++i) {
        auto client = std::make_unique<RawClient>(name);
        PHPVPN_CHECK(client->pipe != INVALID_HANDLE_VALUE);
        clients.push_back(std::move(client)); // Incomplete request, no bytes.
    }
    for (unsigned i = 0; i < 8; ++i) {
        auto client = std::make_unique<RawClient>(name);
        PHPVPN_CHECK(client->send("large"));
        clients.push_back(std::move(client)); // Deliberately never reads response.
    }
    std::array<std::future<Result<std::string>>, 4> replies;
    const auto start = GetTickCount64();
    for (unsigned i = 0; i < replies.size(); ++i) {
        replies[i] = std::async(std::launch::async, [&, i] {
            return sendIpcRequestTo(name, i == 0 ? "disconnect" : "status", 1000);
        });
    }
    for (auto& future : replies) {
        auto result = future.get();
        PHPVPN_REQUIRE_OK(result);
        PHPVPN_CHECK_EQ(result.value(), std::string("ok"));
    }
    const auto responseMs = GetTickCount64() - start;
    PHPVPN_CHECK(responseMs < 1000);
    PHPVPN_CHECK_EQ(disconnects.load(), unsigned(1));
    PHPVPN_CHECK_EQ(server.stats().connectionSlots, std::size_t(32));
    const auto beforeTick = tick.load();
    PHPVPN_CHECK(waitUntil([&] { return tick.load() > beforeTick; }, 600));
    const auto stopStart = GetTickCount64();
    server.stop();
    const auto stopMs = GetTickCount64() - stopStart;
    PHPVPN_CHECK(stopMs < 5000);
    std::cout << "[ MEASURE ] supported-load status/disconnect " << responseMs
              << " ms, stop " << stopMs << " ms\n";
}

PHPVPN_TEST(ipc_availability, queues_are_bounded_and_accepted_tasks_drain_on_stop) {
    const auto name = privatePipe();
    Gate gate;
    std::atomic<unsigned> fileStarted{0}, diagnosticStarted{0}, shortStarted{0}, finished{0};
    IpcPipeServerOptions options;
    options.pipeName = name;
    options.busyResponse = [](std::string_view) { return std::string("busy"); };
    options.cancelHandlers = [&] { gate.release(); };
    options.classify = [](std::string_view command) {
        if (command == "file") return IpcWorkClass::file;
        return command == "diagnostic" ? IpcWorkClass::diagnostic : IpcWorkClass::shortControl;
    };
    IpcPipeServer server([&](std::string_view command) {
        if (command == "block") { ++shortStarted; gate.wait(); }
        if (command == "diagnostic") { ++diagnosticStarted; gate.wait(); }
        return std::string("ok");
    }, options);
    PHPVPN_REQUIRE_OK(server.start());
    PHPVPN_CHECK(server.tryPostFile([&] { ++fileStarted; gate.wait(); ++finished; }));
    PHPVPN_CHECK(waitUntil([&] { return fileStarted == 1; }));
    for (unsigned i = 0; i < 4; ++i) PHPVPN_CHECK(server.tryPostFile([&] { ++finished; }));
    PHPVPN_CHECK(!server.tryPostFile([&] { ++finished; }));
    PHPVPN_CHECK_EQ(server.stats().fileQueued, std::size_t(4));
    auto overflowFile = sendIpcRequestTo(name, "file", 1000);
    PHPVPN_REQUIRE_OK(overflowFile);
    PHPVPN_CHECK_EQ(overflowFile.value(), std::string("busy"));
    RawClient diagnostic(name);
    PHPVPN_CHECK(diagnostic.send("diagnostic"));
    PHPVPN_CHECK(waitUntil([&] { return diagnosticStarted == 1; }));
    PHPVPN_CHECK(!server.tryPostDiagnostic([] {}));
    auto secondDiagnostic = sendIpcRequestTo(name, "diagnostic", 1000);
    PHPVPN_REQUIRE_OK(secondDiagnostic);
    PHPVPN_CHECK_EQ(secondDiagnostic.value(), std::string("busy"));
    auto blocked = std::make_unique<RawClient>(name);
    PHPVPN_CHECK(blocked->send("block"));
    PHPVPN_CHECK(waitUntil([&] { return shortStarted == 1; }));
    std::vector<std::unique_ptr<RawClient>> queued;
    for (unsigned i = 0; i < 16; ++i) {
        auto client = std::make_unique<RawClient>(name);
        PHPVPN_CHECK(client->send("queued"));
        queued.push_back(std::move(client));
    }
    PHPVPN_CHECK(waitUntil([&] { return server.stats().shortQueued == 16; }));
    auto overflow = sendIpcRequestTo(name, "overflow", 1000);
    PHPVPN_REQUIRE_OK(overflow);
    PHPVPN_CHECK_EQ(overflow.value(), std::string("busy"));
    PHPVPN_CHECK(server.stats().rejected >= 3);
    const auto begin = GetTickCount64();
    server.stop();
    PHPVPN_CHECK(GetTickCount64() - begin < 5000);
    PHPVPN_CHECK_EQ(finished.load(), unsigned(5));
    PHPVPN_CHECK(!server.tryPostFile([] {}));
    PHPVPN_CHECK(!server.tryPostDiagnostic([] {}));
}

PHPVPN_TEST(ipc_availability, read_and_nonreader_deadlines_reclaim_slots) {
    const auto name = privatePipe();
    IpcPipeServerOptions options;
    options.pipeName = name;
    IpcPipeServer server([](std::string_view request) {
        return request == "large" ? std::string(1024 * 1024, 'x') : std::string("ok");
    }, options);
    PHPVPN_REQUIRE_OK(server.start());
    std::vector<std::unique_ptr<RawClient>> clients;
    for (unsigned i = 0; i < 16; ++i) clients.push_back(std::make_unique<RawClient>(name));
    for (unsigned i = 0; i < 16; ++i) {
        auto client = std::make_unique<RawClient>(name);
        PHPVPN_CHECK(client->send("large"));
        clients.push_back(std::move(client));
    }
    PHPVPN_CHECK_EQ(server.stats().connectionSlots, std::size_t(32));
    const auto begin = GetTickCount64();
    PHPVPN_CHECK(waitUntil([&] { return server.stats().ioTimeouts >= 32; }, 6500));
    auto after = sendIpcRequestTo(name, "status", 1000);
    PHPVPN_REQUIRE_OK(after);
    PHPVPN_CHECK_EQ(after.value(), std::string("ok"));
    std::cout << "[ MEASURE ] 32 incomplete/nonreader slots reclaimed after "
              << GetTickCount64() - begin << " ms\n";
}
#ifdef PHPVPN_RESOURCE_TESTS
PHPVPN_TEST(resource_ipc, maximum_messages_nonreaders_repeated_rss_and_recovery) {
    constexpr SIZE_T mib = 1024 * 1024;
    MEMORYSTATUSEX system{sizeof(system)};
    if (!GlobalMemoryStatusEx(&system) || system.ullAvailPhys < 1024 * mib)
        throw std::runtime_error("At least 1 GiB available physical memory is required for this opt-in test");
    auto memory = [] {
        PROCESS_MEMORY_COUNTERS_EX result{};
        result.cb = sizeof(result);
        if (!GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&result), sizeof(result)))
            throw std::runtime_error("GetProcessMemoryInfo failed");
        return result;
    };
    const auto baseline = memory();
    const auto name = privatePipe();
    const std::string maximum(kIpcMaxMessageBytes, 'm');
    std::atomic<unsigned> completed{0};
    IpcPipeServerOptions options;
    options.pipeName = name;
    IpcPipeServer server([&](std::string_view request) {
        std::string response(request);
        ++completed;
        return response;
    }, options);
    PHPVPN_REQUIRE_OK(server.start());
    SIZE_T maxPrivate = baseline.PrivateUsage, maxRss = baseline.WorkingSetSize;
    for (unsigned round = 0; round < 4; ++round) {
        std::vector<std::unique_ptr<RawClient>> clients;
        const auto before = completed.load();
        const auto timeouts = server.stats().ioTimeouts;
        for (unsigned slot = 0; slot < 32; ++slot) {
            auto client = std::make_unique<RawClient>(name);
            if (!client->send(maximum)) throw std::runtime_error("maximum request was not completely sent");
            clients.push_back(std::move(client));
        }
        if (!waitUntil([&] { return completed == before + 32; }, 2000))
            throw std::runtime_error("all 32 maximum responses were not allocated");
        PHPVPN_CHECK_EQ(server.stats().ioTimeouts, timeouts); // All slots occupied simultaneously.
        PHPVPN_CHECK_EQ(server.stats().connectionSlots, std::size_t(32));
        const auto loaded = memory();
        maxPrivate = (std::max)(maxPrivate, loaded.PrivateUsage);
        maxRss = (std::max)(maxRss, loaded.WorkingSetSize);
        PHPVPN_CHECK(loaded.PrivateUsage >= baseline.PrivateUsage + 240 * mib);
        // 256 MiB request/response + 2 MiB chunks, with 62 MiB explicitly
        // allowed for the test's client payload, stacks, allocator and runtime.
        PHPVPN_CHECK(loaded.PrivateUsage <= baseline.PrivateUsage + 320 * mib);
        PHPVPN_CHECK(loaded.WorkingSetSize <= baseline.WorkingSetSize + 320 * mib);
        RawClient overflow(name);
        PHPVPN_CHECK(overflow.pipe == INVALID_HANDLE_VALUE);
        PHPVPN_CHECK(waitUntil([&] { return server.stats().ioTimeouts >= timeouts + 32; }, 6500));
        clients.clear();
        const auto start = GetTickCount64();
        auto status = sendIpcRequestTo(name, "status", 1000);
        PHPVPN_REQUIRE_OK(status);
        PHPVPN_CHECK_EQ(status.value(), std::string("status"));
        const auto recovered = memory();
        std::cout << "[ MEASURE ] round=" << round + 1 << " slots=32 request=4194304 response=4194304"
                  << " loaded_rss=" << loaded.WorkingSetSize << " loaded_private=" << loaded.PrivateUsage
                  << " reclaimed_rss=" << recovered.WorkingSetSize << " reclaimed_private=" << recovered.PrivateUsage
                  << " status_ms=" << GetTickCount64() - start << "\n";
    }
    const auto stop = GetTickCount64();
    server.stop();
    PHPVPN_CHECK(GetTickCount64() - stop < 5000);
    const auto final = memory();
    PHPVPN_CHECK(final.PeakWorkingSetSize <= baseline.PeakWorkingSetSize + 320 * mib);
    PHPVPN_CHECK(final.PeakPagefileUsage <= baseline.PeakPagefileUsage + 320 * mib);
    std::cout << "[ MEASURE ] baseline_rss=" << baseline.WorkingSetSize << " baseline_private=" << baseline.PrivateUsage
              << " maximum_rss=" << maxRss << " maximum_private=" << maxPrivate
              << " os_peak_rss=" << final.PeakWorkingSetSize << " os_peak_commit=" << final.PeakPagefileUsage
              << " final_private=" << final.PrivateUsage << " stop_ms=" << GetTickCount64() - stop << "\n";
}
#endif
}  // namespace phpvpn
