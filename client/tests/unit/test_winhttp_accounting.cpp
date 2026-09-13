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
    explicit BodyServer(Mode mode, std::size_t bytes, std::string extraHeaders = {}) : mode_(mode), body_(bytes, 'x'), extraHeaders_(std::move(extraHeaders)) {
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
    std::string receivedRequest() { std::lock_guard lock(mutex_); return request_; }
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
            if (headerEnd != std::string::npos) {
                const auto lengthAt = request.find("Content-Length:");
                if (lengthAt == std::string::npos) { closesocket(peer); return; }
                const auto length = std::stoull(request.substr(lengthAt + 15));
                if (request.size() >= headerEnd + 4 + length) break;
            }
        }
        { std::lock_guard lock(mutex_); request_ = request; }
        const bool incomplete = mode_ == Mode::incomplete || mode_ == Mode::truncated || mode_ == Mode::reset;
        const std::string response = std::string("HTTP/1.1 ") +
            (mode_ == Mode::httpError ? "503 Service Unavailable" : "200 OK") +
            "\r\nContent-Type: application/x-ndjson\r\nConnection: close\r\nContent-Length: " +
            std::to_string(body_.size() + (incomplete ? 100 : 0)) + "\r\n" + extraHeaders_ + "\r\n";
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
    std::string extraHeaders_, request_;
    SOCKET listener_ = INVALID_SOCKET;
    unsigned short port_ = 0;
    std::thread worker_;
    std::mutex mutex_;
    std::condition_variable wake_;
    bool bodySent_ = false;
    bool released_ = false;
};

std::unique_ptr<Transport> transportFor(const BodyServer& server, std::int64_t cap = 1000000, bool capture = false) {
    auto created = createPlatformTransport();
    if (!created.ok()) throw std::runtime_error("platform transport creation failed");
    auto transport = std::move(created).value();
    TransportSettings settings; settings.httpHeaders = "X-Test: fixture";
    settings.serverUrl = server.url();
    settings.responseTimeoutSeconds = 1;
    settings.maxResponseBodyBytes = cap;
    settings.captureWizardHttpDiagnostics = capture;
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

PHPVPN_TEST(WinHttpDiagnostic, CapturesApplicationExchangeBeforeProcessingAndNeverData) {
    for (auto action : {HttpAction::health, HttpAction::info, HttpAction::data}) {
        for (bool enabled : {false, true}) {
            BodyServer server(BodyServer::Mode::httpError, 65537);
            auto transport = transportFor(server, 1000000, enabled);
            auto result = transport->post(action, "{}");
            PHPVPN_REQUIRE_OK(result);
            PHPVPN_CHECK_EQ(result.diagnostic.has_value(), enabled && action != HttpAction::data);
            if (!result.diagnostic) continue;
            const auto& d = *result.diagnostic;
            PHPVPN_CHECK_EQ(d.action, action);
            PHPVPN_CHECK(d.responseComplete);
            PHPVPN_CHECK(d.request.prefix.starts_with("POST " + server.url() + "?action=" + httpActionName(action) + " HTTP/"));
            PHPVPN_CHECK(d.request.prefix.ends_with("\r\n\r\n{}"));
            PHPVPN_CHECK(d.request.prefix.find("Content-Type: " + std::string(httpActionContentType(action))) != std::string::npos);
            PHPVPN_CHECK_EQ(d.requestHeadersSource, RequestHeadersSource::winhttp);
            PHPVPN_CHECK(d.response.prefix.starts_with("HTTP/1.1 503 Service Unavailable\r\n"));
            PHPVPN_CHECK_EQ(d.response.prefix.size(), 64512u);
            PHPVPN_CHECK_EQ(d.response.suffix, std::string(1024, 'x'));
            PHPVPN_CHECK(d.response.originalSizeBytes > 65537);
        }
    }
}
PHPVPN_TEST(WinHttpDiagnostic, LimitIncludesRejectedBlockButTimeoutDoesNotPublish) {
    {
        BodyServer server(BodyServer::Mode::complete, 32768);
        auto transport = transportFor(server, 100, true);
        auto result = transport->post(HttpAction::health, "{}");
        PHPVPN_REQUIRE_ERROR(result, ErrorCode::transport_response_too_large);
        PHPVPN_CHECK(result.responseProcessingFailed && result.diagnostic);
        if (result.diagnostic) {
            PHPVPN_CHECK(!result.diagnostic->responseComplete);
            const auto& r = result.diagnostic->response;
            PHPVPN_CHECK_EQ(r.originalSizeBytes, r.prefix.find("\r\n\r\n") + 4 + result.responseBodyBytesRead);
        }
    }
    {
        BodyServer server(BodyServer::Mode::incomplete, 16384);
        auto transport = transportFor(server, 1000000, true);
        auto result = transport->post(HttpAction::health, "{}");
        PHPVPN_REQUIRE_ERROR(result, ErrorCode::transport_timeout);
        PHPVPN_CHECK(!result.diagnostic);
    }
}

PHPVPN_TEST(WinHttpDiagnostic, ExactRequestBytesIndependentBoundsDuplicatesAndImplicitPath) {
    for (std::size_t requestSize : {0u, 1u, 65536u, 65537u}) {
        BodyServer server(BodyServer::Mode::httpError, requestSize % 2, "X-Dup: first\r\nX-Dup: second\r\n");
        auto transport = createPlatformTransport().value();
        TransportSettings settings; settings.httpHeaders = "X-Test: fixture"; settings.captureWizardHttpDiagnostics = true;
        settings.serverUrl = server.url().substr(0, server.url().find("/phpvpn.php"));
        PHPVPN_REQUIRE_OK(transport->configure(settings));
        std::string sent(requestSize, 's');
        if (sent.size() > 10) sent.replace(2, 8, std::string("\0\xff\r\n...\n", 8));
        const auto result = transport->post(HttpAction::info, sent);
        PHPVPN_CHECK(result.ok() && result.diagnostic.has_value());
        if (!result.diagnostic) continue;
        const auto& d = *result.diagnostic;
        const auto received = server.receivedRequest();
        const auto bodyStart = received.find("\r\n\r\n");
        PHPVPN_CHECK(bodyStart != std::string::npos);
        PHPVPN_CHECK_EQ(received.substr(bodyStart + 4), sent);
        PHPVPN_CHECK(received.starts_with("POST /?action=info HTTP/1.1\r\n"));
        PHPVPN_CHECK(d.request.prefix.starts_with("POST " + settings.serverUrl + "/?action=info HTTP/"));
        const auto start = d.request.prefix.find("\r\n\r\n") + 4;
        const auto full = d.request.prefix.substr(0, start) + sent;
        PHPVPN_CHECK_EQ(d.request.originalSizeBytes, full.size());
        PHPVPN_CHECK_EQ(d.request.prefix, full.substr(0, full.size() <= 65536 ? full.size() : 64512));
        PHPVPN_CHECK_EQ(d.request.suffix, full.size() <= 65536 ? std::string{} : full.substr(full.size() - 1024));
        PHPVPN_CHECK(d.response.prefix.find("X-Dup: first\r\nX-Dup: second") != std::string::npos);
        PHPVPN_CHECK(d.responseComplete && !d.response.truncated());
    }
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
    TransportSettings settings; settings.httpHeaders = "X-Test: fixture";
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
        TransportSettings settings; settings.httpHeaders = "X-Test: fixture";
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
    TransportSettings settings; settings.httpHeaders = "X-Test: fixture";
    settings.serverUrl = server.url();
    PHPVPN_REQUIRE_OK(transport->configure(settings));
    auto response = transport->post(HttpAction::data, "{}");
    PHPVPN_CHECK(response.ok());
    PHPVPN_CHECK_EQ(response.responseBodyBytesRead, 42U);
}

#include "core/config/config.h"
#include "core/protocol/http_headers.h"
namespace {
std::vector<std::string> wireValues(const std::string& request, std::string name) {
    std::vector<std::string> values;
    const auto end = request.find("\r\n\r\n");
    std::size_t at = request.find("\r\n") + 2;
    while (at < end) {
        const auto next = request.find("\r\n", at);
        auto line = request.substr(at, next - at);
        const auto colon = line.find(':');
        auto key = line.substr(0, colon);
        for (auto& c : key) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
        if (key == name) {
            auto value = line.substr(colon + 1);
            while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) value.erase(0, 1);
            values.push_back(value);
        }
        at = next + 2;
    }
    return values;
}
}
PHPVPN_TEST(WinHttpHeaders, WireMatrix) {
    for (auto action : {HttpAction::health, HttpAction::info, HttpAction::data}) {
        const std::vector<std::string> blocks = {kDefaultHttpHeaders, "X-Test: own", "User-Agent: own-agent",
            "X-Order: first\r\nX-Trace: second\r\nX-Last: third",
            "X-Test: lf\nX-Empty:\n", "X-Utf8: \xc5\xbe\xc3\xa1\xf0\x9f\x98\x80",
            "Cookie: a=1; b=2", "Cache-Control: no-store", "Pragma: custom"};
        for (const auto& block : blocks) {
            BodyServer server(BodyServer::Mode::complete, 3);
            auto transport = createPlatformTransport().value();
            TransportSettings settings;
            settings.serverUrl = server.url();
            settings.httpHeaders = block;
            settings.responseTimeoutSeconds = 2;
            PHPVPN_CHECK(transport->configure(settings).ok());
            const std::string body("a\0b\r\n", 5);
            const auto result = transport->postDiagnostic(action, body);
            PHPVPN_CHECK(result.ok());
            if (!result.ok()) {
                std::cout << "WIRE failure action=" << httpActionName(action) << " headers=" << block
                          << " error=" << result.error().name() << "\n";
                continue;
            }
            const auto wire = server.receivedRequest();
            std::cout << "WIRE action=" << httpActionName(action) << " headers=" << block << "\n" << wire.substr(0, wire.find("\r\n\r\n")) << "\n";
            PHPVPN_CHECK(wire.starts_with(std::string("POST /phpvpn.php?action=") + httpActionName(action) + " HTTP/1.1\r\n"));
            PHPVPN_CHECK_EQ(wire.substr(wire.find("\r\n\r\n") + 4), body);
            PHPVPN_CHECK_EQ(wireValues(wire, "content-length"), std::vector<std::string>{"5"});
            PHPVPN_CHECK(wire.find("PHPVPN/1.0") == std::string::npos);
            const auto expectedHeaders = buildApplicationRequestHeaders(httpActionContentType(action), *normalizeHttpHeaders(block));
            const auto expected = std::string("POST / HTTP/1.1\r\n") + expectedHeaders + "\r\n\r\n";
            for (const auto name : {"cache-control", "pragma", "user-agent", "x-order", "x-trace", "x-last", "x-test", "x-empty", "x-utf8", "cookie"}) {
                PHPVPN_CHECK_EQ(wireValues(wire, name), wireValues(expected, name));
                if (result.diagnostic) PHPVPN_CHECK_EQ(wireValues(result.diagnostic->request.prefix, name), wireValues(expected, name));
            }
            PHPVPN_CHECK_EQ(wireValues(wire, "content-type"), std::vector<std::string>{httpActionContentType(action)});
            PHPVPN_CHECK_EQ(wireValues(wire, "connection"), std::vector<std::string>{"Keep-Alive"});
            PHPVPN_CHECK_EQ(result.diagnostic.has_value(), action != HttpAction::data);
            if (result.diagnostic) {
                const auto& captured = result.diagnostic->request.prefix;
                PHPVPN_CHECK_EQ(captured.substr(captured.find("\r\n\r\n") + 4), body);
            }
            if (block == "X-Test: own") PHPVPN_CHECK(wireValues(wire, "user-agent").empty());
            if (block == "User-Agent: own-agent") PHPVPN_CHECK_EQ(wireValues(wire, "user-agent"), std::vector<std::string>{"own-agent"});
            if (block.starts_with("X-Utf8")) PHPVPN_CHECK_EQ(wireValues(wire, "x-utf8"), std::vector<std::string>{"\xc5\xbe\xc3\xa1\xf0\x9f\x98\x80"});
        }
    }
}

PHPVPN_TEST(WinHttpHeaders, ConfigureRejectsUnsafeBlocksBeforeSendingAndReconfigureRemovesUserAgent) {
    auto transport = createPlatformTransport().value();
    for (const auto bad : {"", "X: ok\r\n\r\nGET / HTTP/1.1", "Content-Type: bad", "content-length: 0",
        "CONNECTION: close", "hOsT: other", "X: first\nx: second", "User-Agent: first\nuser-agent: second", "Transfer-Encoding: chunked", "X: \xff"}) {
        BodyServer server(BodyServer::Mode::complete, 0);
        TransportSettings settings;
        settings.serverUrl = server.url();
        settings.httpHeaders = bad;
        PHPVPN_CHECK(!transport->configure(settings));
        PHPVPN_CHECK(!transport->post(HttpAction::data, "body").ok());
        PHPVPN_CHECK(server.receivedRequest().empty());
    }
    for (const auto headers : {kDefaultHttpHeaders, "X-Test: own", "User-Agent: replacement", "X-Test: own"}) {
        BodyServer server(BodyServer::Mode::complete, 0);
        TransportSettings settings;
        settings.serverUrl = server.url();
        settings.httpHeaders = headers;
        PHPVPN_REQUIRE_OK(transport->configure(settings));
        PHPVPN_REQUIRE_OK(transport->post(HttpAction::info, "{}"));
        const auto wire = server.receivedRequest();
        const auto userAgent = wireValues(wire, "user-agent");
        if (std::string_view(headers) == "X-Test: own") PHPVPN_CHECK(userAgent.empty());
        else PHPVPN_CHECK_EQ(userAgent.size(), 1u);
        PHPVPN_CHECK(wire.find("PHPVPN/1.0") == std::string::npos);
    }
}
