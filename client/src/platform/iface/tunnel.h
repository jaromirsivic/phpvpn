// Virtual L3 interface boundary.
//
// One sequential packet source for the input worker and one sequential packet
// sink for the output worker, client.md "Sekvencni I/O a vlastnictvi paketu".
// The pointer into the driver ring never leaves the platform layer.
#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "core/base/result.h"
#include "core/protocol/ipv4.h"

namespace phpvpn {

enum class TunnelReadStatus {
    packet,     // a packet was copied into the caller buffer
    stopped,    // the stop event was signalled
    empty,      // the wait timed out without a packet
};

struct TunnelReadResult {
    TunnelReadStatus status = TunnelReadStatus::empty;
    std::size_t size = 0;
};

class Tunnel {
public:
    virtual ~Tunnel() = default;

    // Blocks until a packet arrives, the stop event fires or `timeoutMs`
    // elapses. Copies at most `buffer.size()` bytes; an oversized packet is
    // released and counted without being delivered.
    virtual Result<TunnelReadResult> receive(std::span<std::uint8_t> buffer,
                                             std::int64_t timeoutMs) = 0;

    // Allocates, copies and sends exactly one already validated packet. A full
    // ring produces a bounded interruptible wait, never a busy loop.
    virtual Status send(std::span<const std::uint8_t> packet) = 0;

    // Wakes every blocked receive and send so the workers can be joined.
    virtual void signalStop() = 0;

    // Aggregated counter of packets the driver reported as too large.
    virtual std::uint64_t oversizedReceiveCount() const = 0;
};

struct TunnelAdapterInfo {
    std::uint64_t interfaceLuid = 0;
    std::string adapterGuid;
    bool created = false;  // false when an existing adapter was opened
};

// Owns the driver library, the adapter and the single session.
class TunnelDriver {
public:
    virtual ~TunnelDriver() = default;

    // Loads the library from the installation directory and resolves the whole
    // allowlist without creating an adapter.
    virtual Status loadAndVerify(const std::wstring& installDirectory) = 0;

    virtual Result<TunnelAdapterInfo> createAdapter(const std::wstring& name,
                                                    const std::wstring& tunnelType,
                                                    const std::wstring& requestedGuid) = 0;

    virtual Result<TunnelAdapterInfo> openAdapter(const std::wstring& name) = 0;

    // Starts the single session with the 4 MiB ring required by client.md.
    virtual Result<std::shared_ptr<Tunnel>> startSession(std::uint32_t ringCapacity) = 0;

    virtual void endSession() = 0;
    virtual void closeAdapter() = 0;

    virtual std::uint32_t runningDriverVersion() const = 0;
};

std::unique_ptr<TunnelDriver> createPlatformTunnelDriver();

}  // namespace phpvpn
