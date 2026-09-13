// Explicit elevated integration controller. Never targets the installed phpvpn
// service. The separate runner owns conflict checks and elevation consent.
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include "core/config/config_defaults.h"
#include "core/ipc/ipc_protocol.h"
#include "core/ipc/wizard_ipc.h"
#include "core/runtime/network_worker.h"
#include "core/runtime/tunnel_session.h"
#include "core/wizard/probe_client.h"
#include "core/wizard/wizard_defaults.h"
#include "platform/iface/netconfig.h"
#include "platform/windows/ipc_pipe.h"
#include "status_model.h"
#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <utility>

namespace {
using namespace phpvpn;
constexpr wchar_t serviceName[] = L"phpvpn-plan3-e2e-d6c8";
constexpr wchar_t pipeName[] = LR"(\\.\pipe\phpvpn-plan3-e2e-d6c8)";
constexpr wchar_t adapterName[] = L"PHPVPN Plan3 E2E d6c8";
constexpr char adapterGuid[] = "{4b67d77c-70a2-49f6-97db-a5f4d6c8e2e1}";
constexpr char relay[] = "http://127.0.0.1:18093/phpvpn.php";
void check(bool ok, const std::string& message) { if (!ok) throw std::runtime_error(message); }
template<class T> T require(Result<T> result) {
    if (!result) throw std::runtime_error(std::string("operation failed: ") + result.error().name());
    return std::move(result).value();
}
void require(Status result) {
    if (!result) throw std::runtime_error(std::string("operation failed: ") + result.error().name());
}
void winCheck(bool ok, const char* action) {
    if (!ok) throw std::runtime_error(std::string(action) + ": Windows error " + std::to_string(GetLastError()));
}
void pass(const char* name) { std::cout << "[ PASS ] " << name << '\n' << std::flush; }
std::filesystem::path executableDirectory() {
    std::array<wchar_t, 32768> path{};
    const auto count = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    check(count && count < path.size(), "executable path unavailable or truncated");
    return std::filesystem::path(std::wstring(path.data(), count)).parent_path();
}
std::string readConfig(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    check(input.is_open(), "isolated configuration is unreadable");
    std::string bytes(kMaxConfigFileBytes + 1, '\0');
    input.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    const auto size = static_cast<std::size_t>(input.gcount());
    check(size <= kMaxConfigFileBytes && !input.bad(), "isolated config exceeds bound or read failed");
    bytes.resize(size);
    return bytes;
}

class OwnedService {
public:
    explicit OwnedService(const std::filesystem::path& executable) {
        check(executable.filename() == L"phpvpn-e2e-service.exe" && std::filesystem::is_regular_file(executable),
              "expected sibling phpvpn-e2e-service.exe is missing");
        executablePath_ = std::filesystem::absolute(executable).lexically_normal().wstring();
        manager_ = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT | SC_MANAGER_CREATE_SERVICE);
        winCheck(manager_ != nullptr, "OpenSCManager(test registration)");
        const auto command = L"\"" + executable.wstring() + L"\"";
        service_ = CreateServiceW(manager_, serviceName, serviceName,
            SERVICE_START | SERVICE_STOP | SERVICE_QUERY_STATUS | DELETE,
            SERVICE_WIN32_OWN_PROCESS, SERVICE_DEMAND_START, SERVICE_ERROR_NORMAL,
            command.c_str(), nullptr, nullptr, nullptr, nullptr, nullptr);
        if (!service_) {
            const auto error = GetLastError();
            CloseServiceHandle(manager_); manager_ = nullptr;
            throw std::runtime_error("CreateService refused fixed test identity, existing services are never adopted: " + std::to_string(error));
        }
    }
    OwnedService(const OwnedService&) = delete;
    OwnedService& operator=(const OwnedService&) = delete;
    ~OwnedService() {
        if (service_) {
            try { cleanup(); }
            catch (const std::exception& error) {
                std::cerr << "[ CLEANUP FAIL ] " << error.what() << ". Runner must inspect only the fixed test service.\n";
                CloseServiceHandle(service_); service_ = nullptr;
            }
        }
        if (manager_) CloseServiceHandle(manager_);
    }
    SERVICE_STATUS_PROCESS status() const {
        SERVICE_STATUS_PROCESS value{}; DWORD count = 0;
        winCheck(QueryServiceStatusEx(service_, SC_STATUS_PROCESS_INFO, reinterpret_cast<BYTE*>(&value), sizeof(value), &count) != FALSE,
                 "QueryServiceStatusEx(test service)");
        return value;
    }
    void start() {
        winCheck(StartServiceW(service_, 0, nullptr) != FALSE, "StartService(test service)");
        const auto deadline = GetTickCount64() + 15000;
        for (;;) {
            const auto value = status();
            if (value.dwCurrentState == SERVICE_RUNNING) return;
            check(value.dwCurrentState != SERVICE_STOPPED, "test service stopped during startup, exit=" + std::to_string(value.dwWin32ExitCode));
            check(GetTickCount64() < deadline, "SCM start exceeded 15 seconds");
            Sleep(20);
        }
    }
    ULONGLONG stop() {
        const auto began = GetTickCount64();
        if (status().dwCurrentState != SERVICE_STOPPED) {
            SERVICE_STATUS ignored{};
            if (!ControlService(service_, SERVICE_CONTROL_STOP, &ignored)) {
                const auto error = GetLastError();
                check(error == ERROR_SERVICE_NOT_ACTIVE, "SCM test stop rejected: " + std::to_string(error));
            }
            while (status().dwCurrentState != SERVICE_STOPPED) {
                check(GetTickCount64() - began < 5000, "SCM test stop exceeded 5000 ms");
                Sleep(10);
            }
        }
        return GetTickCount64() - began;
    }
    ULONGLONG crashVerifiedProcess() {
        // Retain the process handle throughout verification and termination.
        // A PID recycled after OpenProcess cannot retarget that handle.
        const auto original = status();
        check(original.dwCurrentState == SERVICE_RUNNING && original.dwServiceType == SERVICE_WIN32_OWN_PROCESS &&
              original.dwProcessId != 0 && original.dwProcessId != GetCurrentProcessId(),
              "refusing crash: owned test service has no distinct RUNNING process");
        struct ProcessHandle {
            HANDLE value = nullptr;
            ~ProcessHandle() { if (value) CloseHandle(value); }
        } process;
        process.value = OpenProcess(PROCESS_TERMINATE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, original.dwProcessId);
        winCheck(process.value != nullptr, "OpenProcess(owned test service crash)");
        std::array<wchar_t, 32768> image{};
        DWORD size = static_cast<DWORD>(image.size());
        winCheck(QueryFullProcessImageNameW(process.value, 0, image.data(), &size) != FALSE,
                 "QueryFullProcessImageName(owned test service crash)");
        const auto imagePath = std::filesystem::path(std::wstring(image.data(), size)).lexically_normal().wstring();
        check(CompareStringOrdinal(imagePath.c_str(), -1, executablePath_.c_str(), -1, TRUE) == CSTR_EQUAL,
              "refusing crash: SCM PID image differs from exact sibling test executable path");
        const auto latest = status();
        check(latest.dwCurrentState == SERVICE_RUNNING && latest.dwProcessId == original.dwProcessId,
              "refusing crash: owned service process changed during verification");
        DWORD exitCode = 0;
        winCheck(GetExitCodeProcess(process.value, &exitCode) != FALSE, "GetExitCodeProcess(before test crash)");
        check(exitCode == STILL_ACTIVE, "refusing crash: verified test process has already exited");
        const auto began = GetTickCount64();
        constexpr UINT forcedExit = 0xE2E10001u;
        winCheck(TerminateProcess(process.value, forcedExit) != FALSE, "TerminateProcess(verified owned test service)");
        for (;;) {
            winCheck(GetExitCodeProcess(process.value, &exitCode) != FALSE, "GetExitCodeProcess(after test crash)");
            if (exitCode != STILL_ACTIVE && status().dwCurrentState == SERVICE_STOPPED) break;
            check(GetTickCount64() - began < 5000, "owned service crash did not reach process exit and SCM STOPPED within 5000 ms");
            Sleep(10);
        }
        check(exitCode == forcedExit, "owned process exit did not match deliberate crash exit code");
        std::cout << "[ EVIDENCE ] verified fixed test service PID " << original.dwProcessId
                  << " terminated, SCM STOPPED after " << GetTickCount64() - began << " ms\n";
        return GetTickCount64() - began;
    }
    void cleanup() {
        if (!service_) return;
        stop();
        winCheck(DeleteService(service_) != FALSE, "DeleteService(owned test service)");
        CloseServiceHandle(service_); service_ = nullptr;
    }
private:
    SC_HANDLE manager_ = nullptr, service_ = nullptr;
    std::wstring executablePath_;
};

class Client {
public:
    std::unique_ptr<CryptoProvider> crypto = require(createPlatformCryptoProvider());
    WizardRequest session;
    std::string uuid() { return require(crypto->randomUuidV4()); }
    std::string send(std::string_view bytes, DWORD timeout = 5000) {
        return require(sendIpcRequestTo(pipeName, bytes, timeout));
    }
    JsonValue legacy(IpcCommand command, bool allowError = false, DWORD timeout = 5000) {
        const auto id = uuid();
        auto value = require(Json::parse(send(IpcProtocol::buildRequest(command, id), timeout)));
        check(value.isObject() && value.find("requestId") && value.find("requestId")->isString() &&
              value.find("requestId")->stringValue() == id, "legacy reply is not correlated");
        if (const auto* error = value.find("error"); error && !allowError) {
            const auto* code = error->find("code");
            throw std::runtime_error("service rejected command: " + (code && code->isString() ? code->stringValue() : "malformed error"));
        }
        return value;
    }
    tray::TrayStatus status(DWORD timeout = 5000) {
        auto result = tray::parseStatusResponse(Json::serialize(legacy(IpcCommand::get_status, false, timeout)));
        check(result.valid, "strict production status parser rejected service reply");
        return result;
    }
    WizardReply wizard(WizardCommand command) {
        session.command = command; session.requestId = uuid();
        const auto encoded = require(WizardIpc::buildRequest(session));
        auto reply = require(WizardIpc::parseReply(command, session.requestId, send(encoded)));
        if (reply.error) throw std::runtime_error("Wizard command failed: " + reply.error->code);
        return reply;
    }
    void awaitVpn(std::string_view state, std::string_view phase, DWORD timeout) {
        const auto end = GetTickCount64() + timeout;
        for (;;) {
            const auto now = GetTickCount64();
            check(now < end, "VPN state deadline exceeded for " + std::string(state));
            const auto value = status(static_cast<DWORD>(std::min<ULONGLONG>(1000, end - now)));
            if (value.state == state && value.phase == phase) return;
            check(value.state != "error", "VPN entered error: " + value.errorCode);
            Sleep(20);
        }
    }
    WizardOperationResult checkNext(WizardCommand command, WizardOperationKind kind) {
        session.clientAttemptId = uuid(); ++session.attemptSequence;
        auto start = wizard(command);
        session.operationId = start.result.find("operationId")->stringValue();
        const auto end = GetTickCount64() + 45000;
        auto nextHeartbeat = GetTickCount64() + 15000;
        for (;;) {
            check(GetTickCount64() < end, "Wizard Next exceeded controller deadline, no retry issued");
            auto value = require(WizardIpc::parseOperation(wizard(WizardCommand::get_operation).result));
            check(value.operationId == session.operationId && value.kind == kind && value.connectionEpoch == session.connectionEpoch &&
                  value.clientAttemptId == session.clientAttemptId && value.attemptSequence == session.attemptSequence,
                  "Wizard operation correlation mismatch");
            if (value.state == WizardOperationState::succeeded) return value;
            check(value.state == WizardOperationState::pending || value.state == WizardOperationState::running,
                  "Wizard operation terminal failure: " + (value.error ? value.error->code : "cancelled/interrupted"));
            if (GetTickCount64() >= nextHeartbeat) { wizard(WizardCommand::keepalive); nextHeartbeat += 15000; }
            Sleep(25);
        }
    }
};

void verifyNetwork(NetConfig& network, const NetworkSnapshot& baseline, std::uint64_t luid, bool connected) {
    const auto snapshot = require(network.snapshot());
    check(snapshot.sameDefaultRouteAndDns(baseline), "default route or DNS changed");
    const auto own = *Ipv4Address::parse("10.99.99.2");
    const auto vpn = *Ipv4Cidr::parse("10.99.99.0/24");
    const bool address = std::any_of(snapshot.addresses.begin(), snapshot.addresses.end(), [&](const auto& item) {
        return item.interfaceLuid == luid && item.address == own && item.prefixLength == 24;
    });
    const bool route = std::any_of(snapshot.routes.begin(), snapshot.routes.end(), [&](const auto& item) {
        return item.interfaceLuid == luid && item.destination.toString() == vpn.toString() && item.nextHop.value() == 0;
    });
    check(address == connected && route == connected, connected ? "Wintun IP/on-link route missing on exact adapter LUID" : "owned IP/route survived teardown");
    if (!connected) {
        check(std::none_of(snapshot.addresses.begin(), snapshot.addresses.end(), [&](const auto& item) { return item.interfaceLuid == luid; }),
              "test adapter still has an IPv4 address after teardown");
        check(std::none_of(snapshot.routes.begin(), snapshot.routes.end(), [&](const auto& item) { return item.interfaceLuid == luid; }),
              "test adapter still has an IPv4 route after teardown");
    }
}

// The peer owns no adapter. It authenticates as .3 through the production
// NetworkWorker and returns an IPv4 UDP reply to the actual Windows .2 socket.
void udpRoundtrip(Client& client, const ClientConfigPtr& stored) {
    WSADATA data{};
    check(WSAStartup(MAKEWORD(2, 2), &data) == 0, "WSAStartup failed");
    struct Winsock { ~Winsock() { WSACleanup(); } } winsock;
    struct Socket {
        SOCKET value = INVALID_SOCKET;
        ~Socket() { if (value != INVALID_SOCKET) closesocket(value); }
    } socket;
    socket.value = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    check(socket.value != INVALID_SOCKET, "UDP socket creation failed");
    sockaddr_in local{}; local.sin_family = AF_INET;
    check(InetPtonA(AF_INET, "10.99.99.2", &local.sin_addr) == 1, "local IPv4 parse failed");
    check(bind(socket.value, reinterpret_cast<const sockaddr*>(&local), sizeof(local)) == 0,
          "UDP socket could not bind to actual Wintun IPv4, error=" + std::to_string(WSAGetLastError()));
    int localSize = sizeof(local);
    check(getsockname(socket.value, reinterpret_cast<sockaddr*>(&local), &localSize) == 0, "UDP getsockname failed");
    sockaddr_in remote{}; remote.sin_family = AF_INET; remote.sin_port = htons(47993);
    check(InetPtonA(AF_INET, "10.99.99.3", &remote.sin_addr) == 1, "peer IPv4 parse failed");
    check(connect(socket.value, reinterpret_cast<const sockaddr*>(&remote), sizeof(remote)) == 0, "UDP peer selection failed");
    u_long nonblocking = 1;
    check(ioctlsocket(socket.value, FIONBIO, &nonblocking) == 0, "UDP nonblocking mode failed");

    const auto a2 = *Ipv4Address::parse("10.99.99.2");
    const auto a3 = *Ipv4Address::parse("10.99.99.3");
    const auto peerKey = require(defaultPayloadKey(*client.crypto, a2, a3));
    const auto string = [](std::string value) { return JsonValue::makeString(std::move(value)); };
    auto document = JsonValue::makeObject({
        {"masterSetup", JsonValue::makeObject({
            {"httpHeaders", string(stored->httpHeaders)}, {"serverUrl", string(relay)}, {"serverSecret", string(stored->serverSecret)},
            {"vpnCidr", string("10.99.99.0/24")}, {"virtualIp", string("10.99.99.3")},
            {"accessKey", string(defaultAccessKey("10.99.99.3"))}})},
        {"runtime", JsonValue::makeObject({{"responseTimeoutSeconds", JsonValue::makeInteger(3)}})},
        {"communicationContracts", JsonValue::makeObject({{"10.99.99.2", JsonValue::makeObject({{"payloadKey", string(peerKey)}})}})}
    });
    ConfigWarnings warnings;
    const auto peerConfig = require(parseAndValidateConfig(Json::serialize(document), warnings));
    auto transport = require(createPlatformTransport());
    auto clock = createPlatformClock();
    NetworkWorker peer(*client.crypto, *transport, *clock);
    TrafficCounters counters;
    require(peer.configure(peerConfig, &counters));
    require(peer.performHealth()); require(peer.performInfo());
    DirectionQueues outgoing(1024 * 1024, 0, 1), incoming(1024 * 1024, 0, 1);
    DropCounters outDrops, inDrops;
    require(peer.verifyAccessKey(incoming, inDrops));
    // Isolated relay may contain earlier fixture packets. Exact per-run nonce
    // prevents treating one of them as our successful UDP exchange.
    incoming.clear();
    const auto payload = "phpvpn-real-wintun-udp:" + client.uuid();
    const auto before = client.status();
    check(send(socket.value, payload.data(), static_cast<int>(payload.size()), 0) == static_cast<int>(payload.size()),
          "Windows UDP send failed: " + std::to_string(WSAGetLastError()));
    const auto deadline = GetTickCount64() + 15000;
    std::optional<PacketBytes> actualPacket;
    while (!actualPacket) {
        check(GetTickCount64() < deadline, "actual Windows UDP packet did not reach peer through Wintun/PHP");
        require(peer.exchangeData(outgoing, incoming, outDrops, inDrops, true));
        while (auto packet = incoming.popFront(a2)) {
            Ipv4PacketView view;
            if (!validateIpv4Packet(*packet, view) || view.source != a2 || view.destination != a3 || (*packet)[9] != IPPROTO_UDP ||
                packet->size() != view.headerLength + 8 + payload.size()) continue;
            const auto offset = view.headerLength;
            const auto* udp = packet->data() + offset;
            const auto sourcePort = static_cast<unsigned>((udp[0] << 8) | udp[1]);
            const auto targetPort = static_cast<unsigned>((udp[2] << 8) | udp[3]);
            const std::string_view delivered(reinterpret_cast<const char*>(udp + 8), payload.size());
            if (sourcePort == ntohs(local.sin_port) && targetPort == 47993 && delivered == payload) {
                actualPacket = std::move(packet); break;
            }
        }
        if (!actualPacket) Sleep(50);
    }
    PacketBytes reply = *actualPacket;
    const auto headerLength = static_cast<std::size_t>((reply[0] & 15) * 4);
    std::swap_ranges(reply.begin() + 12, reply.begin() + 16, reply.begin() + 16);
    std::swap(reply[headerLength], reply[headerLength + 2]);
    std::swap(reply[headerLength + 1], reply[headerLength + 3]);
    // A zero UDP checksum is valid for IPv4. Header checksum is recomputed
    // after swapping addresses, the exact nonce bytes remain unchanged.
    reply[headerLength + 6] = 0; reply[headerLength + 7] = 0;
    reply[10] = 0; reply[11] = 0;
    const auto checksum = ipv4HeaderChecksum(std::span<const std::uint8_t>(reply.data(), headerLength));
    reply[10] = static_cast<std::uint8_t>(checksum >> 8); reply[11] = static_cast<std::uint8_t>(checksum);
    DropReason reason{};
    check(outgoing.tryEnqueue(a2, std::move(reply), reason), "peer reply was rejected by production queue");
    require(peer.exchangeData(outgoing, incoming, outDrops, inDrops, true));
    std::array<char, 2048> received{};
    for (;;) {
        check(GetTickCount64() < deadline, "UDP response did not reach original Windows socket");
        const auto bytes = recv(socket.value, received.data(), static_cast<int>(received.size()), 0);
        if (bytes >= 0) {
            check(std::string_view(received.data(), static_cast<std::size_t>(bytes)) == payload, "UDP reply payload differs from per-run nonce");
            break;
        }
        check(WSAGetLastError() == WSAEWOULDBLOCK, "UDP receive failed: " + std::to_string(WSAGetLastError()));
        Sleep(20);
    }
    const auto after = client.status();
    check(after.counters.ipv4PacketsSent > before.counters.ipv4PacketsSent &&
          after.counters.ipv4PacketsReceived > before.counters.ipv4PacketsReceived,
          "actual service did not account both Wintun directions");
    pass("actual UDP socket -> Wintun -> service -> PHP -> production .3 peer -> PHP -> Wintun -> socket, exact nonce");
}

void run(bool udp);
} // namespace

int main(int argc, char** argv) {
    if (argc == 2 && std::string_view(argv[1]) == "--cleanup-owned-phantom") {
        try {
            auto network = createPlatformNetConfig();
            const auto luid = require(network->interfaceLuidForAlias(adapterName));
            require(network->removeOwnedPhantomInterface(adapterName, L"{4b67d77c-70a2-49f6-97db-a5f4d6c8e2e1}", luid));
            pass("removed only the verified fixed test phantom PnP device");
            return 0;
        } catch (const std::exception& error) {
            std::cerr << "[ CLEANUP FAIL ] " << error.what() << '\n';
            return 1;
        }
    }
    if (argc != 2 || (std::string_view(argv[1]) != "--run-isolated" && std::string_view(argv[1]) != "--run-isolated-with-udp")) {
        std::cerr << "Explicit elevated runner required. Use --run-isolated or --run-isolated-with-udp.\n";
        return 2;
    }
    try { run(std::string_view(argv[1]) == "--run-isolated-with-udp"); return 0; }
    catch (const std::exception& error) { std::cerr << "[ FAIL ] " << error.what() << '\n'; return 1; }
}

namespace {
void run(bool udp) {
    Client client;
    const auto directory = executableDirectory();
    const auto config = directory / L"isolated-data" / L"config.json";
    check(!std::filesystem::exists(config), "isolated config already exists, controller never overwrites a prior fixture");
    auto network = createPlatformNetConfig();
    const auto baseline = require(network->snapshot());
    check(!network->interfaceLuidForAlias(adapterName), "fixed test adapter already exists");
    // Read-only live endpoint preflight occurs before registering anything.
    auto transport = require(createPlatformTransport());
    auto clock = createPlatformClock();
    ProbeClient probe(*client.crypto, *transport, *clock);
    auto defaults = makeNewWizardDocument();
    defaults.fields.serverUrl = relay;
    defaults.context.runtime.responseTimeoutSeconds = 3;
    require(probe.checkSecret(defaults.context, defaults.fields));
    pass("live PHP default-secret preflight");
    OwnedService service(directory / L"phpvpn-e2e-service.exe");
    service.start();
    client.awaitVpn("disconnected", "idle", 5000);
    check(readConfig(config) == makeDefaultEditorDocument(), "SCM startup did not create the shared default");
    const auto startupBytes = readConfig(config);
    service.stop();
    service.start();
    client.awaitVpn("disconnected", "idle", 5000);
    check(readConfig(config) == startupBytes, "second SCM startup changed existing bytes");
    auto legacy = require(Json::parse(startupBytes));
    for (auto& [name, value] : legacy.objectValue()) if (name == "masterSetup")
        std::erase_if(value.objectValue(), [](const auto& item) { return item.first.ends_with("InWizard"); });
    for (const auto& existing : {std::string(" \r\n") + Json::serialize(legacy), std::string("{invalid fixture json")}) {
        service.stop();
        {
            std::ofstream seeded(config, std::ios::binary | std::ios::trunc);
            seeded << existing;
            check(seeded.good(), "isolated existing-file seed failed");
        }
        service.start();
        client.awaitVpn("disconnected", "idle", 5000);
        check(readConfig(config) == existing, "SCM startup replaced an existing legacy or invalid document");
        const auto state = client.wizard(WizardCommand::get_config_state);
        check(state.result.find("availability")->stringValue() == (existing.front() == '{' ? "invalid" : "valid"), "existing fixture misclassified");
        if (existing.front() == '{') check(client.legacy(IpcCommand::connect, true).find("error") != nullptr, "invalid config Connect was accepted");
    }
    pass("SCM startup preserves legacy and invalid bytes with IPC available");
    check(DeleteFileW(config.c_str()) != FALSE, "isolated config deletion failed");
    const auto missing = client.wizard(WizardCommand::get_config_state);
    check(missing.result.find("availability")->stringValue() == "missing", "missing config was misclassified");
    const auto rejected = client.legacy(IpcCommand::connect, true);
    check(rejected.find("error") && rejected.find("error")->find("code") &&
          rejected.find("error")->find("code")->stringValue() == "config_not_found", "Connect without config did not report config_not_found");
    check(!std::filesystem::exists(config) && service.status().dwCurrentState == SERVICE_RUNNING, "missing Connect created config or stopped service");
    pass("SCM startup creates exact default, restart preserves bytes, missing Connect does not create");
    client.wizard(WizardCommand::create_default_config);
    check(readConfig(config) == startupBytes, "explicit create after deletion did not restore shared default");
    check(DeleteFileW(config.c_str()) != FALSE, "second isolated config deletion failed");
    service.stop();
    service.start();
    client.awaitVpn("disconnected", "idle", 5000);
    check(readConfig(config) == startupBytes, "restart after deletion did not restore shared default");
    auto view = require(WizardIpc::parseView(client.wizard(WizardCommand::open).result));
    check(view.mode == WizardMode::edit && view.availability == ConfigAvailability::valid && view.fields.has_value() && view.contractCount == 1,
          "Wizard did not open startup default in edit mode with one contract");
    check(view.fields->useDefaultServerSecretInWizard && view.fields->autoDetectIpAddressInWizard && view.fields->useDefaultAccessKeyInWizard,
          "startup default preferences were lost");
    client.session.sessionId = view.sessionId; client.session.serviceInstanceId = view.serviceInstanceId;
    client.session.connectionEpoch = view.connectionEpoch; client.session.fields = *view.fields;
    client.session.fields.serverUrl = relay;
    client.session.fields.serverSecret = std::string(kDefaultServerSecret);
    client.session.fields.virtualIp = "10.99.99.2";
    client.session.fields.autoDetectIpAddressInWizard = false;
    client.session.fields.accessKey = defaultAccessKey("10.99.99.2");
    client.checkNext(WizardCommand::check_url, WizardOperationKind::url);
    client.checkNext(WizardCommand::check_secret, WizardOperationKind::secret);
    client.checkNext(WizardCommand::check_access, WizardOperationKind::access);
    check(readConfig(config) == startupBytes, "Wizard Next wrote config before Finish");
    pass("three explicit service Wizard Next operations through real WinHTTP/PHP");
    client.session.commitId = client.uuid(); client.session.createDefaultContracts = true;
    client.wizard(WizardCommand::finish);
    const auto commitDeadline = GetTickCount64() + 10000;
    std::string revision;
    for (;;) {
        auto reply = client.wizard(WizardCommand::get_commit);
        const auto state = reply.result.find("commitState")->stringValue();
        if (state == "confirmed") {
            const auto* receipt = reply.result.find("receipt");
            check(receipt->find("commitId")->stringValue() == client.session.commitId, "Finish receipt commitId mismatch");
            revision = receipt->find("revisionToken")->stringValue();
            break;
        }
        check(state == "pending" && GetTickCount64() < commitDeadline, "Finish failed, unknown or timed out; no automatic retry");
        Sleep(25);
    }
    const auto published = readConfig(config);
    ConfigWarnings warnings;
    const auto stored = require(parseAndValidateConfig(published, warnings));
    check(stored->virtualIp.toString() == "10.99.99.2" && stored->contracts.size() == 13, "Finish default identity/contracts mismatch");
    const auto originalDom = require(Json::parse(startupBytes));
    const auto publishedDom = require(Json::parse(published));
    check(Json::serialize(*originalDom.find("communicationContracts")->find("10.99.99.3")) ==
          Json::serialize(*publishedDom.find("communicationContracts")->find("10.99.99.3")), "Finish replaced an existing payload key");
    client.wizard(WizardCommand::finish); // Same UUID and identical complete input.
    const auto repeated = client.wizard(WizardCommand::get_commit);
    check(repeated.result.find("commitState")->stringValue() == "confirmed" &&
          repeated.result.find("receipt")->find("revisionToken")->stringValue() == revision && readConfig(config) == published,
          "identical Finish changed its confirmed receipt or document");
    client.wizard(WizardCommand::close);
    client.awaitVpn("disconnected", "idle", 1000);
    pass("Finish publication, identical receipt replay, close and no implicit Connect");
    client.legacy(IpcCommand::connect);
    client.awaitVpn("connected", "running", 15000);
    const auto luid = require(network->interfaceLuidForAlias(adapterName));
    verifyNetwork(*network, baseline, luid, true);
    pass("real Wintun connected with exact adapter LUID, IPv4 and route");
    const auto protectedLive = network->removeOwnedPhantomInterface(adapterName, L"{4b67d77c-70a2-49f6-97db-a5f4d6c8e2e1}", luid);
    check(!protectedLive && protectedLive.error().code == ErrorCode::journal_ambiguous,
          "phantom cleanup did not refuse a live adapter");
    const auto wrongGuid = network->removeOwnedPhantomInterface(adapterName, L"{00000000-0000-0000-0000-000000000001}", luid);
    check(!wrongGuid && wrongGuid.error().code == ErrorCode::journal_ambiguous,
          "phantom cleanup did not refuse a mismatched GUID");
    verifyNetwork(*network, baseline, luid, true);
    pass("phantom cleanup refuses live devices and mismatched identity without network changes");
    if (udp) udpRoundtrip(client, stored);
    const auto disconnectSent = GetTickCount64();
    client.legacy(IpcCommand::disconnect, false, 1000);
    const auto accepted = GetTickCount64() - disconnectSent;
    check(accepted < 1000, "Disconnect acceptance exceeded 1000 ms");
    client.awaitVpn("disconnected", "idle", static_cast<DWORD>(5000 - accepted));
    check(GetTickCount64() - disconnectSent < 5000, "Disconnect teardown exceeded 5000 ms");
    verifyNetwork(*network, baseline, luid, false);
    std::cout << "[ EVIDENCE ] Disconnect accepted " << accepted << " ms, completed " << GetTickCount64() - disconnectSent << " ms\n";
    client.legacy(IpcCommand::connect);
    client.awaitVpn("connected", "running", 15000);
    const auto reconnectLuid = require(network->interfaceLuidForAlias(adapterName));
    verifyNetwork(*network, baseline, reconnectLuid, true);
    const auto stopped = service.stop();
    check(stopped < 5000, "connected SCM stop exceeded 5000 ms");
    verifyNetwork(*network, baseline, reconnectLuid, false);
    std::cout << "[ EVIDENCE ] connected SCM stop " << stopped << " ms\n";
    service.start();
    client.awaitVpn("disconnected", "idle", 5000);
    check(readConfig(config) == published, "SCM restart changed confirmed config");
    const auto afterRestart = client.wizard(WizardCommand::get_config_state);
    check(afterRestart.result.find("availability")->stringValue() == "valid", "restart did not retain valid configuration");
    check(afterRestart.result.find("serviceInstanceId")->stringValue() != view.serviceInstanceId, "restart retained old serviceInstanceId");
    pass("reconnect, SCM stop, restart without auto-connect and unchanged config");

    client.legacy(IpcCommand::connect);
    client.awaitVpn("connected", "running", 15000);
    const auto crashLuid = require(network->interfaceLuidForAlias(adapterName));
    verifyNetwork(*network, baseline, crashLuid, true);
    const auto journalPath = directory / L"isolated-data" / L"ownership.json";
    const auto journalBytes = readConfig(journalPath);
    const auto journal = require(OwnershipJournal::parse(journalBytes));
    check(journal.adapterName == "PHPVPN Plan3 E2E d6c8" && journal.adapterGuid == adapterGuid &&
          journal.interfaceLuid == crashLuid && journal.address.toString() == "10.99.99.2" &&
          journal.prefixLength == 24 && journal.routeDestination.toString() == "10.99.99.0/24" &&
          journal.adapterCreated && journal.addressApplied && journal.routeApplied,
          "connected ownership journal does not identify the exact owned test adapter/address/route");
    service.crashVerifiedProcess();
    // An abrupt process exit must leave the durable journal for next-start
    // recovery. The driver itself may already remove some network resources.
    check(readConfig(journalPath) == journalBytes, "deliberate crash did not preserve the connected ownership journal");
    check(readConfig(config) == published, "deliberate crash changed confirmed config");
    service.start();
    client.awaitVpn("disconnected", "idle", 5000);
    check(readConfig(config) == published, "crash recovery changed confirmed config");
    check(!std::filesystem::exists(journalPath), "restart failed to remove the recovered ownership journal");
    verifyNetwork(*network, baseline, crashLuid, false);
    check(!network->interfaceLuidForAlias(adapterName), "test adapter survived crash recovery");
    const auto recovered = client.wizard(WizardCommand::get_config_state);
    check(recovered.result.find("availability")->stringValue() == "valid" &&
          recovered.result.find("serviceInstanceId")->stringValue() != afterRestart.result.find("serviceInstanceId")->stringValue(),
          "crash restart did not expose a new instance with valid preserved configuration");
    pass("actual owned process crash and restart recover original LUID journal without auto-connect or config change");
    client.legacy(IpcCommand::connect);
    client.awaitVpn("connected", "running", 15000);
    const auto recoveredLuid = require(network->interfaceLuidForAlias(adapterName));
    verifyNetwork(*network, baseline, recoveredLuid, true);
    const auto recoveredDisconnect = GetTickCount64();
    client.legacy(IpcCommand::disconnect, false, 1000);
    const auto recoveredAcceptance = GetTickCount64() - recoveredDisconnect;
    check(recoveredAcceptance < 1000, "post-crash Disconnect acceptance exceeded 1000 ms");
    client.awaitVpn("disconnected", "idle", static_cast<DWORD>(5000 - recoveredAcceptance));
    check(GetTickCount64() - recoveredDisconnect < 5000, "post-crash Disconnect teardown exceeded 5000 ms");
    verifyNetwork(*network, baseline, recoveredLuid, false);
    check(!std::filesystem::exists(journalPath) && readConfig(config) == published,
          "post-crash Connect/Disconnect did not clean its journal or preserve config");
    pass("post-crash backend remains usable for another real Connect/Disconnect");
    service.stop(); service.cleanup();
    check(require(network->snapshot()).sameDefaultRouteAndDns(baseline), "final default route/DNS differs from baseline");
    if (!udp) std::cout << "[ NOT RUN ] actual UDP roundtrip, lifecycle-only mode selected\n";
    std::cout << "[ NOT RUN ] two users, sleep/resume and delayed DNS\n";
    std::cout << "ISOLATED SCM/WINTUN LIFECYCLE PASS\n";
}
} // namespace
