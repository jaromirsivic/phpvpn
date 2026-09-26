// DNS part of the Linux network snapshot (plan-v001 section 4.4).
//
// The list combines the IPv4 servers of systemd-resolved (Manager.DNS over the
// system bus, only when org.freedesktop.resolve1 has an owner, so the service is
// never activated) with the nameserver lines of /etc/resolv.conf. client.md does
// not assume a particular network manager, so a missing resolved is not an
// error, while an unreadable resolv.conf fails the snapshot closed. The result
// is sorted and free of duplicates, like the Windows snapshot.
#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "core/base/result.h"

namespace phpvpn {

struct DnsSnapshotOptions {
    // Overridable for the tests; production always uses /etc/resolv.conf.
    std::filesystem::path resolvConfPath = "/etc/resolv.conf";
    bool queryResolved = true;
};

// Bounded read of resolv.conf: the file must be a regular file (after the usual
// symlink resolution, since distributions link it into /run) of at most 64 KiB.
// A missing file means no configured nameserver.
Result<std::vector<std::string>> readResolvConfNameservers(const std::filesystem::path& path);

// IPv4 nameserver addresses in canonical dotted form, in file order.
std::vector<std::string> parseResolvConfNameservers(std::string_view text);

// IPv4 servers known to systemd-resolved; empty when the service or the bus is
// unavailable.
std::vector<std::string> resolvedDnsServers();

Result<std::vector<std::string>> collectDnsServers(const DnsSnapshotOptions& options);

}  // namespace phpvpn
