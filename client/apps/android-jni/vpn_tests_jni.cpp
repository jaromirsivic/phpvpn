// Native half of the instrumented VpnService tests (plan-android-v001 T-A01,
// T-A01b, T-N04 in the application), part of libphpvpn-android-tests.so only.
//
// The Kotlin test (VpnPlatformTest.kt) supplies the real hosts: the
// production AndroidNetworkHost over ConnectivityManager and a test
// VpnService implementing TunnelHost. This file wraps them with the production
// JNI bridge (jni_vpn_bridge.cpp), installs an AndroidVpnContext and drives the
// production factories createPlatformNetConfig and createPlatformTunnelDriver
// through the core's TunnelSession, exactly the calls the engine makes. The
// session runs on a native thread, so the bridge attaches it to the VM and
// detaches it when the thread ends, as on the engine network thread.
//
// The responder plays the relay peers behind the tunnel: it reads every packet
// the interface delivers, counts it by destination and answers ICMP echo
// requests to addresses of the VPN prefix, so a ping from the adb shell user
// proves both directions of the descriptor. Only counters and destination
// addresses leave this file, never packet content.
#include <jni.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <iterator>
#include <memory>
#include <optional>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "core/config/config.h"
#include "core/protocol/ipv4.h"
#include "core/runtime/tunnel_session.h"
#include "jni_vpn_bridge.h"
#include "platform/android/netconfig_android.h"
#include "platform/android/tunnel_android.h"
#include "platform/android/vpn_context.h"
#include "platform/iface/durable_file.h"
#include "platform/iface/transport.h"
#if defined(PHPVPN_ANDROID_TLS_TESTS)
#include <curl/curl.h>

#include "platform/android/tls_trust_android.h"
#endif

namespace phpvpn {
namespace {

constexpr const char* kVirtualIp = "10.99.99.2";
constexpr const char* kVpnCidr = "10.99.99.0/24";

std::string utf8(JNIEnv* env, jbyteArray bytes) {
    if (bytes == nullptr) return {};
    const jsize size = env->GetArrayLength(bytes);
    std::string text(static_cast<std::size_t>(size), '\0');
    if (size > 0) env->GetByteArrayRegion(bytes, 0, size, reinterpret_cast<jbyte*>(text.data()));
    return text;
}

jbyteArray bytes(JNIEnv* env, const std::string& text) {
    const auto size = static_cast<jsize>(text.size());
    jbyteArray result = env->NewByteArray(size);
    if (result == nullptr) return nullptr;  // OutOfMemoryError is pending in the VM
    env->SetByteArrayRegion(result, 0, size, reinterpret_cast<const jbyte*>(text.data()));
    return result;
}

std::string describe(const Status& status) {
    if (status.ok()) return "ok";
    return std::string("error ") + status.error().name() + " (" + status.error().detail + ")";
}

// Runs `work` on a new native thread, which the JNI bridge has to attach.
template <class Work>
auto onNativeThread(Work work) -> decltype(work()) {
    std::optional<decltype(work())> result;
    std::thread thread([&] { result.emplace(work()); });
    thread.join();
    return std::move(*result);
}

std::shared_ptr<ClientConfig> vpnConfig() {
    auto config = std::make_shared<ClientConfig>();
    config->vpnCidr = *Ipv4Cidr::parse(kVpnCidr);
    config->virtualIp = *Ipv4Address::parse(kVirtualIp);
    return config;
}

struct ResponderStats {
    std::uint64_t packets = 0;
    std::uint64_t insidePrefix = 0;
    std::uint64_t outsidePrefix = 0;
    std::uint64_t ipv6 = 0;       // to a global or site address
    std::uint64_t ipv6Local = 0;  // to a link-local or multicast address
    std::uint64_t invalid = 0;
    std::uint64_t echoRequests = 0;
    std::uint64_t echoReplies = 0;
    std::uint64_t sendFailures = 0;
    std::uint64_t readFailures = 0;
    std::vector<std::string> outsideDestinations;
    std::vector<std::string> ipv6Destinations;
    std::vector<std::string> echoTargets;
};

// Uncompressed hexadecimal groups, enough to recognize ff02::2 and friends.
std::string ipv6Text(std::span<const std::uint8_t> address) {
    static constexpr char kDigits[] = "0123456789abcdef";
    std::string text;
    for (std::size_t index = 0; index < 16; index += 2) {
        if (index) text += ':';
        const unsigned group = (static_cast<unsigned>(address[index]) << 8) | address[index + 1];
        text += kDigits[(group >> 12) & 0xF];
        text += kDigits[(group >> 8) & 0xF];
        text += kDigits[(group >> 4) & 0xF];
        text += kDigits[group & 0xF];
    }
    return text;
}

void writeChecksum(std::vector<std::uint8_t>& packet, std::size_t offset, std::size_t length, std::size_t field) {
    packet[field] = 0;
    packet[field + 1] = 0;
    const std::uint16_t sum = ipv4HeaderChecksum(std::span<const std::uint8_t>(packet.data() + offset, length));
    packet[field] = static_cast<std::uint8_t>(sum >> 8);
    packet[field + 1] = static_cast<std::uint8_t>(sum & 0xFF);
}

// An ICMP echo reply for an echo request to a peer of the VPN prefix.
std::vector<std::uint8_t> echoReply(std::span<const std::uint8_t> request, const Ipv4PacketView& view) {
    std::vector<std::uint8_t> reply(request.begin(), request.end());
    for (std::size_t index = 0; index < 4; ++index) std::swap(reply[12 + index], reply[16 + index]);
    reply[8] = 64;  // TTL
    writeChecksum(reply, 0, view.headerLength, 10);
    reply[view.headerLength] = 0;  // echo reply
    writeChecksum(reply, view.headerLength, reply.size() - view.headerLength, view.headerLength + 2);
    return reply;
}

struct Scenario {
    std::shared_ptr<AndroidVpnContext> context;
    std::unique_ptr<NetConfig> netConfig;
    std::unique_ptr<TunnelDriver> driver;
    std::unique_ptr<TunnelSession> session;
    TunnelSessionSettings settings;

    std::thread responder;
    std::atomic<bool> stopResponder{false};
    std::mutex statsMutex;
    ResponderStats stats;

    void respond() {
        const Ipv4Cidr prefix = *Ipv4Cidr::parse(kVpnCidr);
        const Ipv4Address own = *Ipv4Address::parse(kVirtualIp);
        const std::shared_ptr<Tunnel> tunnel = session->tunnel();
        std::vector<std::uint8_t> buffer(kMaxTunnelPacketBytes);
        while (!stopResponder.load()) {
            auto read = tunnel->receive(buffer, 200);
            std::lock_guard<std::mutex> lock(statsMutex);
            if (!read.ok()) {
                ++stats.readFailures;
                return;
            }
            if (read->status == TunnelReadStatus::stopped) return;
            if (read->status != TunnelReadStatus::packet) continue;
            ++stats.packets;
            const std::span<const std::uint8_t> packet(buffer.data(), read->size);
            if ((packet[0] >> 4) == 6) {
                // The kernel's own link-local or multicast housekeeping on the
                // interface (router solicitation, MLD) is no traffic of an app.
                const bool local = packet.size() >= 40 &&
                                   (packet[24] == 0xFF || (packet[24] == 0xFE && (packet[25] & 0xC0) == 0x80));
                ++(local ? stats.ipv6Local : stats.ipv6);
                if (packet.size() >= 40 && stats.ipv6Destinations.size() < 8) {
                    stats.ipv6Destinations.push_back(ipv6Text(packet.subspan(24, 16)) + "/next" +
                                                     std::to_string(packet[6]));
                }
                continue;
            }
            Ipv4PacketView view;
            if (!validateIpv4Packet(packet, view)) {
                ++stats.invalid;
                continue;
            }
            if (!prefix.contains(view.destination)) {
                ++stats.outsidePrefix;
                if (stats.outsideDestinations.size() < 16) {
                    stats.outsideDestinations.push_back(view.destination.toString() + "/proto" +
                                                        std::to_string(view.protocol));
                }
                continue;
            }
            ++stats.insidePrefix;
            const bool echoRequest = view.protocol == 1 && view.totalLength >= view.headerLength + 8 &&
                                     packet[view.headerLength] == 8 && view.destination != own;
            if (!echoRequest) continue;
            ++stats.echoRequests;
            if (stats.echoTargets.size() < 4) stats.echoTargets.push_back(view.destination.toString());
            if (tunnel->send(echoReply(packet, view)).ok()) {
                ++stats.echoReplies;
            } else {
                ++stats.sendFailures;
            }
        }
    }
};

std::mutex& scenarioMutex() {
    static std::mutex mutex;
    return mutex;
}

std::unique_ptr<Scenario>& scenario() {
    static std::unique_ptr<Scenario> instance;
    return instance;
}

std::shared_ptr<AndroidVpnContext> installContext(JNIEnv* env, jobject networkHost, jobject tunnelHost,
                                                  const std::string& packageName) {
    AndroidVpnContext::Options options;
    options.packageName = packageName;
    auto context = std::make_shared<AndroidVpnContext>(options);
    context->setNetworkHost(makeJniNetworkHost(env, networkHost));
    context->setTunnelHost(makeJniTunnelHost(env, tunnelHost));
    AndroidVpnContext::install(context);
    return context;
}

// T-A01: the production stack establishes 10.99.99.2/24 with the route
// 10.99.99.0/24 through the test VpnService. Returns "ok <identity>" or the
// error.
jbyteArray vpnStart(JNIEnv* env, jclass, jobject networkHost, jobject tunnelHost, jbyteArray packageName,
                    jbyteArray directory) {
    std::lock_guard<std::mutex> lock(scenarioMutex());
    if (scenario()) return bytes(env, "error a scenario is already running");
    auto running = std::make_unique<Scenario>();
    running->context = installContext(env, networkHost, tunnelHost, utf8(env, packageName));
    // The platform factories capture the installed context, as in the runtime.
    running->netConfig = createPlatformNetConfig();
    running->driver = createPlatformTunnelDriver();
    running->session = std::make_unique<TunnelSession>(*running->driver, *running->netConfig);
    running->settings.journalPath = std::filesystem::path(utf8(env, directory)) / "tunnel-journal.json";
    const auto config = vpnConfig();
    Scenario& state = *running;
    const Status started = onNativeThread([&state, &config] { return state.session->start(*config, state.settings); });
    if (!started.ok()) {
        AndroidVpnContext::uninstall();
        return bytes(env, describe(started));
    }
    std::ostringstream text;
    text << "ok identity=0x" << std::hex << running->session->interfaceLuid();
    scenario() = std::move(running);
    return bytes(env, text.str());
}

jbyteArray vpnResponderStart(JNIEnv* env, jclass) {
    std::lock_guard<std::mutex> lock(scenarioMutex());
    if (!scenario() || !scenario()->session->active()) return bytes(env, "error no session");
    if (scenario()->responder.joinable()) return bytes(env, "error the responder is already running");
    Scenario& state = *scenario();
    state.stopResponder.store(false);
    {
        std::lock_guard<std::mutex> statsLock(state.statsMutex);
        state.stats = ResponderStats{};
    }
    state.responder = std::thread([&state] { state.respond(); });
    return bytes(env, "ok");
}

jbyteArray vpnResponderStop(JNIEnv* env, jclass) {
    std::lock_guard<std::mutex> lock(scenarioMutex());
    if (!scenario() || !scenario()->responder.joinable()) return bytes(env, "error no responder");
    Scenario& state = *scenario();
    state.stopResponder.store(true);
    state.responder.join();
    std::lock_guard<std::mutex> statsLock(state.statsMutex);
    const ResponderStats& stats = state.stats;
    std::ostringstream text;
    text << "packets=" << stats.packets << " insidePrefix=" << stats.insidePrefix
         << " outsidePrefix=" << stats.outsidePrefix << " ipv6=" << stats.ipv6 << " ipv6Local=" << stats.ipv6Local
         << " invalid=" << stats.invalid << " echoRequests=" << stats.echoRequests
         << " echoReplies=" << stats.echoReplies << " sendFailures=" << stats.sendFailures
         << " readFailures=" << stats.readFailures << " echoTargets=";
    for (const auto& target : stats.echoTargets) text << target << ",";
    text << " outsideDestinations=";
    for (const auto& destination : stats.outsideDestinations) text << destination << ",";
    text << " ipv6Destinations=";
    for (const auto& destination : stats.ipv6Destinations) text << destination << ",";
    return bytes(env, text.str());
}

// Stops the session the way the engine teardown does; returns the result and
// how long the stop (closing the descriptor and waiting for the VPN network to
// vanish) took.
jbyteArray vpnStop(JNIEnv* env, jclass) {
    std::lock_guard<std::mutex> lock(scenarioMutex());
    if (!scenario()) return bytes(env, "error no session");
    Scenario& state = *scenario();
    if (state.responder.joinable()) {
        state.stopResponder.store(true);
        state.responder.join();
    }
    const auto begin = std::chrono::steady_clock::now();
    const Status stopped = onNativeThread([&state] { return state.session->stop(); });
    const auto elapsed =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - begin).count();
    const bool journalRemoved = !std::filesystem::exists(state.settings.journalPath);
    onNativeThread([&state] {
        // The hosts drop their global references on an attached thread.
        state.session.reset();
        state.driver.reset();
        state.netConfig.reset();
        state.context.reset();
        AndroidVpnContext::uninstall();
        return 0;
    });
    scenario().reset();
    return bytes(env, describe(stopped) + " stopMs=" + std::to_string(elapsed) +
                          " journalRemoved=" + (journalRemoved ? "true" : "false"));
}

// Startup recovery after a process death (point 3): a journal names an
// identity no descriptor backs any more; recoverPreviousRun with the real
// snapshot must retire it.
jbyteArray vpnRecoveryCheck(JNIEnv* env, jclass, jobject networkHost, jbyteArray packageName, jbyteArray directory) {
    std::lock_guard<std::mutex> lock(scenarioMutex());
    if (scenario()) return bytes(env, "error a scenario is running");
    auto context = installContext(env, networkHost, nullptr, utf8(env, packageName));
    auto netConfig = createPlatformNetConfig();
    auto driver = createPlatformTunnelDriver();
    TunnelSessionSettings settings;
    settings.journalPath = std::filesystem::path(utf8(env, directory)) / "tunnel-journal.json";
    settings.recoveryWaitMs = 0;
    OwnershipJournal journal;
    journal.adapterName = "phpvpn";
    journal.adapterGuid = "{3f6c2a91-5b7e-4d0a-9c1e-8a2b7d4e6f10}";
    journal.interfaceLuid = composeAndroidIdentity(0xDEAD);
    journal.adapterCreated = journal.addressApplied = journal.routeApplied = true;
    journal.address = *Ipv4Address::parse(kVirtualIp);
    journal.prefixLength = 24;
    journal.routeDestination = *Ipv4Cidr::parse(kVpnCidr);
    journal.routeMetric = 5;
    std::string result;
    if (const Status written = durableReplaceFile(settings.journalPath, journal.serialize()); !written.ok()) {
        result = describe(written);
    } else {
        TunnelSession session(*driver, *netConfig);
        const Status recovered = onNativeThread([&] { return session.recoverPreviousRun(settings); });
        result = describe(recovered) + " journalRemoved=" +
                 (std::filesystem::exists(settings.journalPath) ? "false" : "true");
    }
    onNativeThread([&] {
        driver.reset();
        netConfig.reset();
        context.reset();
        AndroidVpnContext::uninstall();
        return 0;
    });
    return bytes(env, result);
}

// loadAndVerify with the real NetworkHost and no TunnelHost (point 18):
// "the VPN service is not running", never a crash.
jbyteArray vpnLoadCheck(JNIEnv* env, jclass, jobject networkHost, jbyteArray packageName) {
    std::lock_guard<std::mutex> lock(scenarioMutex());
    if (scenario()) return bytes(env, "error a scenario is running");
    auto context = installContext(env, networkHost, nullptr, utf8(env, packageName));
    auto driver = createPlatformTunnelDriver();
    const Status loaded = onNativeThread([&] { return driver->loadAndVerify(L""); });
    onNativeThread([&] {
        driver.reset();
        context.reset();
        AndroidVpnContext::uninstall();
        return 0;
    });
    return bytes(env, describe(loaded));
}

#if defined(PHPVPN_ANDROID_TLS_TESTS)
// T-N04 in the application: one exchange of the production curl transport
// with the system CA store of tls_trust_android.cpp.
jbyteArray tlsRequest(JNIEnv* env, jclass, jbyteArray url, jboolean trustAny) {
    auto transport = createPlatformTransport();
    if (!transport.ok()) return bytes(env, describe(transport.error()));
    TransportSettings settings;
    settings.serverUrl = utf8(env, url);
    settings.trustAnyTlsCertificate = trustAny == JNI_TRUE;
    settings.responseTimeoutSeconds = 20;
    settings.maxResponseBodyBytes = 4 * 1024 * 1024;
    settings.httpHeaders = kDefaultHttpHeaders;
    if (const Status configured = (*transport)->configure(settings); !configured.ok()) {
        return bytes(env, describe(configured));
    }
    const auto exchange = onNativeThread([&] { return (*transport)->post(HttpAction::health, ""); });
    if (!exchange.ok()) return bytes(env, describe(exchange.error()));
    return bytes(env, "ok status=" + std::to_string(exchange.value().statusCode));
}

// The same request with curl directly and its error text, for the cases
// where the transport only reports the mapped error code.
jbyteArray tlsCurlDiagnose(JNIEnv* env, jclass, jbyteArray url, jboolean trustAny) {
    CURL* easy = curl_easy_init();
    if (easy == nullptr) return bytes(env, "error curl_easy_init");
    char buffer[CURL_ERROR_SIZE] = {};
    const std::string target = utf8(env, url) + "?action=health";
    curl_easy_setopt(easy, CURLOPT_URL, target.c_str());
    curl_easy_setopt(easy, CURLOPT_PROTOCOLS_STR, "http,https");
    curl_easy_setopt(easy, CURLOPT_HTTP_VERSION, static_cast<long>(CURL_HTTP_VERSION_1_1));
    curl_easy_setopt(easy, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(easy, CURLOPT_TIMEOUT_MS, 20000L);
    curl_easy_setopt(easy, CURLOPT_SSL_VERIFYPEER, trustAny == JNI_TRUE ? 0L : 1L);
    curl_easy_setopt(easy, CURLOPT_SSL_VERIFYHOST, trustAny == JNI_TRUE ? 0L : 2L);
    curl_easy_setopt(easy, CURLOPT_SSLVERSION, static_cast<long>(CURL_SSLVERSION_TLSv1_2));
    curl_easy_setopt(easy, CURLOPT_POSTFIELDS, "");
    curl_easy_setopt(easy, CURLOPT_POSTFIELDSIZE_LARGE, static_cast<curl_off_t>(0));
    curl_easy_setopt(easy, CURLOPT_ERRORBUFFER, buffer);
    curl_easy_setopt(easy, CURLOPT_WRITEFUNCTION, +[](char*, std::size_t size, std::size_t count, void*) { return size * count; });
    if (trustAny != JNI_TRUE && !installAndroidSystemTrust(easy).ok()) {
        curl_easy_cleanup(easy);
        return bytes(env, "error the CA store could not be installed");
    }
    const CURLcode code = onNativeThread([easy] { return curl_easy_perform(easy); });
    long status = 0;
    curl_easy_getinfo(easy, CURLINFO_RESPONSE_CODE, &status);
    curl_easy_cleanup(easy);
    return bytes(env, "curl=" + std::to_string(static_cast<int>(code)) + " http=" + std::to_string(status) +
                          " error=" + buffer);
}

jbyteArray tlsStoreInfo(JNIEnv* env, jclass) {
    const auto info = androidSystemTrustStoreInfo();
    if (!info.ok()) return bytes(env, describe(info.error()));
    return bytes(env, "ok certificates=" + std::to_string(info->certificates) +
                          " directoryIndex=" + std::to_string(info->directoryIndex) +
                          " skipped=" + std::to_string(info->skippedFiles));
}
#else
jbyteArray tlsRequest(JNIEnv* env, jclass, jbyteArray, jboolean) {
    return bytes(env, "skipped: built without PHPVPN_ANDROID_DEPS");
}
jbyteArray tlsStoreInfo(JNIEnv* env, jclass) { return bytes(env, "skipped: built without PHPVPN_ANDROID_DEPS"); }
jbyteArray tlsCurlDiagnose(JNIEnv* env, jclass, jbyteArray, jboolean) {
    return bytes(env, "skipped: built without PHPVPN_ANDROID_DEPS");
}
#endif

const JNINativeMethod kVpnTestMethods[] = {
    {const_cast<char*>("vpnStart"),
     const_cast<char*>("(Lphpvpn/android/bridge/NetworkHost;Lphpvpn/android/bridge/TunnelHost;[B[B)[B"),
     reinterpret_cast<void*>(&vpnStart)},
    {const_cast<char*>("vpnResponderStart"), const_cast<char*>("()[B"), reinterpret_cast<void*>(&vpnResponderStart)},
    {const_cast<char*>("vpnResponderStop"), const_cast<char*>("()[B"), reinterpret_cast<void*>(&vpnResponderStop)},
    {const_cast<char*>("vpnStop"), const_cast<char*>("()[B"), reinterpret_cast<void*>(&vpnStop)},
    {const_cast<char*>("vpnRecoveryCheck"), const_cast<char*>("(Lphpvpn/android/bridge/NetworkHost;[B[B)[B"),
     reinterpret_cast<void*>(&vpnRecoveryCheck)},
    {const_cast<char*>("vpnLoadCheck"), const_cast<char*>("(Lphpvpn/android/bridge/NetworkHost;[B)[B"),
     reinterpret_cast<void*>(&vpnLoadCheck)},
    {const_cast<char*>("tlsRequest"), const_cast<char*>("([BZ)[B"), reinterpret_cast<void*>(&tlsRequest)},
    {const_cast<char*>("tlsStoreInfo"), const_cast<char*>("()[B"), reinterpret_cast<void*>(&tlsStoreInfo)},
    {const_cast<char*>("tlsCurlDiagnose"), const_cast<char*>("([BZ)[B"), reinterpret_cast<void*>(&tlsCurlDiagnose)},
};

}  // namespace

// Called from JNI_OnLoad of libphpvpn-android-tests.so (native_tests_jni.cpp).
bool registerVpnTestNatives(JNIEnv* env) {
    if (!initializeVpnHostBridge(env)) return false;
    jclass library = env->FindClass("phpvpn/android/bridge/VpnTestLibrary");
    if (library == nullptr) return false;  // NoClassDefFoundError is pending
    const jint registered =
        env->RegisterNatives(library, kVpnTestMethods, static_cast<jint>(std::size(kVpnTestMethods)));
    env->DeleteLocalRef(library);
    return registered == JNI_OK;
}

}  // namespace phpvpn
