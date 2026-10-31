// Tunnel driver of Android over AndroidVpnContext (plan-android-v001 points 1,
// 2, 3, 17 and 18, "Nativní část").
//
// loadAndVerify checks the VPN consent through the NetworkHost
// (tunnel_permission_missing) and that the VpnService registered its
// TunnelHost (tunnel_adapter_failed, "the VPN service is not running").
// createAdapter only allocates the synthetic identity (point 3); openAdapter is
// not_supported like on Linux. startSession establishes the interface from the
// recorded VpnSpec, immediately checks that the default route and the DNS
// servers of the underlying network still equal the baseline of the context
// (point 2: the core's own check after addRoute runs before anything exists on
// Android) and wraps the descriptor. endSession detaches the session and keeps
// the descriptor; closeAdapter closes it, after which Android removes the VPN
// network with its address and route, and waits at most 2 s for that. The ring
// capacity has no counterpart and runningDriverVersion is 0.
#pragma once

#include <functional>
#include <memory>

#include "core/base/result.h"
#include "platform/android/vpn_context.h"
#include "platform/iface/tunnel.h"

namespace phpvpn {

// The packet session over an established descriptor.
struct AndroidDescriptorSession {
    std::shared_ptr<Tunnel> tunnel;
    // Stops every user of the descriptor; runs before the descriptor closes.
    std::function<void()> detach;
};

// Injected so the host unit tests (T-N02) can drive the driver without a
// packet descriptor; production uses makeFdTunnelSession.
using AndroidTunnelFactory = std::function<Result<AndroidDescriptorSession>(int descriptor)>;

#if defined(__linux__)
// O_NONBLOCK and FD_CLOEXEC on the descriptor, an eventfd stop event and the
// FdTunnel shared with Linux (platform/posix/fd_tunnel.h): one read returns one
// IPv4 packet without header, like a TUN device with IFF_NO_PI.
Result<AndroidDescriptorSession> makeFdTunnelSession(int descriptor);
#endif

// Over `context`; a null context gives a driver whose every operation fails
// with tunnel_driver_unsupported (point 17).
std::unique_ptr<TunnelDriver> createAndroidTunnelDriver(std::shared_ptr<AndroidVpnContext> context,
                                                        AndroidTunnelFactory factory);

}  // namespace phpvpn
