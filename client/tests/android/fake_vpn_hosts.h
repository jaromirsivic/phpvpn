// Fake NetworkHost and TunnelHost for the Android platform tests (T-N02,
// plan-android-v001 WP03). They stand in for the Kotlin hosts of point 18, so
// AndroidVpnContext, AndroidNetConfig and AndroidTunnelDriver run on the build
// host (Windows, Linux) and on the emulator without JNI.
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <span>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#include <share.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

#include "platform/android/tunnel_android.h"
#include "platform/android/vpn_context.h"
#include "platform/iface/tunnel.h"

namespace phpvpn::test {

// A descriptor the context may close; on POSIX also checkable afterwards.
inline int openPlaceholderDescriptor() {
#if defined(_WIN32)
    int descriptor = -1;
    if (::_sopen_s(&descriptor, "NUL", _O_RDONLY, _SH_DENYNO, 0) != 0) return -1;
    return descriptor;
#else
    return ::open("/dev/null", O_RDONLY | O_CLOEXEC);
#endif
}

#if !defined(_WIN32)
inline bool descriptorIsOpen(int descriptor) { return ::fcntl(descriptor, F_GETFD) != -1; }
#endif

struct FakeRoute {
    std::string destination;
    int prefixLength = 0;
    std::string gateway;  // empty: null (on-link)
    bool isDefault = false;
    std::string type = "unicast";
    std::string interfaceName;  // empty: the network's interface
    int ifindex = -1;           // -1: the network's ifindex
};

inline FakeRoute fakeRoute(std::string destination, int prefixLength, std::string gateway, bool isDefault,
                           std::string type = "unicast") {
    FakeRoute route;
    route.destination = std::move(destination);
    route.prefixLength = prefixLength;
    route.gateway = std::move(gateway);
    route.isDefault = isDefault;
    route.type = std::move(type);
    return route;
}

struct FakeNetwork {
    std::int64_t handle = 0;
    std::string interfaceName;
    int ifindex = 0;
    bool vpn = false;
    std::string transport = "wifi";
    std::vector<std::pair<std::string, int>> addresses;
    std::vector<FakeRoute> routes;
    std::vector<std::string> dnsServers;
};

inline std::string quoted(const std::string& text) { return "\"" + text + "\""; }

// The snapshot JSON of netconfig_android.h, the shape AndroidNetworkHost.kt writes.
inline std::string snapshotJson(std::optional<std::int64_t> active, const std::vector<FakeNetwork>& networks) {
    std::string json = "{\"version\":1,\"activeNetwork\":" + (active ? std::to_string(*active) : "null") +
                       ",\"networks\":[";
    for (std::size_t n = 0; n < networks.size(); ++n) {
        const FakeNetwork& network = networks[n];
        if (n) json += ",";
        json += "{\"handle\":" + std::to_string(network.handle) + ",\"interfaceName\":" + quoted(network.interfaceName) +
                ",\"vpn\":" + (network.vpn ? "true" : "false") + ",\"transports\":[" + quoted(network.transport) +
                "],\"addresses\":[";
        for (std::size_t a = 0; a < network.addresses.size(); ++a) {
            if (a) json += ",";
            json += "{\"interfaceName\":" + quoted(network.interfaceName) + ",\"ifindex\":" +
                    std::to_string(network.ifindex) + ",\"address\":" + quoted(network.addresses[a].first) +
                    ",\"prefixLength\":" + std::to_string(network.addresses[a].second) + "}";
        }
        json += "],\"routes\":[";
        for (std::size_t r = 0; r < network.routes.size(); ++r) {
            const FakeRoute& route = network.routes[r];
            if (r) json += ",";
            json += "{\"interfaceName\":" +
                    quoted(route.interfaceName.empty() ? network.interfaceName : route.interfaceName) +
                    ",\"ifindex\":" + std::to_string(route.ifindex < 0 ? network.ifindex : route.ifindex) +
                    ",\"destination\":" + quoted(route.destination) +
                    ",\"prefixLength\":" + std::to_string(route.prefixLength) +
                    ",\"gateway\":" + (route.gateway.empty() ? std::string("null") : quoted(route.gateway)) +
                    ",\"type\":" + quoted(route.type) + ",\"isDefault\":" + (route.isDefault ? "true" : "false") + "}";
        }
        json += "],\"dnsServers\":[";
        for (std::size_t d = 0; d < network.dnsServers.size(); ++d) {
            if (d) json += ",";
            json += quoted(network.dnsServers[d]);
        }
        json += "]}";
    }
    return json + "]}";
}

// The emulator's Wi-Fi as AndroidNetworkHost reports it (ifindex 16).
inline FakeNetwork emulatorWifi() {
    FakeNetwork wifi;
    wifi.handle = 434026158090;
    wifi.interfaceName = "wlan0";
    wifi.ifindex = 16;
    wifi.addresses = {{"10.0.2.16", 24}, {"fec0::d980:4e45:9fac:c5a9", 64}};
    wifi.routes = {fakeRoute("0.0.0.0", 0, "10.0.2.2", true),
                   fakeRoute("10.0.2.0", 24, "", false),
                   fakeRoute("::", 0, "fe80::2", true),
                   fakeRoute("fec0::", 64, "", false)};
    wifi.dnsServers = {"10.0.2.3", "fec0::3"};
    return wifi;
}

// The emulator's cellular network (ifindex 15).
inline FakeNetwork emulatorCellular() {
    FakeNetwork cellular;
    cellular.handle = 429731190794;
    cellular.interfaceName = "eth0";
    cellular.ifindex = 15;
    cellular.transport = "cellular";
    cellular.addresses = {{"10.0.2.15", 24}};
    cellular.routes = {fakeRoute("0.0.0.0", 0, "10.0.2.2", true), fakeRoute("10.0.2.0", 24, "", false)};
    cellular.dnsServers = {"10.0.2.3"};
    return cellular;
}

// Another app's VPN, which the snapshot must ignore.
inline FakeNetwork foreignVpn(const std::string& address = "10.99.99.5", int prefixLength = 24) {
    FakeNetwork vpn;
    vpn.handle = 438321125386;
    vpn.interfaceName = "tun0";
    vpn.ifindex = 30;
    vpn.vpn = true;
    vpn.transport = "vpn";
    vpn.addresses = {{address, prefixLength}};
    vpn.routes = {fakeRoute("10.99.99.0", 24, "", false), fakeRoute("::", 0, "", false, "unreachable")};
    vpn.dnsServers = {"10.99.99.1"};
    return vpn;
}

class FakeNetworkHost final : public AndroidNetworkHost {
public:
    explicit FakeNetworkHost(std::string json) : json_(std::move(json)) {}

    Result<std::string> snapshotJson() override {
        std::lock_guard<std::mutex> lock(mutex_);
        ++snapshotCalls;
        if (snapshotFailure) return Error(*snapshotFailure, "fake snapshot failure");
        if (afterCalls > 0 && snapshotCalls > afterCalls) return afterJson_;
        return json_;
    }

    Result<bool> isConsentGranted() override {
        std::lock_guard<std::mutex> lock(mutex_);
        ++consentCalls;
        if (consentFailure) return Error(*consentFailure, "fake consent failure");
        return consent;
    }

    void setJson(std::string json) {
        std::lock_guard<std::mutex> lock(mutex_);
        json_ = std::move(json);
    }

    // From call number `calls + 1` on, the host reports `json` instead.
    void switchAfter(int calls, std::string json) {
        std::lock_guard<std::mutex> lock(mutex_);
        afterCalls = calls;
        afterJson_ = std::move(json);
    }

    bool consent = true;
    std::optional<ErrorCode> consentFailure;
    std::optional<ErrorCode> snapshotFailure;
    int snapshotCalls = 0;
    int consentCalls = 0;
    int afterCalls = 0;

private:
    std::mutex mutex_;
    std::string json_;
    std::string afterJson_;
};

class FakeTunnelHost final : public AndroidTunnelHost {
public:
    explicit FakeTunnelHost(std::function<int()> makeDescriptor = openPlaceholderDescriptor)
        : makeDescriptor_(std::move(makeDescriptor)) {}

    Result<int> establish(const VpnSpec& spec) override {
        ++establishCalls;
        lastSpec = spec;
        lastSpecJson = serializeVpnSpec(spec);
        if (establishFailure) return Error(*establishFailure, "fake establish failure");
        lastDescriptor = makeDescriptor_();
        return lastDescriptor;
    }

    Result<bool> waitVpnNetworkGone(int timeoutMs) override {
        ++waitCalls;
        lastWaitTimeoutMs = timeoutMs;
        return networkGone;
    }

    std::optional<ErrorCode> establishFailure;
    bool networkGone = true;
    int establishCalls = 0;
    int waitCalls = 0;
    int lastWaitTimeoutMs = 0;
    int lastDescriptor = -1;
    std::optional<VpnSpec> lastSpec;
    std::string lastSpecJson;

private:
    std::function<int()> makeDescriptor_;
};

inline AndroidVpnContext::Options testContextOptions() {
    AndroidVpnContext::Options options;
    options.packageName = "io.github.jaromirsivic.phpvpn";
    return options;
}

// Packet session without a packet descriptor, for the portable driver tests.
class FakeTunnel final : public Tunnel {
public:
    Result<TunnelReadResult> receive(std::span<std::uint8_t>, std::int64_t) override {
        return TunnelReadResult{stopped ? TunnelReadStatus::stopped : TunnelReadStatus::empty, 0};
    }
    Status send(std::span<const std::uint8_t>) override { return Status::success(); }
    void signalStop() override { stopped = true; }
    std::uint64_t oversizedReceiveCount() const override { return 0; }

    bool stopped = false;
    bool detached = false;
};

struct FakeTunnelFactory {
    std::shared_ptr<int> calls = std::make_shared<int>(0);
    std::shared_ptr<int> lastDescriptor = std::make_shared<int>(-1);
    std::shared_ptr<FakeTunnel> tunnel = std::make_shared<FakeTunnel>();
    std::optional<ErrorCode> failure;

    AndroidTunnelFactory factory() const {
        auto self = *this;
        return [self](int descriptor) -> Result<AndroidDescriptorSession> {
            ++*self.calls;
            *self.lastDescriptor = descriptor;
            if (self.failure) return Error(*self.failure, "fake session failure");
            AndroidDescriptorSession session;
            session.tunnel = self.tunnel;
            auto tunnel = self.tunnel;
            session.detach = [tunnel] {
                tunnel->signalStop();
                tunnel->detached = true;
            };
            return session;
        };
    }
};

}  // namespace phpvpn::test
