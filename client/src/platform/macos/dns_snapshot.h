// DNS part of the macOS network snapshot (plan-macos-v001 step 5).
//
// configd publishes the resolver configuration of the whole system as the
// dynamic store key State:/Network/Global/DNS; its ServerAddresses array lists
// the active servers. client.md does not assume a particular network setup, so
// a missing key or a missing array means no configured server, while a store
// that cannot be opened or read fails the snapshot closed (the ErrorCode the
// Linux snapshot uses for an unreadable resolv.conf). The result holds IPv4
// servers only, in canonical dotted form, sorted and free of duplicates, like
// the Windows and Linux snapshots.
#pragma once

#include <CoreFoundation/CoreFoundation.h>

#include <string>
#include <vector>

#include "core/base/result.h"

namespace phpvpn {

// IPv4 entries of a ServerAddresses array, canonical, sorted and without
// duplicates. IPv6 addresses (scoped or not), any other text and elements that
// are not strings are skipped; a null array or another CF type is empty.
std::vector<std::string> parseDnsServerAddresses(CFArrayRef servers);

// The IPv4 servers of State:/Network/Global/DNS from SCDynamicStore.
Result<std::vector<std::string>> collectDnsServers();

}  // namespace phpvpn
