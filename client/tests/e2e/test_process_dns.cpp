// Windows 11 opt-in only. DNS configuration is confined to this executable.
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <windns.h>
#include <iphlpapi.h>
#include "harness.h"
#include "platform/iface/transport.h"
#include <atomic>
#include <chrono>
#include <future>
#include <stdexcept>
#include <thread>
#include <vector>

namespace phpvpn {
namespace {
using namespace std::chrono_literals;
struct Socket {
    SOCKET value = INVALID_SOCKET;
    ~Socket() { if (value != INVALID_SOCKET) closesocket(value); }
};
struct Winsock {
    Winsock() {
        WSADATA data{};
        if (WSAStartup(MAKEWORD(2, 2), &data)) throw std::runtime_error("WSAStartup failed");
    }
    ~Winsock() { WSACleanup(); }
};
void requireUnusedHostDnsAddress() {
    ULONG bytes = 16384;
    std::vector<unsigned char> storage(bytes);
    auto* adapters = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(storage.data());
    ULONG result = GetAdaptersAddresses(AF_UNSPEC, 0, nullptr, adapters, &bytes);
    if (result == ERROR_BUFFER_OVERFLOW) {
        storage.resize(bytes);
        adapters = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(storage.data());
        result = GetAdaptersAddresses(AF_UNSPEC, 0, nullptr, adapters, &bytes);
    }
    if (result != NO_ERROR) throw std::runtime_error("cannot verify host DNS isolation");
    for (auto* adapter = adapters; adapter; adapter = adapter->Next) {
        for (auto* dns = adapter->FirstDnsServerAddress; dns; dns = dns->Next) {
            if (dns->Address.lpSockaddr && dns->Address.lpSockaddr->sa_family == AF_INET &&
                reinterpret_cast<sockaddr_in*>(dns->Address.lpSockaddr)->sin_addr.s_addr == htonl(0x7f000002))
                throw std::runtime_error("127.0.0.2 is configured as host DNS, refusing test listener");
        }
    }
}
class LocalServers {
public:
    LocalServers() {
        requireUnusedHostDnsAddress();
        dns.value = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        http.value = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        const DWORD enabled = 1;
        if (setsockopt(dns.value, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char*>(&enabled), sizeof(enabled)))
            throw std::runtime_error("exclusive loopback DNS setup failed");
        sockaddr_in dnsAddress{};
        dnsAddress.sin_family = AF_INET;
        dnsAddress.sin_addr.s_addr = htonl(0x7f000002);
        dnsAddress.sin_port = htons(53);
        if (bind(dns.value, reinterpret_cast<sockaddr*>(&dnsAddress), sizeof(dnsAddress)))
            throw std::runtime_error("exclusive 127.0.0.2:53 unavailable, no existing DNS listener will be replaced");
        dnsPort = 53;
        httpPort = bindLocal(http.value);
        if (listen(http.value, 4)) throw std::runtime_error("listen failed");
        dnsWorker = std::thread([this] { serveDns(); });
        httpWorker = std::thread([this] { serveHttp(); });
    }
    ~LocalServers() {
        stop = true;
        if (dnsWorker.joinable()) dnsWorker.join();
        if (httpWorker.joinable()) httpWorker.join();
        // Socket destructors precede Winsock cleanup. No host DNS is changed.
    }
    unsigned short dnsPort = 0, httpPort = 0;
    std::atomic<unsigned> queries{0};
    std::atomic<bool> answer{true};
private:
    static unsigned short bindLocal(SOCKET socket) {
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (socket == INVALID_SOCKET || bind(socket, reinterpret_cast<sockaddr*>(&address), sizeof(address)))
            throw std::runtime_error("exclusive loopback bind failed");
        int length = sizeof(address);
        if (getsockname(socket, reinterpret_cast<sockaddr*>(&address), &length)) throw std::runtime_error("getsockname failed");
        return ntohs(address.sin_port);
    }
    bool ready(SOCKET socket) {
        fd_set reads;
        FD_ZERO(&reads);
        FD_SET(socket, &reads);
        timeval timeout{0, 100000};
        return select(0, &reads, nullptr, nullptr, &timeout) > 0;
    }
    void serveDns() {
        while (!stop) {
            if (!ready(dns.value)) continue;
            unsigned char bytes[512];
            sockaddr_storage source{};
            int length = sizeof(source);
            const int size = recvfrom(dns.value, reinterpret_cast<char*>(bytes), sizeof(bytes), 0,
                reinterpret_cast<sockaddr*>(&source), &length);
            if (size < 17) continue;
            ++queries;
            if (!answer) continue; // Actual DNS datagram received, deliberately unanswered.
            std::size_t end = 12;
            while (end < static_cast<std::size_t>(size) && bytes[end]) end += bytes[end] + 1;
            if (end + 5 > static_cast<std::size_t>(size)) continue;
            const bool isA = bytes[end + 1] == 0 && bytes[end + 2] == 1;
            std::vector<unsigned char> response(bytes, bytes + end + 5);
            response[2] = 0x81; response[3] = 0x80;
            response[6] = 0; response[7] = isA ? 1 : 0;
            response[8] = response[9] = response[10] = response[11] = 0;
            if (isA) {
                const unsigned char record[]{0xc0, 0x0c, 0, 1, 0, 1, 0, 0, 0, 0, 0, 4, 127, 0, 0, 1};
                response.insert(response.end(), std::begin(record), std::end(record));
            }
            sendto(dns.value, reinterpret_cast<const char*>(response.data()), static_cast<int>(response.size()), 0,
                reinterpret_cast<sockaddr*>(&source), length);
        }
    }
    void serveHttp() {
        while (!stop) {
            if (!ready(http.value)) continue;
            Socket peer;
            peer.value = accept(http.value, nullptr, nullptr);
            if (peer.value == INVALID_SOCKET) continue;
            const DWORD timeout = 2000;
            setsockopt(peer.value, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
            char buffer[8192];
            if (recv(peer.value, buffer, sizeof(buffer), 0) <= 0) continue;
            const std::string response = "HTTP/1.1 200 OK\r\nContent-Length: 2\r\nConnection: close\r\n\r\nok";
            send(peer.value, response.data(), static_cast<int>(response.size()), 0);
            shutdown(peer.value, SD_SEND);
        }
    }
    Winsock winsock;
    Socket dns, http;
    std::atomic<bool> stop{false};
    std::thread dnsWorker, httpWorker;
};
struct ProcessDns {
    explicit ProcessDns(unsigned short) {
        DNS_CUSTOM_SERVER server{};
        server.dwServerType = DNS_CUSTOM_SERVER_TYPE_UDP;
        server.ServerAddr.Ipv4.sin_family = AF_INET;
        server.ServerAddr.Ipv4.sin_addr.s_addr = htonl(0x7f000002);
        // This API requires port zero, which selects standard UDP port 53.
        const DNS_APPLICATION_SETTINGS settings{DNS_APP_SETTINGS_VERSION1, DNS_APP_SETTINGS_EXCLUSIVE_SERVERS};
        const auto result = DnsSetApplicationSettings(1, &server, &settings);
        if (result) throw std::runtime_error("process-local DNS configuration failed: " + std::to_string(result));
    }
    ~ProcessDns() { DnsSetApplicationSettings(0, nullptr, nullptr); }
    DWORD reset() { return DnsSetApplicationSettings(0, nullptr, nullptr); }
};
}

PHPVPN_TEST(resource_dns, real_delayed_resolution_deadline_cancel_and_reuse) {
    LocalServers servers;
    ProcessDns processOnly(servers.dnsPort);
    auto made = createPlatformTransport();
    PHPVPN_REQUIRE_OK(made);
    auto transport = std::move(made).value();
    const auto suffix = std::to_string(GetCurrentProcessId()) + "-" + std::to_string(GetTickCount64()) + ".phpvpn.test";
    addrinfo* address = nullptr;
    const auto resolveCode = getaddrinfo(("direct-" + suffix).c_str(), nullptr, nullptr, &address);
    if (address) freeaddrinfo(address);
    std::cout << "[ MEASURE ] process DNS getaddrinfo=" << resolveCode << " received_queries=" << servers.queries << "\n";
    auto configure = [&](const char* phase) {
        TransportSettings settings;
        settings.serverUrl = "http://" + std::string(phase) + "-" + suffix + ":" + std::to_string(servers.httpPort) + "/phpvpn.php";
        settings.responseTimeoutSeconds = 1;
        if (!transport->configure(settings)) throw std::runtime_error("transport configure failed");
    };
    configure("control");
    auto control = transport->post(HttpAction::health, "{}");
    std::cout << "[ MEASURE ] WinHTTP control received_queries=" << servers.queries << "\n";
    PHPVPN_REQUIRE_OK(control);
    PHPVPN_CHECK_EQ(control.value().body, std::string("ok"));
    PHPVPN_CHECK(servers.queries > 0);

    servers.answer = false;
    configure("timeout");
    const auto beforeTimeout = servers.queries.load();
    const auto begin = GetTickCount64();
    auto timeout = transport->post(HttpAction::health, "{}");
    const auto timeoutMs = GetTickCount64() - begin;
    PHPVPN_REQUIRE_ERROR(timeout, ErrorCode::transport_timeout);
    PHPVPN_CHECK(servers.queries > beforeTimeout);
    PHPVPN_CHECK(timeoutMs < 2500);
    PHPVPN_CHECK_EQ(timeout.responseBodyBytesRead, std::uint64_t(0));

    configure("cancel");
    const auto beforeCancel = servers.queries.load();
    auto pending = std::async(std::launch::async, [&] { return transport->post(HttpAction::health, "{}"); });
    const auto observedDeadline = GetTickCount64() + 800;
    while (servers.queries == beforeCancel && GetTickCount64() < observedDeadline) Sleep(2);
    const bool observed = servers.queries > beforeCancel;
    const auto cancelBegin = GetTickCount64();
    transport->cancelCurrentRequest();
    auto cancelled = pending.get();
    const auto cancelMs = GetTickCount64() - cancelBegin;
    PHPVPN_CHECK(observed);
    PHPVPN_REQUIRE_ERROR(cancelled, ErrorCode::cancelled);
    PHPVPN_CHECK(cancelMs < 2000);
    servers.answer = true;
    configure("reuse");
    auto reused = transport->post(HttpAction::health, "{}");
    PHPVPN_REQUIRE_OK(reused);
    PHPVPN_CHECK_EQ(reused.value().body, std::string("ok"));
    PHPVPN_CHECK_EQ(processOnly.reset(), DWORD(ERROR_SUCCESS));
    std::cout << "[ MEASURE ] process-local DNS UDP port=" << servers.dnsPort << " received_queries=" << servers.queries
              << " timeout_ms=" << timeoutMs << " cancel_ms=" << cancelMs << " control_and_reuse=200\n";
}
} // namespace phpvpn
