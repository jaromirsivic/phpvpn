#include <winsock2.h>
#include <ws2tcpip.h>

#include "harness.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <future>
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
    enum class Mode { complete, httpError, incomplete, truncated, reset };
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
    std::string url() const { return "http://127.0.0.1:" + std::to_string(port_) + "/phpvpn.php"; }
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
        if (sendAll(peer, response) && sendAll(peer, body_)) {
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
