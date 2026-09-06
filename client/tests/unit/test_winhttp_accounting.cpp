#include <winsock2.h>
#include <ws2tcpip.h>

#include "harness.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <future>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <thread>

#include "platform/iface/transport.h"

#pragma comment(lib, "ws2_32.lib")

namespace {
using namespace phpvpn;
using namespace std::chrono_literals;

// A private ephemeral loopback endpoint. It never touches the installed service,
// external network, configuration files, adapters or routes.
class BodyServer {
public:
    enum class Mode { complete, httpError, incomplete, truncated, reset, slowBody, slowHeaders, tlsHandshake };
    explicit BodyServer(Mode mode, std::size_t bytes) : mode_(mode), body_(bytes, 'x') {
        WSADATA data{};
        if (WSAStartup(MAKEWORD(2, 2), &data) != 0) throw std::runtime_error("WSAStartup failed");
        listener_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (listener_ == INVALID_SOCKET ||
            bind(listener_, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0 ||
            listen(listener_, 1) != 0) throw std::runtime_error("loopback listen failed");
        int length = sizeof(address);
        if (getsockname(listener_, reinterpret_cast<sockaddr*>(&address), &length) != 0)
            throw std::runtime_error("loopback port failed");
        port_ = ntohs(address.sin_port);
        worker_ = std::thread([this] { serve(); });
    }
    ~BodyServer() {
        {
            std::lock_guard lock(mutex_);
            released_ = true;
        }
        wake_.notify_all();
        closesocket(listener_);
        if (worker_.joinable()) worker_.join();
        WSACleanup();
    }
    std::string url() const { return std::string(mode_ == Mode::tlsHandshake ? "https" : "http") + "://127.0.0.1:" + std::to_string(port_) + "/phpvpn.php"; }
    bool waitForBody() {
        std::unique_lock lock(mutex_);
        return wake_.wait_for(lock, 5s, [this] { return bodySent_; });
    }

private:
    static bool sendAll(SOCKET peer, const std::string& text) {
        std::size_t offset = 0;
        while (offset < text.size()) {
            const int sent = send(peer, text.data() + offset, static_cast<int>(text.size() - offset), 0);
            if (sent <= 0) return false;
            offset += static_cast<std::size_t>(sent);
        }
        return true;
    }
    void serve() {
        const SOCKET peer = accept(listener_, nullptr, nullptr);
        if (peer == INVALID_SOCKET) return;
        if (mode_ == Mode::tlsHandshake) {
            std::unique_lock lock(mutex_);
            bodySent_ = true;
            wake_.notify_all();
            wake_.wait_for(lock, 5s, [this] { return released_; });
            closesocket(peer);
            return;
        }
        const DWORD timeout = 5000;
        setsockopt(peer, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
        std::string request;
        char block[4096];
        for (;;) {
            const int received = recv(peer, block, sizeof(block), 0);
            if (received <= 0) { closesocket(peer); return; }
            request.append(block, static_cast<std::size_t>(received));
            const auto headerEnd = request.find("\r\n\r\n");
            if (headerEnd != std::string::npos && request.size() >= headerEnd + 6) break;
        }
        const bool incomplete = mode_ == Mode::incomplete || mode_ == Mode::truncated || mode_ == Mode::reset;
        const std::string response = std::string("HTTP/1.1 ") +
            (mode_ == Mode::httpError ? "503 Service Unavailable" : "200 OK") +
            "\r\nContent-Type: application/x-ndjson\r\nConnection: close\r\nContent-Length: " +
            std::to_string(body_.size() + (incomplete ? 100 : 0)) + "\r\n\r\n";
        if (mode_ == Mode::slowHeaders) {
            std::unique_lock lock(mutex_);
            wake_.wait_for(lock, 3s, [this] { return released_; });
        }
        if (mode_ == Mode::slowBody) {
            if (sendAll(peer, response)) {
                // Deliver a complete WinHTTP internal read buffer before the
                // trickle, making partial-byte evidence independent of its
                // implementation-specific buffering of small socket writes.
                if (!sendAll(peer, body_.substr(0, 8192))) { closesocket(peer); return; }
                for (std::size_t i = 8192; i < body_.size(); i += 1024) {
                    if (!sendAll(peer, body_.substr(i, 1024))) break;
                    std::unique_lock lock(mutex_);
                    if (wake_.wait_for(lock, 150ms, [this] { return released_; })) break;
                }
            }
        } else if (sendAll(peer, response) && sendAll(peer, body_)) {
            std::unique_lock lock(mutex_);
            bodySent_ = true;
            wake_.notify_all();
            if (mode_ == Mode::incomplete) wake_.wait_for(lock, 5s, [this] { return released_; });
            if (mode_ == Mode::truncated || mode_ == Mode::reset)
                wake_.wait_for(lock, 200ms, [this] { return released_; });
        }
        if (mode_ == Mode::reset) {
            const linger reset{1, 0};
            setsockopt(peer, SOL_SOCKET, SO_LINGER, reinterpret_cast<const char*>(&reset), sizeof(reset));
        } else {
            shutdown(peer, SD_BOTH);
        }
        closesocket(peer);
    }
    Mode mode_;
    std::string body_;
    SOCKET listener_ = INVALID_SOCKET;
    unsigned short port_ = 0;
    std::thread worker_;
    std::mutex mutex_;
    std::condition_variable wake_;
    bool bodySent_ = false;
    bool released_ = false;
};

std::unique_ptr<Transport> transportFor(const BodyServer& server, std::int64_t cap = 1000000) {
    auto created = createPlatformTransport();
    if (!created.ok()) throw std::runtime_error("platform transport creation failed");
    auto transport = std::move(created).value();
    TransportSettings settings;
    settings.serverUrl = server.url();
    settings.responseTimeoutSeconds = 1;
    settings.maxResponseBodyBytes = cap;
    if (!transport->configure(settings).ok()) throw std::runtime_error("transport configuration failed");
    return transport;
}
}

PHPVPN_TEST(WinHttpAccounting, SuccessfulAndUnsuccessfulHttpStatusesCountBodyOnly) {
    for (const auto mode : {BodyServer::Mode::complete, BodyServer::Mode::httpError}) {
        BodyServer server(mode, 137);
        auto transport = transportFor(server);
        auto response = transport->post(HttpAction::data, "{}");
        PHPVPN_CHECK(response.ok());
        PHPVPN_CHECK_EQ(response.responseBodyBytesRead, 137U);
        if (response.ok()) {
            PHPVPN_CHECK_EQ(response.value().body.size(), 137U);
            PHPVPN_CHECK_EQ(response.value().statusCode, mode == BodyServer::Mode::complete ? 200 : 503);
        }
    }
}

PHPVPN_TEST(WinHttpAccounting, OversizedReadBlockIsAccountedBeforeLimitRejection) {
    BodyServer server(BodyServer::Mode::complete, 65536);
    auto transport = transportFor(server, 1);
    auto response = transport->post(HttpAction::data, "{}");
    PHPVPN_CHECK(!response.ok());
    if (!response.ok()) PHPVPN_CHECK_EQ(response.error().code, ErrorCode::transport_response_too_large);
    // WinHTTP may return fewer than the requested 64 KiB, but every byte of the
    // over-limit block must survive failure and must not be truncated to cap.
    PHPVPN_CHECK(response.responseBodyBytesRead > 1);
    PHPVPN_CHECK(response.responseBodyBytesRead <= 65536);
}

PHPVPN_TEST(WinHttpAccounting, PartialBodySurvivesReceiveTimeout) {
    BodyServer server(BodyServer::Mode::incomplete, 65536);
    auto transport = transportFor(server);
    auto response = transport->post(HttpAction::data, "{}");
    PHPVPN_CHECK(!response.ok());
    if (!response.ok()) PHPVPN_CHECK_EQ(response.error().code, ErrorCode::transport_timeout);
    PHPVPN_CHECK_EQ(response.responseBodyBytesRead, 65536U);
}

PHPVPN_TEST(WinHttpAccounting, PartialBodySurvivesPrematureSocketClose) {
    BodyServer server(BodyServer::Mode::truncated, 65536);
    auto transport = transportFor(server);
    auto response = transport->post(HttpAction::data, "{}");
    PHPVPN_CHECK(!response.ok());
    PHPVPN_CHECK_EQ(response.responseBodyBytesRead, 65536U);
}

PHPVPN_TEST(WinHttpAccounting, PartialBodySurvivesExplicitCancellation) {
    BodyServer server(BodyServer::Mode::incomplete, 65536);
    auto transport = transportFor(server);
    auto pending = std::async(std::launch::async, [&] { return transport->post(HttpAction::data, "{}"); });
    PHPVPN_CHECK(server.waitForBody());
    // The known full read block is delivered before cancellation interrupts the
    // following read. No application polling is involved.
    std::this_thread::sleep_for(200ms);
    transport->cancelCurrentRequest();
    const auto response = pending.get();
    PHPVPN_CHECK(!response.ok());
    if (!response.ok()) PHPVPN_CHECK_EQ(response.error().code, ErrorCode::cancelled);
    PHPVPN_CHECK_EQ(response.responseBodyBytesRead, 65536U);
}

PHPVPN_TEST(WinHttpAccounting, PartialBodySurvivesConnectionResetReadError) {
    BodyServer server(BodyServer::Mode::reset, 65536);
    auto transport = transportFor(server);
    auto response = transport->post(HttpAction::data, "{}");
    PHPVPN_CHECK(!response.ok());
    PHPVPN_CHECK_EQ(response.responseBodyBytesRead, 65536U);
}

PHPVPN_TEST(WinHttpDeadline, HeadersTlsAndTrickleBodyShareOneAbsoluteBudget) {
    for (const auto mode : {BodyServer::Mode::slowHeaders, BodyServer::Mode::tlsHandshake, BodyServer::Mode::slowBody}) {
        BodyServer server(mode, 65536);
        auto transport = transportFor(server);
        const auto start = std::chrono::steady_clock::now();
        auto result = transport->post(HttpAction::data, "{}");
        const auto elapsed = std::chrono::steady_clock::now() - start;
        PHPVPN_CHECK(!result.ok());
        if (!result.ok()) PHPVPN_CHECK_EQ(result.error().code, ErrorCode::transport_timeout);
        // ERROR_WINHTTP_TIMEOUT may originate in TCP/IP independently of the
        // configured WinHTTP intervals. The overall budget is an upper bound,
        // not a requirement to delay an already reported native failure.
        // https://learn.microsoft.com/en-us/windows/win32/winhttp/error-messages
        std::cout << "    deadline mode=" << static_cast<int>(mode)
                  << " elapsed=" << std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count()
                  << " ms (native timeout may precede the absolute deadline)\n";
        PHPVPN_CHECK(elapsed < 2500ms);
        if (mode == BodyServer::Mode::slowBody) {
            PHPVPN_CHECK(result.responseBodyBytesRead > 0);
            PHPVPN_CHECK(result.responseBodyBytesRead < 65536);
        }
    }
}

PHPVPN_TEST(WinHttpDeadline, BoundarySettingsNeverBecomeInfinite) {
    auto created = createPlatformTransport();
    PHPVPN_REQUIRE_OK(created);
    TransportSettings settings;
    settings.serverUrl = "http://127.0.0.1/phpvpn.php";
    for (const auto seconds : {0LL, -1LL, 2147484LL, 9223372036854775807LL}) {
        settings.responseTimeoutSeconds = seconds;
        PHPVPN_CHECK_EQ(created.value()->configure(settings).code(), ErrorCode::config_invalid_limit);
    }
    settings.responseTimeoutSeconds = 2147483;
    PHPVPN_REQUIRE_OK(created.value()->configure(settings));
}

PHPVPN_TEST(WinHttpLifecycle, ConcurrentCancellationThenReuse) {
    auto created = createPlatformTransport();
    PHPVPN_REQUIRE_OK(created);
    auto transport = std::move(created).value();
    for (int i = 0; i < 5; ++i) {
        BodyServer server(BodyServer::Mode::tlsHandshake, 0);
        TransportSettings settings;
        settings.serverUrl = server.url();
        PHPVPN_REQUIRE_OK(transport->configure(settings));
        auto pending = std::async(std::launch::async, [&] { return transport->post(HttpAction::health, "{}"); });
        PHPVPN_CHECK(server.waitForBody());
        const auto start = std::chrono::steady_clock::now();
        std::thread first([&] { transport->cancelCurrentRequest(); });
        std::thread second([&] { transport->cancelCurrentRequest(); });
        first.join();
        second.join();
        auto result = pending.get();
        PHPVPN_CHECK(std::chrono::steady_clock::now() - start < 2s);
        PHPVPN_CHECK(!result.ok());
        if (!result.ok()) PHPVPN_CHECK_EQ(result.error().code, ErrorCode::cancelled);
    }
    BodyServer server(BodyServer::Mode::complete, 42);
    TransportSettings settings;
    settings.serverUrl = server.url();
    PHPVPN_REQUIRE_OK(transport->configure(settings));
    auto response = transport->post(HttpAction::data, "{}");
    PHPVPN_CHECK(response.ok());
    PHPVPN_CHECK_EQ(response.responseBodyBytesRead, 42U);
}
