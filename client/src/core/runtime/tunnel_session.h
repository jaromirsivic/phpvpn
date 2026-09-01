// Adapter, address and route lifecycle with an ownership journal.
//
// client.md "Vlastnictvi adapteru a session" makes the service the single owner
// of the adapter, of the assigned address and of the routes it created. The
// journal records exactly what was applied, so a service that was killed can
// clean up on the next start instead of leaving a stale address behind.
//
// The preflight is fail-closed: an address of the VPN prefix that already exists
// on a foreign interface, or a route into the prefix over a foreign interface,
// aborts the start. Nothing outside the tunnel is ever reconfigured, so the
// default route and the DNS servers must be identical before and after.
#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

#include "core/base/result.h"
#include "core/config/config.h"
#include "platform/iface/netconfig.h"
#include "platform/iface/tunnel.h"

namespace phpvpn {

// What the service applied and therefore has to remove again.
struct OwnershipJournal {
    std::string adapterName;
    std::string adapterGuid;
    std::uint64_t interfaceLuid = 0;
    bool adapterCreated = false;
    bool addressApplied = false;
    bool routeApplied = false;
    Ipv4Address address;
    int prefixLength = 0;
    Ipv4Cidr routeDestination;
    std::uint32_t routeMetric = 0;

    std::string serialize() const;
    static Result<OwnershipJournal> parse(std::string_view text);
};

struct TunnelSessionSettings {
    std::wstring adapterName = L"PHPVPN";
    std::wstring tunnelType = L"PHPVPN";
    // Stable adapter GUID so a restart reuses the same interface identity.
    std::wstring requestedGuid = L"{7d1b4d2f-1f34-4d0e-9a3e-2f6c9b5a41d8}";
    std::uint32_t ringCapacity = 4u * 1024 * 1024;
    std::uint32_t routeMetric = 5;
    std::filesystem::path journalPath;
    std::wstring installDirectory;
};

class TunnelSession {
public:
    TunnelSession(TunnelDriver& driver, NetConfig& netConfig);
    ~TunnelSession();

    TunnelSession(const TunnelSession&) = delete;
    TunnelSession& operator=(const TunnelSession&) = delete;

    // Removes whatever a previous, abnormally terminated run left behind. A
    // journal that cannot be parsed is reported instead of being guessed at.
    Status recoverPreviousRun(const TunnelSessionSettings& settings);

    // Runs the preflight, creates the adapter, applies the address and the
    // route, verifies that nothing else changed and starts the session.
    Status start(const ClientConfig& config, const TunnelSessionSettings& settings);

    // Removes everything this session applied, in the reverse order, and deletes
    // the journal only after every step succeeded.
    Status stop();

    const std::shared_ptr<Tunnel>& tunnel() const noexcept { return tunnel_; }
    bool active() const noexcept { return tunnel_ != nullptr; }
    std::uint64_t interfaceLuid() const noexcept { return journal_.interfaceLuid; }

private:
    Status preflight(const ClientConfig& config, std::uint64_t ownLuid);
    Status applyJournal(const TunnelSessionSettings& settings);
    Status removeApplied(const OwnershipJournal& journal);

    TunnelDriver& driver_;
    NetConfig& netConfig_;
    std::shared_ptr<Tunnel> tunnel_;
    OwnershipJournal journal_;
    std::filesystem::path journalPath_;
    NetworkSnapshot baseline_;
    bool hasBaseline_ = false;
};

}  // namespace phpvpn
