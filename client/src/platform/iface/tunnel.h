// Virtual L3 interface boundary.
//
// One sequential packet source for the input worker and one sequential packet
// sink for the output worker, client.md "Sekvencni I/O a vlastnictvi paketu".
// The pointer into the driver ring never leaves the platform layer.
//
// Platform meaning (the names follow the Windows origin): Windows uses Wintun
// with a ring, an adapter GUID and a LUID. Linux uses one non-persistent
// /dev/net/tun descriptor: the "adapter" is the TUN interface with the exact
// configured name, the GUID becomes the ownership mark "phpvpn-owner:<GUID>" in
// its ifalias, `interfaceLuid` is the identity described in netconfig.h, the
// ring capacity and the tunnel type are ignored, and the kernel removes the
// interface with its addresses and routes when the descriptor closes.
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
    // allowlist without creating an adapter. Linux: verifies that /dev/net/tun
    // is the character device 10:200 offering IFF_TUN|IFF_NO_PI; the directory
    // is unused.
    virtual Status loadAndVerify(const std::wstring& installDirectory) = 0;

    virtual Result<TunnelAdapterInfo> createAdapter(const std::wstring& name,
                                                    const std::wstring& tunnelType,
                                                    const std::wstring& requestedGuid) = 0;

    // Linux: always not_supported, an existing interface of that name is a
    // collision.
    virtual Result<TunnelAdapterInfo> openAdapter(const std::wstring& name) = 0;

    // Starts the single session with the 4 MiB ring required by client.md.
    virtual Result<std::shared_ptr<Tunnel>> startSession(std::uint32_t ringCapacity) = 0;

    // Linux: endSession stops and detaches the session but keeps the
    // descriptor, so the interface stays until closeAdapter closes it and
    // waits (bounded) until the kernel removed the interface.
    virtual void endSession() = 0;
    virtual void closeAdapter() = 0;

    // Linux: always 0, a TUN device has no driver version.
    virtual std::uint32_t runningDriverVersion() const = 0;
};

std::unique_ptr<TunnelDriver> createPlatformTunnelDriver();

}  // namespace phpvpn
