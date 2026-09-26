// Linux platform tests owned by WP03 (plan-v001 section 9.4): the libcurl
// transport against an in-process HTTP(S) server on an ephemeral 127.0.0.1 port.
// The expectations follow the WinHTTP suite (tests/unit/test_winhttp_accounting.cpp).
// Nothing here touches the external network, a proxy or the installed service.
#include "harness.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "core/config/config.h"
#include "core/protocol/http_headers.h"
#include "platform/iface/transport.h"

namespace phpvpn {
namespace {

using namespace std::chrono_literals;
using SteadyClock = std::chrono::steady_clock;

// SSL_write on a socket the client already closed must fail, not kill the run.
[[maybe_unused]] const bool kIgnoreSigpipe = [] {
    signal(SIGPIPE, SIG_IGN);
    return true;
}();

std::string lower(std::string text) {
    for (char& c : text) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + ('a' - 'A'));
    return text;
}

// Self-signed P-256 certificate for 127.0.0.1, generated per process.
class TlsContext {
public:
    TlsContext() {
        EVP_PKEY* key = EVP_EC_gen("P-256");
        X509* cert = X509_new();
        if (!key || !cert) throw std::runtime_error("certificate allocation failed");
        X509_set_version(cert, 2);
        ASN1_INTEGER_set(X509_get_serialNumber(cert), 1);
        X509_gmtime_adj(X509_getm_notBefore(cert), -3600);
        X509_gmtime_adj(X509_getm_notAfter(cert), 86400);
        X509_set_pubkey(cert, key);
        X509_NAME* name = X509_get_subject_name(cert);
        X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC, reinterpret_cast<const unsigned char*>("127.0.0.1"), -1, -1, 0);
        X509_set_issuer_name(cert, name);
        // A matching SAN leaves the unknown issuer as the only verification failure.
        X509_EXTENSION* san = X509V3_EXT_conf_nid(nullptr, nullptr, NID_subject_alt_name, const_cast<char*>("IP:127.0.0.1"));
        if (!san || !X509_add_ext(cert, san, -1)) throw std::runtime_error("certificate extension failed");
        X509_EXTENSION_free(san);
        if (!X509_sign(cert, key, EVP_sha256())) throw std::runtime_error("certificate signing failed");
        ctx_ = SSL_CTX_new(TLS_server_method());
        if (!ctx_ || SSL_CTX_use_certificate(ctx_, cert) != 1 || SSL_CTX_use_PrivateKey(ctx_, key) != 1)
            throw std::runtime_error("TLS context failed");
        X509_free(cert);
        EVP_PKEY_free(key);
    }
    ~TlsContext() { SSL_CTX_free(ctx_); }
    SSL_CTX* get() const { return ctx_; }

private:
    SSL_CTX* ctx_ = nullptr;
};

class LoopbackServer;

// One accepted connection, plain or TLS. The TLS handshake runs lazily on the
// first read or write, so a handler that never reads stalls the handshake.
class Peer {
public:
    Peer(LoopbackServer& server, int fd, SSL_CTX* tls) : server_(server), fd_(fd) {
        const timeval timeout{5, 0};
        setsockopt(fd_, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        setsockopt(fd_, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
        if (tls) {
            ssl_ = SSL_new(tls);
            SSL_set_fd(ssl_, fd_);
        }
    }
    ~Peer() {
        if (ssl_) {
            if (handshaken_ && !reset_) {
                SSL_set_quiet_shutdown(ssl_, 1);
                SSL_shutdown(ssl_);
            }
            SSL_free(ssl_);
        }
        if (!reset_) shutdown(fd_, SHUT_RDWR);
        close(fd_);
    }
    // Reads one complete request (headers and Content-Length body).
    std::optional<std::string> readRequest();
    bool send(std::string_view data) {
        if (!handshake()) return false;
        while (!data.empty()) {
            const long sent = ssl_ ? SSL_write(ssl_, data.data(), static_cast<int>(std::min<std::size_t>(data.size(), 1 << 20)))
                                   : ::send(fd_, data.data(), data.size(), MSG_NOSIGNAL);
            if (sent <= 0) return false;
            data.remove_prefix(static_cast<std::size_t>(sent));
        }
        return true;
    }
    // Aborts the connection with a TCP RST.
    void reset() {
        const linger abortive{1, 0};
        setsockopt(fd_, SOL_SOCKET, SO_LINGER, &abortive, sizeof(abortive));
        reset_ = true;
    }
    int fd() const { return fd_; }

private:
    bool handshake() {
        if (!ssl_ || handshaken_) return true;
        handshaken_ = SSL_accept(ssl_) == 1;
        return handshaken_;
    }
    long receive(char* buffer, std::size_t size) {
        if (!handshake()) return -1;
        if (ssl_) return SSL_read(ssl_, buffer, static_cast<int>(size));
        return ::recv(fd_, buffer, size, 0);
    }
    LoopbackServer& server_;
    int fd_;
    SSL* ssl_ = nullptr;
    bool handshaken_ = false, reset_ = false;
    std::string pending_;
};

// Serves accepted connections one after another on its own thread. Handlers
// block only in bounded socket I/O or in waitReleased(), which the destructor ends.
class LoopbackServer {
public:
    using Handler = std::function<void(Peer&, LoopbackServer&)>;
    explicit LoopbackServer(Handler handler, bool tls = false) : handler_(std::move(handler)) {
        if (tls) tls_ = std::make_unique<TlsContext>();
        listener_ = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
        const int one = 1;
        setsockopt(listener_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        socklen_t length = sizeof(address);
        if (listener_ < 0 || bind(listener_, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0 ||
            listen(listener_, 16) != 0 || getsockname(listener_, reinterpret_cast<sockaddr*>(&address), &length) != 0)
            throw std::runtime_error("loopback listen failed");
        port_ = ntohs(address.sin_port);
        worker_ = std::thread([this] { serve(); });
    }
    ~LoopbackServer() {
        release();
        stopping_ = true;
        shutdown(listener_, SHUT_RDWR);
        if (worker_.joinable()) worker_.join();
        close(listener_);
    }
    std::string url(const std::string& path = "/phpvpn.php") const {
        return std::string(tls_ ? "https" : "http") + "://127.0.0.1:" + std::to_string(port_) + path;
    }
    std::vector<std::string> requests() {
        std::lock_guard lock(mutex_);
        return requests_;
    }
    std::string lastRequest() {
        std::lock_guard lock(mutex_);
        return requests_.empty() ? std::string{} : requests_.back();
    }
    int accepted() const { return accepted_.load(); }
    // Handler side: announce that an exchange reached the phase under test.
    void signal() {
        std::lock_guard lock(mutex_);
        ++reached_;
        wake_.notify_all();
    }
    bool waitReached(int count = 1, std::chrono::milliseconds timeout = 5s) {
        std::unique_lock lock(mutex_);
        return wake_.wait_for(lock, timeout, [this, count] { return reached_ >= count; });
    }
    // Handler side: returns true once the test releases the server.
    bool waitReleased(std::chrono::milliseconds timeout) {
        std::unique_lock lock(mutex_);
        return wake_.wait_for(lock, timeout, [this] { return released_; });
    }
    void release() {
        std::lock_guard lock(mutex_);
        released_ = true;
        if (currentFd_ >= 0) shutdown(currentFd_, SHUT_RDWR);
        wake_.notify_all();
    }
    void record(std::string request) {
        std::lock_guard lock(mutex_);
        requests_.push_back(std::move(request));
    }

private:
    void serve() {
        for (;;) {
            const int fd = accept4(listener_, nullptr, nullptr, SOCK_CLOEXEC);
            if (fd < 0) {
                if (errno == EINTR) continue;
                return;
            }
            if (stopping_) {
                close(fd);
                return;
            }
            ++accepted_;
            Peer peer(*this, fd, tls_ ? tls_->get() : nullptr);
            {
                std::lock_guard lock(mutex_);
                currentFd_ = fd;
                if (released_) shutdown(fd, SHUT_RDWR);
            }
            handler_(peer, *this);
            std::lock_guard lock(mutex_);
            currentFd_ = -1;
        }
    }
    Handler handler_;
    std::unique_ptr<TlsContext> tls_;
    int listener_ = -1;
    int currentFd_ = -1;
    std::uint16_t port_ = 0;
    std::thread worker_;
    std::mutex mutex_;
    std::condition_variable wake_;
    std::vector<std::string> requests_;
    std::atomic<int> accepted_{0};
    std::atomic<bool> stopping_{false};
    int reached_ = 0;
    bool released_ = false;
};

std::optional<std::string> Peer::readRequest() {
    char block[16384];
    for (;;) {
        const auto headerEnd = pending_.find("\r\n\r\n");
        if (headerEnd != std::string::npos) {
            std::size_t length = 0;
            const auto head = lower(pending_.substr(0, headerEnd));
            if (const auto at = head.find("\r\ncontent-length:"); at != std::string::npos)
                length = std::stoull(head.substr(at + 17));
            if (pending_.size() >= headerEnd + 4 + length) {
                std::string request = pending_.substr(0, headerEnd + 4 + length);
                pending_.erase(0, request.size());
                server_.record(request);
                return request;
            }
        }
        const long received = receive(block, sizeof(block));
        if (received <= 0) return std::nullopt;
        pending_.append(block, static_cast<std::size_t>(received));
    }
}

std::string response(const std::string& status, const std::string& body, const std::string& extra = {},
                     std::optional<std::size_t> declaredLength = {}) {
    return "HTTP/1.1 " + status + "\r\nContent-Type: application/json\r\nContent-Length: " +
           std::to_string(declaredLength.value_or(body.size())) + "\r\n" + extra + "\r\n" + body;
}

// Answers every request on the connection with the same response (keep-alive).
LoopbackServer::Handler respond(std::string status, std::string body, std::string extra = {}) {
    return [status, body, extra](Peer& peer, LoopbackServer&) {
        while (peer.readRequest()) {
            if (!peer.send(response(status, body, extra))) return;
        }
    };
}

TransportSettings settingsFor(const std::string& url) {
    TransportSettings settings;
    settings.serverUrl = url;
    settings.httpHeaders = "X-Test: fixture";
    settings.responseTimeoutSeconds = 5;
    settings.maxResponseBodyBytes = 1000000;
    return settings;
}

std::unique_ptr<Transport> configured(const TransportSettings& settings) {
    auto created = createPlatformTransport();
    if (!created.ok()) throw std::runtime_error("platform transport creation failed");
    auto transport = std::move(created).value();
    if (!transport->configure(settings).ok()) throw std::runtime_error("transport configuration failed");
    return transport;
}

std::string bodyOf(const std::string& request) {
    const auto end = request.find("\r\n\r\n");
    return end == std::string::npos ? std::string{} : request.substr(end + 4);
}

// Header lines of a raw request: after the request line, up to the blank line.
std::vector<std::string> headerLines(const std::string& request) {
    std::vector<std::string> lines;
    const auto end = std::min(request.find("\r\n\r\n"), request.size());
    const auto first = request.find("\r\n");
    for (std::size_t at = first == std::string::npos ? end : first + 2; at < end;) {
        const auto next = std::min(request.find("\r\n", at), end);
        lines.push_back(request.substr(at, next - at));
        at = next + 2;
    }
    return lines;
}

// Values of one header in a raw request, leading whitespace removed.
std::vector<std::string> wireValues(const std::string& request, const std::string& name) {
    std::vector<std::string> values;
    for (const auto& line : headerLines(request)) {
        const auto colon = line.find(':');
        if (colon != std::string::npos && lower(line.substr(0, colon)) == name) {
            auto value = line.substr(colon + 1);
            while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) value.erase(0, 1);
            values.push_back(value);
        }
    }
    return values;
}

std::vector<std::string> headerNames(const std::string& request) {
    std::vector<std::string> names;
    for (const auto& line : headerLines(request)) names.push_back(lower(line.substr(0, line.find(':'))));
    std::sort(names.begin(), names.end());
    return names;
}

long long elapsedMs(SteadyClock::time_point since) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(SteadyClock::now() - since).count();
}

// A listener whose accept queue is full: the kernel drops further SYNs, so a
// connect stays in progress until the client gives up. Loopback only.
class FullBacklogListener {
public:
    FullBacklogListener() {
        listener_ = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        socklen_t length = sizeof(address);
        if (listener_ < 0 || bind(listener_, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0 ||
            listen(listener_, 0) != 0 || getsockname(listener_, reinterpret_cast<sockaddr*>(&address), &length) != 0)
            throw std::runtime_error("loopback listen failed");
        port_ = ntohs(address.sin_port);
        filler_ = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
        if (filler_ < 0 || connect(filler_, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0)
            throw std::runtime_error("backlog filler failed");
    }
    ~FullBacklogListener() {
        close(filler_);
        close(listener_);
    }
    std::string url() const { return "http://127.0.0.1:" + std::to_string(port_) + "/phpvpn.php"; }

private:
    int listener_ = -1, filler_ = -1;
    std::uint16_t port_ = 0;
};

// Sets or removes an environment variable for one scope.
class ScopedEnvironment {
public:
    ScopedEnvironment(const char* name, const char* value) : name_(name) {
        if (const char* old = std::getenv(name)) previous_ = old;
        if (value) setenv(name, value, 1);
        else unsetenv(name);
    }
    ~ScopedEnvironment() {
        if (previous_) setenv(name_.c_str(), previous_->c_str(), 1);
        else unsetenv(name_.c_str());
    }

private:
    std::string name_;
    std::optional<std::string> previous_;
};

}  // namespace

PHPVPN_TEST(CurlTransport, HealthInfoAndDataBodiesWithActionUrlAndContentType) {
    LoopbackServer server(respond("200 OK", "{\"ok\":true}"));
    auto transport = configured(settingsFor(server.url()));
    for (const auto action : {HttpAction::health, HttpAction::info, HttpAction::data}) {
        const std::string sent = std::string("{\"action\":\"") + httpActionName(action) + "\"}";
        auto result = transport->post(action, sent);
        PHPVPN_REQUIRE_OK(result);
        PHPVPN_CHECK_EQ(result.value().statusCode, 200);
        PHPVPN_CHECK_EQ(result.value().body, std::string("{\"ok\":true}"));
        PHPVPN_CHECK_EQ(result.value().contentType, std::string("application/json"));
        PHPVPN_CHECK_EQ(result.responseBodyBytesRead, 11U);
        PHPVPN_CHECK(!result.diagnostic);
        PHPVPN_CHECK(!result.receivedHttpStatus);
        const auto request = server.lastRequest();
        PHPVPN_CHECK(request.starts_with(std::string("POST /phpvpn.php?action=") + httpActionName(action) + " HTTP/1.1\r\n"));
        PHPVPN_CHECK_EQ(bodyOf(request), sent);
        PHPVPN_CHECK_EQ(wireValues(request, "content-type"), std::vector<std::string>{httpActionContentType(action)});
    }
    // All three exchanges shared one kept-alive connection.
    PHPVPN_CHECK_EQ(server.accepted(), 1);
}

PHPVPN_TEST(CurlTransport, ImplicitRootPathAndEmptyBody) {
    LoopbackServer server(respond("200 OK", ""));
    auto transport = configured(settingsFor(server.url("")));
    auto result = transport->post(HttpAction::health, "");
    PHPVPN_REQUIRE_OK(result);
    PHPVPN_CHECK_EQ(result.value().body, std::string{});
    PHPVPN_CHECK_EQ(result.responseBodyBytesRead, 0U);
    const auto request = server.lastRequest();
    PHPVPN_CHECK(request.starts_with("POST /?action=health HTTP/1.1\r\n"));
    PHPVPN_CHECK_EQ(wireValues(request, "content-length"), std::vector<std::string>{"0"});
    PHPVPN_CHECK_EQ(bodyOf(request), std::string{});
}

PHPVPN_TEST(CurlTransport, UnsuccessfulHttpStatusesReturnTheBodyLikeWinHttp) {
    for (const std::string status : {"503 Service Unavailable", "404 Not Found", "500 Internal Server Error"}) {
        LoopbackServer server(respond(status, std::string(137, 'x')));
        auto transport = configured(settingsFor(server.url()));
        auto result = transport->post(HttpAction::data, "{}");
        PHPVPN_REQUIRE_OK(result);
        PHPVPN_CHECK_EQ(result.value().statusCode, std::stoi(status));
        PHPVPN_CHECK_EQ(result.value().body.size(), 137U);
        PHPVPN_CHECK_EQ(result.responseBodyBytesRead, 137U);
    }
}

PHPVPN_TEST(CurlTransport, BodyLimitExactPassesAndOneMoreByteFails) {
    for (const std::size_t size : {1000u, 1001u}) {
        LoopbackServer server(respond("200 OK", std::string(size, 'b')));
        auto settings = settingsFor(server.url());
        settings.maxResponseBodyBytes = 1000;
        auto transport = configured(settings);
        auto result = transport->post(HttpAction::data, "{}");
        if (size == 1000) {
            PHPVPN_REQUIRE_OK(result);
            PHPVPN_CHECK_EQ(result.value().body.size(), 1000U);
            PHPVPN_CHECK_EQ(result.responseBodyBytesRead, 1000U);
        } else {
            PHPVPN_REQUIRE_ERROR(result, ErrorCode::transport_response_too_large);
            // The whole over-limit block is accounted, never truncated to the cap.
            PHPVPN_CHECK_EQ(result.responseBodyBytesRead, 1001U);
            PHPVPN_CHECK(!result.diagnostic);
        }
    }
    LoopbackServer server(respond("200 OK", std::string(200000, 'b')));
    auto settings = settingsFor(server.url());
    settings.maxResponseBodyBytes = 50000;
    auto transport = configured(settings);
    auto result = transport->post(HttpAction::data, "{}");
    PHPVPN_REQUIRE_ERROR(result, ErrorCode::transport_response_too_large);
    PHPVPN_CHECK(result.responseBodyBytesRead > 50000);
    PHPVPN_CHECK(result.responseBodyBytesRead < 200000);
    // The transport stays usable after the aborted transfer.
    settings.maxResponseBodyBytes = 1000000;
    PHPVPN_REQUIRE_OK(transport->configure(settings));
    auto again = transport->post(HttpAction::data, "{}");
    PHPVPN_REQUIRE_OK(again);
    PHPVPN_CHECK_EQ(again.value().body.size(), 200000U);
}

PHPVPN_TEST(CurlTransport, ContentLengthMismatchAndResetKeepByteAccounting) {
    {
        // Declared 100 bytes more, then the connection closes: incomplete body.
        LoopbackServer server([](Peer& peer, LoopbackServer&) {
            if (peer.readRequest()) peer.send(response("200 OK", std::string(65536, 'x'), "Connection: close\r\n", 65636));
        });
        auto transport = configured(settingsFor(server.url()));
        auto result = transport->post(HttpAction::data, "{}");
        PHPVPN_REQUIRE_ERROR(result, ErrorCode::transport_receive_failed);
        PHPVPN_CHECK_EQ(result.responseBodyBytesRead, 65536U);
    }
    {
        // Declared more and the server stays silent: the deadline ends it.
        LoopbackServer server([](Peer& peer, LoopbackServer& self) {
            if (peer.readRequest() && peer.send(response("200 OK", std::string(65536, 'x'), {}, 65636)))
                self.waitReleased(5s);
        });
        auto settings = settingsFor(server.url());
        settings.responseTimeoutSeconds = 1;
        auto transport = configured(settings);
        auto result = transport->post(HttpAction::data, "{}");
        PHPVPN_REQUIRE_ERROR(result, ErrorCode::transport_timeout);
        PHPVPN_CHECK_EQ(result.responseBodyBytesRead, 65536U);
    }
    {
        // TCP reset after part of the body: a terminated connection.
        LoopbackServer server([](Peer& peer, LoopbackServer& self) {
            if (peer.readRequest() && peer.send(response("200 OK", std::string(65536, 'x'), {}, 65636))) {
                self.waitReleased(200ms);
                peer.reset();
            }
        });
        auto transport = configured(settingsFor(server.url()));
        auto result = transport->post(HttpAction::data, "{}");
        PHPVPN_REQUIRE_ERROR(result, ErrorCode::transport_connect_failed);
        PHPVPN_CHECK_EQ(result.responseBodyBytesRead, 65536U);
    }
}

PHPVPN_TEST(CurlTransport, RedirectIsReturnedAndNeverFollowed) {
    LoopbackServer target(respond("200 OK", "{}"));
    LoopbackServer server(respond("302 Found", "moved", "Location: " + target.url() + "\r\n"));
    auto transport = configured(settingsFor(server.url()));
    auto result = transport->post(HttpAction::info, "{}");
    PHPVPN_REQUIRE_OK(result);
    PHPVPN_CHECK_EQ(result.value().statusCode, 302);
    PHPVPN_CHECK_EQ(result.value().body, std::string("moved"));
    PHPVPN_CHECK(target.requests().empty());
    PHPVPN_CHECK_EQ(server.requests().size(), 1U);
}

PHPVPN_TEST(CurlTransport, ProxyEnvironmentIsIgnored) {
    LoopbackServer proxy(respond("200 OK", "proxy"));
    const std::string proxyUrl = proxy.url("");
    std::vector<std::unique_ptr<ScopedEnvironment>> environment;
    for (const char* name : {"http_proxy", "HTTP_PROXY", "https_proxy", "HTTPS_PROXY", "all_proxy", "ALL_PROXY"})
        environment.push_back(std::make_unique<ScopedEnvironment>(name, proxyUrl.c_str()));
    environment.push_back(std::make_unique<ScopedEnvironment>("no_proxy", nullptr));
    environment.push_back(std::make_unique<ScopedEnvironment>("NO_PROXY", nullptr));
    for (const bool tls : {false, true}) {
        LoopbackServer server(respond("200 OK", "direct"), tls);
        auto transport = configured(settingsFor(server.url()));
        auto result = transport->post(HttpAction::health, "{}");
        PHPVPN_REQUIRE_OK(result);
        PHPVPN_CHECK_EQ(result.value().body, std::string("direct"));
        PHPVPN_CHECK(server.lastRequest().starts_with("POST /phpvpn.php?action=health HTTP/1.1\r\n"));
    }
    PHPVPN_CHECK_EQ(proxy.accepted(), 0);
}

PHPVPN_TEST(CurlTransport, ExactRequestHeaderSetMatchesWinHttp) {
    // WinHTTP sends Host, Content-Type, the configured block, Content-Length and
    // Connection: Keep-Alive; no Accept, Accept-Encoding, Expect or User-Agent.
    for (const auto action : {HttpAction::health, HttpAction::info, HttpAction::data}) {
        const std::vector<std::string> blocks = {kDefaultHttpHeaders, "X-Test: own", "User-Agent: own-agent",
            "X-Order: first\r\nX-Trace: second\r\nX-Last: third", "X-Test: lf\nX-Empty:\n",
            "X-Utf8: \xc5\xbe\xc3\xa1\xf0\x9f\x98\x80", "Cookie: a=1; b=2", "Accept: text/plain"};
        for (const auto& block : blocks) {
            LoopbackServer server(respond("200 OK", "abc"));
            auto settings = settingsFor(server.url());
            settings.httpHeaders = block;
            auto transport = configured(settings);
            const std::string body("a\0b\r\n", 5);
            const auto result = transport->postDiagnostic(action, body);
            PHPVPN_REQUIRE_OK(result);
            const auto wire = server.lastRequest();
            PHPVPN_CHECK(wire.starts_with(std::string("POST /phpvpn.php?action=") + httpActionName(action) + " HTTP/1.1\r\n"));
            PHPVPN_CHECK_EQ(bodyOf(wire), body);
            const auto application = buildApplicationRequestHeaders(httpActionContentType(action), *normalizeHttpHeaders(block));
            const auto expected = "POST / HTTP/1.1\r\n" + application + "\r\nHost: 127.0.0.1\r\nContent-Length: 5\r\nConnection: Keep-Alive\r\n\r\n";
            PHPVPN_CHECK_EQ(headerNames(wire), headerNames(expected));
            for (const auto& name : headerNames(expected)) {
                if (name == "host") continue;
                PHPVPN_CHECK_EQ(wireValues(wire, name), wireValues(expected, name));
            }
            PHPVPN_CHECK_EQ(wireValues(wire, "host"), std::vector<std::string>{server.url("").substr(7)});
            PHPVPN_CHECK_EQ(result.diagnostic.has_value(), action != HttpAction::data);
            if (result.diagnostic) {
                const auto& captured = result.diagnostic->request.prefix;
                PHPVPN_CHECK_EQ(result.diagnostic->requestHeadersSource, RequestHeadersSource::winhttp);
                PHPVPN_CHECK_EQ(bodyOf(captured), body);
                for (const auto& name : headerNames("POST / HTTP/1.1\r\n" + application + "\r\n\r\n"))
                    PHPVPN_CHECK_EQ(wireValues(captured, name), wireValues(expected, name));
            }
        }
    }
    // A body above curl's 1 MiB Expect threshold is still sent without 100-continue.
    LoopbackServer server(respond("200 OK", "{}"));
    auto transport = configured(settingsFor(server.url()));
    const std::string large(2 * 1024 * 1024, 'l');
    PHPVPN_REQUIRE_OK(transport->post(HttpAction::data, large));
    const auto wire = server.lastRequest();
    PHPVPN_CHECK(wireValues(wire, "expect").empty());
    PHPVPN_CHECK_EQ(bodyOf(wire).size(), large.size());
}

PHPVPN_TEST(CurlTransport, ConfigureValidatesBeforeAnythingIsSent) {
    LoopbackServer server(respond("200 OK", "{}"));
    auto created = createPlatformTransport();
    PHPVPN_REQUIRE_OK(created);
    auto transport = std::move(created).value();
    PHPVPN_REQUIRE_ERROR(transport->post(HttpAction::health, "{}"), ErrorCode::transport_unavailable);
    auto settings = settingsFor(server.url());
    for (const auto seconds : {0LL, -1LL, 2147484LL, 9223372036854775807LL}) {
        settings.responseTimeoutSeconds = seconds;
        PHPVPN_CHECK_EQ(transport->configure(settings).code(), ErrorCode::config_invalid_limit);
    }
    settings.responseTimeoutSeconds = 2147483;
    PHPVPN_REQUIRE_OK(transport->configure(settings));
    settings.maxResponseBodyBytes = 0;
    PHPVPN_CHECK_EQ(transport->configure(settings).code(), ErrorCode::config_invalid_limit);
    PHPVPN_REQUIRE_ERROR(transport->post(HttpAction::health, "{}"), ErrorCode::transport_unavailable);
    settings = settingsFor("ftp://127.0.0.1/");
    PHPVPN_CHECK_EQ(transport->configure(settings).code(), ErrorCode::config_invalid_url);
    for (const auto bad : {"", "X: ok\r\n\r\nGET / HTTP/1.1", "Content-Type: bad", "content-length: 0", "CONNECTION: close",
                           "hOsT: other", "X: first\nx: second", "Transfer-Encoding: chunked"}) {
        settings = settingsFor(server.url());
        settings.httpHeaders = bad;
        PHPVPN_CHECK_EQ(transport->configure(settings).code(), ErrorCode::config_invalid_value);
        PHPVPN_REQUIRE_ERROR(transport->post(HttpAction::data, "body"), ErrorCode::transport_unavailable);
    }
    settings.httpHeaders = "X: \xff";
    PHPVPN_CHECK_EQ(transport->configure(settings).code(), ErrorCode::config_encoding_error);
    PHPVPN_CHECK(server.requests().empty());
    PHPVPN_CHECK_EQ(server.accepted(), 0);
}

PHPVPN_TEST(CurlTransport, DnsFailureMapsToDnsError) {
    // A 64 octet label is rejected by the resolver without any network query.
    auto transport = configured(settingsFor("http://" + std::string(64, 'x') + ".invalid/phpvpn.php"));
    PHPVPN_REQUIRE_ERROR(transport->post(HttpAction::health, "{}"), ErrorCode::transport_dns_failed);
}

PHPVPN_TEST(CurlTransport, RefusedConnectionMapsToConnectError) {
    int probe = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    socklen_t length = sizeof(address);
    bind(probe, reinterpret_cast<sockaddr*>(&address), sizeof(address));
    getsockname(probe, reinterpret_cast<sockaddr*>(&address), &length);
    close(probe);  // the port is now closed, a connect gets RST
    auto transport = configured(settingsFor("http://127.0.0.1:" + std::to_string(ntohs(address.sin_port)) + "/"));
    PHPVPN_REQUIRE_ERROR(transport->post(HttpAction::health, "{}"), ErrorCode::transport_connect_failed);
}

PHPVPN_TEST(CurlTransport, OneAbsoluteDeadlineCoversConnectTlsHeadersAndTrickleBody) {
    enum class Phase { connect, tls, headers, body };
    for (const auto phase : {Phase::connect, Phase::tls, Phase::headers, Phase::body}) {
        std::unique_ptr<FullBacklogListener> blackhole;
        std::unique_ptr<LoopbackServer> server;
        std::string url;
        if (phase == Phase::connect) {
            blackhole = std::make_unique<FullBacklogListener>();
            url = blackhole->url();
        } else {
            server = std::make_unique<LoopbackServer>([phase](Peer& peer, LoopbackServer& self) {
                if (phase == Phase::tls) { self.waitReleased(5s); return; }
                if (!peer.readRequest()) return;
                if (phase == Phase::headers) { self.waitReleased(5s); return; }
                if (!peer.send(response("200 OK", std::string(8192, 'x'), {}, 65536))) return;
                for (int i = 0; i < 100; ++i) {
                    if (!peer.send(std::string(512, 'x')) || self.waitReleased(150ms)) return;
                }
            }, phase == Phase::tls);
            url = server->url();
        }
        auto settings = settingsFor(url);
        settings.responseTimeoutSeconds = 1;
        auto transport = configured(settings);
        const auto start = SteadyClock::now();
        auto result = transport->post(HttpAction::data, "{}");
        const auto elapsed = elapsedMs(start);
        std::cout << "    deadline phase=" << static_cast<int>(phase) << " elapsed=" << elapsed << " ms\n";
        PHPVPN_REQUIRE_ERROR(result, ErrorCode::transport_timeout);
        PHPVPN_CHECK(elapsed >= 950);
        PHPVPN_CHECK(elapsed < 1500);
        if (phase == Phase::body) {
            PHPVPN_CHECK(result.responseBodyBytesRead > 8192);
            PHPVPN_CHECK(result.responseBodyBytesRead < 65536);
        } else {
            PHPVPN_CHECK_EQ(result.responseBodyBytesRead, 0U);
        }
    }
}

PHPVPN_TEST(CurlTransport, CancellationInterruptsEveryPhasePromptlyAndTransportIsReusable) {
    enum class Phase { connect, tls, headers, body };
    auto created = createPlatformTransport();
    PHPVPN_REQUIRE_OK(created);
    auto transport = std::move(created).value();
    for (const auto phase : {Phase::connect, Phase::tls, Phase::headers, Phase::body}) {
        std::unique_ptr<FullBacklogListener> blackhole;
        std::unique_ptr<LoopbackServer> server;
        std::string url;
        if (phase == Phase::connect) {
            blackhole = std::make_unique<FullBacklogListener>();
            url = blackhole->url();
        } else {
            server = std::make_unique<LoopbackServer>([phase](Peer& peer, LoopbackServer& self) {
                if (phase == Phase::tls) { self.signal(); self.waitReleased(10s); return; }
                if (!peer.readRequest()) return;
                if (phase == Phase::headers) { self.signal(); self.waitReleased(10s); return; }
                if (!peer.send(response("200 OK", std::string(20000, 'x'), {}, 65536))) return;
                self.signal();
                self.waitReleased(10s);
            }, phase == Phase::tls);
            url = server->url();
        }
        auto settings = settingsFor(url);
        settings.responseTimeoutSeconds = 30;
        PHPVPN_REQUIRE_OK(transport->configure(settings));
        auto pending = std::async(std::launch::async, [&] { return transport->post(HttpAction::data, "{}"); });
        if (server) PHPVPN_CHECK(server->waitReached());
        // Let the client settle in the blocking phase (SYN retransmit, TLS read, body read).
        std::this_thread::sleep_for(200ms);
        const auto start = SteadyClock::now();
        std::thread first([&] { transport->cancelCurrentRequest(); });
        std::thread second([&] { transport->cancelCurrentRequest(); });
        first.join();
        second.join();
        auto result = pending.get();
        const auto elapsed = elapsedMs(start);
        std::cout << "    cancel phase=" << static_cast<int>(phase) << " returned after " << elapsed << " ms\n";
        PHPVPN_CHECK(elapsed < 500);
        PHPVPN_REQUIRE_ERROR(result, ErrorCode::cancelled);
        PHPVPN_CHECK_EQ(result.responseBodyBytesRead, phase == Phase::body ? 20000U : 0U);
        PHPVPN_CHECK(!result.diagnostic);
    }
    // A cancellation without a request in flight does not affect the next one.
    transport->cancelCurrentRequest();
    LoopbackServer server(respond("200 OK", std::string(42, 'r')));
    PHPVPN_REQUIRE_OK(transport->configure(settingsFor(server.url())));
    auto response = transport->post(HttpAction::data, "{}");
    PHPVPN_REQUIRE_OK(response);
    PHPVPN_CHECK_EQ(response.responseBodyBytesRead, 42U);
}

PHPVPN_TEST(CurlTransport, ReuseAfterCancelAndTimeoutOnTheSameConfiguration) {
    // The first two connections stall after the request until the client
    // closes them, later connections answer.
    std::atomic<int> connections{0};
    LoopbackServer server([&connections](Peer& peer, LoopbackServer& self) {
        const int index = connections++;
        if (index < 2) {
            if (peer.readRequest()) {
                self.signal();
                peer.readRequest();
            }
            return;
        }
        while (peer.readRequest()) {
            if (!peer.send(response("200 OK", "fine"))) return;
        }
    });
    auto settings = settingsFor(server.url());
    settings.responseTimeoutSeconds = 1;
    auto transport = configured(settings);
    auto pending = std::async(std::launch::async, [&] { return transport->post(HttpAction::info, "{}"); });
    PHPVPN_CHECK(server.waitReached());
    transport->cancelCurrentRequest();
    PHPVPN_REQUIRE_ERROR(pending.get(), ErrorCode::cancelled);
    // The cancelled connection is closed, the next exchange opens a new one and times out.
    PHPVPN_REQUIRE_ERROR(transport->post(HttpAction::info, "{}"), ErrorCode::transport_timeout);
    for (int i = 0; i < 3; ++i) {
        auto result = transport->post(HttpAction::info, "{}");
        PHPVPN_REQUIRE_OK(result);
        PHPVPN_CHECK_EQ(result.value().body, std::string("fine"));
    }
    PHPVPN_CHECK_EQ(server.accepted(), 3);
}

PHPVPN_TEST(CurlTransport, DiagnosticCaptureOnlyForWizardNonDataExchanges) {
    for (const auto action : {HttpAction::health, HttpAction::info, HttpAction::data}) {
        for (const bool enabled : {false, true}) {
            LoopbackServer server(respond("503 Service Unavailable", std::string(65537, 'x')));
            auto settings = settingsFor(server.url());
            settings.captureWizardHttpDiagnostics = enabled;
            auto transport = configured(settings);
            auto result = transport->post(action, "{}");
            PHPVPN_REQUIRE_OK(result);
            PHPVPN_CHECK_EQ(result.diagnostic.has_value(), enabled && action != HttpAction::data);
            if (!result.diagnostic) continue;
            const auto& d = *result.diagnostic;
            PHPVPN_CHECK_EQ(d.action, action);
            PHPVPN_CHECK(d.responseComplete);
            PHPVPN_CHECK(d.request.prefix.starts_with("POST " + server.url() + "?action=" + httpActionName(action) + " HTTP/1.1\r\n"));
            PHPVPN_CHECK(d.request.prefix.ends_with("\r\n\r\n{}"));
            PHPVPN_CHECK(d.request.prefix.find("Content-Type: " + std::string(httpActionContentType(action))) != std::string::npos);
            PHPVPN_CHECK_EQ(d.requestHeadersSource, RequestHeadersSource::winhttp);
            PHPVPN_CHECK_EQ(d.responseHeadersSource, ResponseHeadersSource::winhttp);
            PHPVPN_CHECK(d.response.prefix.starts_with("HTTP/1.1 503 Service Unavailable\r\n"));
            PHPVPN_CHECK_EQ(d.response.prefix.size(), 64512U);
            PHPVPN_CHECK_EQ(d.response.suffix, std::string(1024, 'x'));
            PHPVPN_CHECK(d.response.originalSizeBytes > 65537);
            PHPVPN_CHECK(!result.responseProcessingFailed);
            PHPVPN_CHECK_EQ(result.receivedHttpStatus, std::optional<int>(503));
        }
    }
    // postDiagnostic is the one-exchange opt-in, still never for data.
    LoopbackServer server(respond("200 OK", "{}"));
    auto transport = configured(settingsFor(server.url()));
    PHPVPN_CHECK(transport->postDiagnostic(HttpAction::health, "{}").diagnostic.has_value());
    PHPVPN_CHECK(!transport->postDiagnostic(HttpAction::data, "{}").diagnostic.has_value());
    PHPVPN_CHECK(!transport->post(HttpAction::health, "{}").diagnostic.has_value());
}

PHPVPN_TEST(CurlTransport, DiagnosticRequestBoundsDuplicatesAndResponseTrailers) {
    for (const std::size_t requestSize : {0u, 1u, 65536u, 65537u}) {
        LoopbackServer server(respond("200 OK", std::string(requestSize % 2, 'y'), "X-Dup: first\r\nX-Dup: second\r\n"));
        auto settings = settingsFor(server.url(""));
        settings.captureWizardHttpDiagnostics = true;
        auto transport = configured(settings);
        std::string sent(requestSize, 's');
        if (sent.size() > 10) sent.replace(2, 8, std::string("\0\xff\r\n...\n", 8));
        const auto result = transport->post(HttpAction::info, sent);
        PHPVPN_REQUIRE_OK(result);
        PHPVPN_CHECK(result.diagnostic.has_value());
        if (!result.diagnostic) continue;
        const auto& d = *result.diagnostic;
        PHPVPN_CHECK_EQ(bodyOf(server.lastRequest()), sent);
        PHPVPN_CHECK(d.request.prefix.starts_with("POST " + settings.serverUrl + "/?action=info HTTP/1.1\r\n"));
        const auto start = d.request.prefix.find("\r\n\r\n") + 4;
        const auto full = d.request.prefix.substr(0, start) + sent;
        PHPVPN_CHECK_EQ(d.request.originalSizeBytes, full.size());
        PHPVPN_CHECK_EQ(d.request.prefix, full.substr(0, full.size() <= 65536 ? full.size() : 64512));
        PHPVPN_CHECK_EQ(d.request.suffix, full.size() <= 65536 ? std::string{} : full.substr(full.size() - 1024));
        PHPVPN_CHECK(d.response.prefix.find("X-Dup: first\r\nX-Dup: second") != std::string::npos);
        PHPVPN_CHECK(d.responseComplete && !d.response.truncated());
    }
    LoopbackServer server([](Peer& peer, LoopbackServer&) {
        if (!peer.readRequest()) return;
        peer.send("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nTransfer-Encoding: chunked\r\nTrailer: X-Checksum\r\n\r\n"
                  "5\r\nhello\r\n6\r\n world\r\n0\r\nX-Checksum: abc\r\n\r\n");
    });
    auto settings = settingsFor(server.url());
    settings.captureWizardHttpDiagnostics = true;
    auto transport = configured(settings);
    const auto result = transport->post(HttpAction::health, "{}");
    PHPVPN_REQUIRE_OK(result);
    PHPVPN_CHECK_EQ(result.value().body, std::string("hello world"));
    PHPVPN_CHECK_EQ(result.responseBodyBytesRead, 11U);
    PHPVPN_CHECK(result.diagnostic.has_value());
    if (result.diagnostic) {
        const auto& prefix = result.diagnostic->response.prefix;
        PHPVPN_CHECK(prefix.starts_with("HTTP/1.1 200 OK\r\n"));
        PHPVPN_CHECK(prefix.ends_with("Trailer: X-Checksum\r\n[Response trailers]\r\nX-Checksum: abc\r\n\r\nhello world"));
    }
}

PHPVPN_TEST(CurlTransport, ProcessingFailurePublishesButTimeoutAndCancelDoNot) {
    {
        LoopbackServer server(respond("200 OK", std::string(32768, 'x')));
        auto settings = settingsFor(server.url());
        settings.maxResponseBodyBytes = 100;
        settings.captureWizardHttpDiagnostics = true;
        auto transport = configured(settings);
        auto result = transport->post(HttpAction::health, "{}");
        PHPVPN_REQUIRE_ERROR(result, ErrorCode::transport_response_too_large);
        PHPVPN_CHECK(result.responseProcessingFailed);
        PHPVPN_CHECK_EQ(result.receivedHttpStatus, std::optional<int>(200));
        PHPVPN_CHECK(result.diagnostic.has_value());
        if (result.diagnostic) {
            PHPVPN_CHECK(!result.diagnostic->responseComplete);
            const auto& r = result.diagnostic->response;
            PHPVPN_CHECK_EQ(r.originalSizeBytes, r.prefix.find("\r\n\r\n") + 4 + result.responseBodyBytesRead);
        }
    }
    {
        // A non-ASCII Content-Type fails before any body byte is read.
        LoopbackServer server([](Peer& peer, LoopbackServer&) {
            if (peer.readRequest())
                peer.send("HTTP/1.1 200 OK\r\nContent-Type: application/json; x=\xc3\xa9\r\nContent-Length: 2\r\n\r\n{}");
        });
        auto settings = settingsFor(server.url());
        settings.captureWizardHttpDiagnostics = true;
        auto transport = configured(settings);
        auto result = transport->post(HttpAction::info, "{}");
        PHPVPN_REQUIRE_ERROR(result, ErrorCode::transport_receive_failed);
        PHPVPN_CHECK(result.responseProcessingFailed);
        PHPVPN_CHECK_EQ(result.receivedHttpStatus, std::optional<int>(200));
        PHPVPN_CHECK_EQ(result.responseBodyBytesRead, 0U);
        PHPVPN_CHECK(result.diagnostic.has_value());
        // Without capture nothing beyond the error is reported.
        settings.captureWizardHttpDiagnostics = false;
        PHPVPN_REQUIRE_OK(transport->configure(settings));
        auto plain = transport->post(HttpAction::info, "{}");
        PHPVPN_REQUIRE_ERROR(plain, ErrorCode::transport_receive_failed);
        PHPVPN_CHECK(!plain.diagnostic && !plain.responseProcessingFailed && !plain.receivedHttpStatus);
    }
    {
        LoopbackServer server([](Peer& peer, LoopbackServer& self) {
            // Declares more than it sends and stalls until the client closes.
            if (peer.readRequest() && peer.send(response("200 OK", std::string(16384, 'x'), {}, 16484))) {
                self.signal();
                peer.readRequest();
            }
        });
        auto settings = settingsFor(server.url());
        settings.responseTimeoutSeconds = 1;
        settings.captureWizardHttpDiagnostics = true;
        auto transport = configured(settings);
        auto result = transport->post(HttpAction::health, "{}");
        PHPVPN_REQUIRE_ERROR(result, ErrorCode::transport_timeout);
        PHPVPN_CHECK(!result.diagnostic);
        PHPVPN_CHECK_EQ(result.responseBodyBytesRead, 16384U);
        settings.responseTimeoutSeconds = 30;
        PHPVPN_REQUIRE_OK(transport->configure(settings));
        auto pending = std::async(std::launch::async, [&] { return transport->post(HttpAction::health, "{}"); });
        PHPVPN_CHECK(server.waitReached(2));
        std::this_thread::sleep_for(100ms);
        transport->cancelCurrentRequest();
        auto cancelled = pending.get();
        PHPVPN_REQUIRE_ERROR(cancelled, ErrorCode::cancelled);
        PHPVPN_CHECK(!cancelled.diagnostic);
        PHPVPN_CHECK_EQ(cancelled.responseBodyBytesRead, 16384U);
    }
}

PHPVPN_TEST(CurlTransport, TlsTrustAnyAcceptsSelfSignedAndVerificationRejectsIt) {
    LoopbackServer server(respond("200 OK", "secure"), true);
    auto settings = settingsFor(server.url());
    settings.trustAnyTlsCertificate = true;
    auto transport = configured(settings);
    auto trusted = transport->post(HttpAction::health, "{}");
    PHPVPN_REQUIRE_OK(trusted);
    PHPVPN_CHECK_EQ(trusted.value().body, std::string("secure"));
    PHPVPN_CHECK(server.lastRequest().starts_with("POST /phpvpn.php?action=health HTTP/1.1\r\n"));
    settings.trustAnyTlsCertificate = false;
    PHPVPN_REQUIRE_OK(transport->configure(settings));
    auto verified = transport->post(HttpAction::health, "{}");
    PHPVPN_REQUIRE_ERROR(verified, ErrorCode::transport_tls_failed);
    PHPVPN_CHECK_EQ(verified.responseBodyBytesRead, 0U);
    PHPVPN_CHECK_EQ(server.requests().size(), 1U);
}

}  // namespace phpvpn
