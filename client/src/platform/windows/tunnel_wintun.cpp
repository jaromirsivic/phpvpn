// Wintun L3 backend.
//
// client.md "Windows" requires that the library is loaded from the installation
// directory only, that the whole symbol allowlist resolves before anything is
// created and that the running driver version is recorded. A pointer into the
// driver ring never leaves this file: a received packet is copied into the
// caller buffer before the ring entry is released, and a sent packet is copied
// into the ring entry that was allocated for it.
//
// The read path waits on the session read event together with a manual stop
// event, so the input worker always leaves its blocking call promptly.

#include <windows.h>

#include <objbase.h>

#include <atomic>
#include <cstring>
#include <mutex>
#include <string>

#include "core/base/logger.h"
#include "platform/iface/tunnel.h"
#include "wintun.h"

namespace phpvpn {
namespace {

constexpr const char* kComponent = "wintun";

// The full symbol allowlist. Every entry has to resolve, otherwise the library
// is refused before an adapter is created.
struct WintunApi {
    WINTUN_CREATE_ADAPTER_FUNC* createAdapter = nullptr;
    WINTUN_OPEN_ADAPTER_FUNC* openAdapter = nullptr;
    WINTUN_CLOSE_ADAPTER_FUNC* closeAdapter = nullptr;
    WINTUN_DELETE_DRIVER_FUNC* deleteDriver = nullptr;
    WINTUN_GET_ADAPTER_LUID_FUNC* getAdapterLuid = nullptr;
    WINTUN_GET_RUNNING_DRIVER_VERSION_FUNC* getRunningDriverVersion = nullptr;
    WINTUN_SET_LOGGER_FUNC* setLogger = nullptr;
    WINTUN_START_SESSION_FUNC* startSession = nullptr;
    WINTUN_END_SESSION_FUNC* endSession = nullptr;
    WINTUN_GET_READ_WAIT_EVENT_FUNC* getReadWaitEvent = nullptr;
    WINTUN_RECEIVE_PACKET_FUNC* receivePacket = nullptr;
    WINTUN_RELEASE_RECEIVE_PACKET_FUNC* releaseReceivePacket = nullptr;
    WINTUN_ALLOCATE_SEND_PACKET_FUNC* allocateSendPacket = nullptr;
    WINTUN_SEND_PACKET_FUNC* sendPacket = nullptr;
};

bool parseGuid(const std::wstring& text, GUID& out) {
    if (text.size() != 38 || text.front() != L'{' || text.back() != L'}') {
        return false;
    }
    return CLSIDFromString(text.c_str(), &out) == NOERROR;
}

class WintunSession final : public Tunnel {
public:
    WintunSession(const WintunApi& api, WINTUN_SESSION_HANDLE session, HANDLE stopEvent)
        : api_(api), session_(session), stopEvent_(stopEvent) {
        readEvent_ = api_.getReadWaitEvent(session_);
    }

    Result<TunnelReadResult> receive(std::span<std::uint8_t> buffer,
                                     std::int64_t timeoutMs) override {
        for (;;) {
            if (stopped_.load()) {
                return TunnelReadResult{TunnelReadStatus::stopped, 0};
            }

            DWORD packetSize = 0;
            BYTE* packet = api_.receivePacket(session_, &packetSize);
            if (packet != nullptr) {
                TunnelReadResult result;
                if (packetSize <= buffer.size()) {
                    std::memcpy(buffer.data(), packet, packetSize);
                    result.status = TunnelReadStatus::packet;
                    result.size = packetSize;
                } else {
                    // The packet is released without being delivered; only the
                    // aggregated counter records that it was too large.
                    oversized_.fetch_add(1);
                    result.status = TunnelReadStatus::empty;
                }
                api_.releaseReceivePacket(session_, packet);
                return result;
            }

            const DWORD error = GetLastError();
            if (error != ERROR_NO_MORE_ITEMS) {
                return Error(ErrorCode::tunnel_io_failed, "the virtual interface could not be read");
            }

            HANDLE waitHandles[2] = {readEvent_, stopEvent_};
            const DWORD waited = WaitForMultipleObjects(2, waitHandles, FALSE,
                                                        static_cast<DWORD>(timeoutMs));
            if (waited == WAIT_OBJECT_0 + 1) {
                return TunnelReadResult{TunnelReadStatus::stopped, 0};
            }
            if (waited == WAIT_TIMEOUT) {
                return TunnelReadResult{TunnelReadStatus::empty, 0};
            }
            if (waited == WAIT_FAILED) {
                return Error(ErrorCode::tunnel_io_failed, "the read wait failed");
            }
        }
    }

    Status send(std::span<const std::uint8_t> packet) override {
        if (packet.empty() || packet.size() > WINTUN_MAX_IP_PACKET_SIZE) {
            return Error(ErrorCode::wire_invalid_packet, "the packet length is out of range");
        }

        // A full ring produces a bounded wait rather than a busy loop, and the
        // stop event always wins so the worker can be joined.
        for (int attempt = 0; attempt < 200; ++attempt) {
            if (stopped_.load()) {
                return Error(ErrorCode::cancelled, "the tunnel is stopping");
            }

            BYTE* slot = api_.allocateSendPacket(session_, static_cast<DWORD>(packet.size()));
            if (slot != nullptr) {
                std::memcpy(slot, packet.data(), packet.size());
                api_.sendPacket(session_, slot);
                return Status::success();
            }

            const DWORD error = GetLastError();
            if (error != ERROR_BUFFER_OVERFLOW) {
                return Error(ErrorCode::tunnel_io_failed, "the virtual interface could not be written");
            }
            if (WaitForSingleObject(stopEvent_, 5) == WAIT_OBJECT_0) {
                return Error(ErrorCode::cancelled, "the tunnel is stopping");
            }
        }

        return Error(ErrorCode::busy, "the virtual interface ring stayed full");
    }

    void signalStop() override {
        stopped_.store(true);
        SetEvent(stopEvent_);
    }

    std::uint64_t oversizedReceiveCount() const override { return oversized_.load(); }

private:
    const WintunApi& api_;
    WINTUN_SESSION_HANDLE session_ = nullptr;
    HANDLE readEvent_ = nullptr;
    HANDLE stopEvent_ = nullptr;
    std::atomic<bool> stopped_{false};
    std::atomic<std::uint64_t> oversized_{0};
};

class WintunDriver final : public TunnelDriver {
public:
    ~WintunDriver() override {
        endSession();
        closeAdapter();
        if (stopEvent_ != nullptr) {
            CloseHandle(stopEvent_);
            stopEvent_ = nullptr;
        }
        if (library_ != nullptr) {
            FreeLibrary(library_);
            library_ = nullptr;
        }
    }

    Status loadAndVerify(const std::wstring& installDirectory) override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (library_ != nullptr) {
            return Status::success();
        }

        std::wstring path = installDirectory;
        if (!path.empty() && path.back() != L'\\' && path.back() != L'/') {
            path += L'\\';
        }
        path += L"wintun.dll";

        // Only the installation directory is searched, so no directory on the
        // search path can substitute the library.
        library_ = LoadLibraryExW(path.c_str(), nullptr,
                                  LOAD_LIBRARY_SEARCH_APPLICATION_DIR |
                                      LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (library_ == nullptr) {
            return Error(ErrorCode::tunnel_library_missing,
                         "the tunnel driver library was not found next to the service");
        }

        if (!resolveAll()) {
            FreeLibrary(library_);
            library_ = nullptr;
            return Error(ErrorCode::tunnel_api_missing,
                         "the tunnel driver library does not export the expected interface");
        }

        stopEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (stopEvent_ == nullptr) {
            FreeLibrary(library_);
            library_ = nullptr;
            return Error(ErrorCode::tunnel_session_failed, "the stop event could not be created");
        }

        return Status::success();
    }

    Result<TunnelAdapterInfo> createAdapter(const std::wstring& name,
                                            const std::wstring& tunnelType,
                                            const std::wstring& requestedGuid) override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (library_ == nullptr) {
            return Error(ErrorCode::tunnel_library_missing, "the tunnel driver is not loaded");
        }
        if (adapter_ != nullptr) {
            return Error(ErrorCode::state_conflict, "an adapter is already open");
        }

        GUID guid{};
        const bool hasGuid = parseGuid(requestedGuid, guid);

        // An adapter that survived a previous run is reused instead of being
        // recreated, which keeps the interface identity and the firewall rules.
        bool created = false;
        adapter_ = api_.openAdapter(name.c_str());
        if (adapter_ == nullptr) {
            adapter_ = api_.createAdapter(name.c_str(), tunnelType.c_str(),
                                          hasGuid ? &guid : nullptr);
            created = true;
        }
        if (adapter_ == nullptr) {
            return Error(ErrorCode::tunnel_adapter_failed, "the virtual adapter could not be created");
        }

        driverVersion_ = api_.getRunningDriverVersion();
        if (driverVersion_ == 0) {
            api_.closeAdapter(adapter_);
            adapter_ = nullptr;
            return Error(ErrorCode::tunnel_driver_unsupported,
                         "the running tunnel driver version could not be determined");
        }

        NET_LUID luid{};
        api_.getAdapterLuid(adapter_, &luid);

        TunnelAdapterInfo info;
        info.interfaceLuid = luid.Value;
        info.adapterGuid = narrowGuid(requestedGuid);
        info.created = created;

        PHPVPN_LOG_INFO(kComponent, "the virtual adapter is available");
        return info;
    }

    Result<TunnelAdapterInfo> openAdapter(const std::wstring& name) override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (library_ == nullptr) {
            return Error(ErrorCode::tunnel_library_missing, "the tunnel driver is not loaded");
        }
        if (adapter_ != nullptr) {
            return Error(ErrorCode::state_conflict, "an adapter is already open");
        }

        adapter_ = api_.openAdapter(name.c_str());
        if (adapter_ == nullptr) {
            return Error(ErrorCode::tunnel_adapter_failed, "no such virtual adapter exists");
        }

        NET_LUID luid{};
        api_.getAdapterLuid(adapter_, &luid);

        TunnelAdapterInfo info;
        info.interfaceLuid = luid.Value;
        info.created = false;
        return info;
    }

    Result<std::shared_ptr<Tunnel>> startSession(std::uint32_t ringCapacity) override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (adapter_ == nullptr) {
            return Error(ErrorCode::tunnel_adapter_failed, "no virtual adapter is open");
        }
        if (session_ != nullptr) {
            return Error(ErrorCode::state_conflict, "a session is already running");
        }
        if (ringCapacity < WINTUN_MIN_RING_CAPACITY || ringCapacity > WINTUN_MAX_RING_CAPACITY) {
            return Error(ErrorCode::invalid_argument, "the ring capacity is out of range");
        }

        ResetEvent(stopEvent_);
        session_ = api_.startSession(adapter_, ringCapacity);
        if (session_ == nullptr) {
            return Error(ErrorCode::tunnel_session_failed, "the tunnel session could not be started");
        }

        tunnel_ = std::make_shared<WintunSession>(api_, session_, stopEvent_);
        return std::static_pointer_cast<Tunnel>(tunnel_);
    }

    void endSession() override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (tunnel_) {
            tunnel_->signalStop();
            tunnel_.reset();
        }
        if (session_ != nullptr) {
            api_.endSession(session_);
            session_ = nullptr;
        }
    }

    void closeAdapter() override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (adapter_ != nullptr) {
            api_.closeAdapter(adapter_);
            adapter_ = nullptr;
        }
    }

    std::uint32_t runningDriverVersion() const override { return driverVersion_; }

private:
    template <class T>
    bool resolve(T*& target, const char* name) {
        target = reinterpret_cast<T*>(
            reinterpret_cast<void*>(GetProcAddress(library_, name)));
        return target != nullptr;
    }

    bool resolveAll() {
        return resolve(api_.createAdapter, "WintunCreateAdapter") &&
               resolve(api_.openAdapter, "WintunOpenAdapter") &&
               resolve(api_.closeAdapter, "WintunCloseAdapter") &&
               resolve(api_.deleteDriver, "WintunDeleteDriver") &&
               resolve(api_.getAdapterLuid, "WintunGetAdapterLUID") &&
               resolve(api_.getRunningDriverVersion, "WintunGetRunningDriverVersion") &&
               resolve(api_.setLogger, "WintunSetLogger") &&
               resolve(api_.startSession, "WintunStartSession") &&
               resolve(api_.endSession, "WintunEndSession") &&
               resolve(api_.getReadWaitEvent, "WintunGetReadWaitEvent") &&
               resolve(api_.receivePacket, "WintunReceivePacket") &&
               resolve(api_.releaseReceivePacket, "WintunReleaseReceivePacket") &&
               resolve(api_.allocateSendPacket, "WintunAllocateSendPacket") &&
               resolve(api_.sendPacket, "WintunSendPacket");
    }

    static std::string narrowGuid(const std::wstring& text) {
        std::string out;
        out.reserve(text.size());
        for (wchar_t character : text) {
            out.push_back(character < 128 ? static_cast<char>(character) : '?');
        }
        return out;
    }

    std::mutex mutex_;
    HMODULE library_ = nullptr;
    WintunApi api_;
    WINTUN_ADAPTER_HANDLE adapter_ = nullptr;
    WINTUN_SESSION_HANDLE session_ = nullptr;
    HANDLE stopEvent_ = nullptr;
    std::shared_ptr<WintunSession> tunnel_;
    std::uint32_t driverVersion_ = 0;
};

}  // namespace

std::unique_ptr<TunnelDriver> createPlatformTunnelDriver() {
    return std::make_unique<WintunDriver>();
}

}  // namespace phpvpn
