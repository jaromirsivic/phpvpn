// Linux factory of the rtnetlink NetConfig with explicit DNS snapshot options.
//
// createPlatformNetConfig() uses the default options; the tests pass their own
// resolv.conf path or disable the systemd-resolved query.
#pragma once

#include <memory>

#include "platform/iface/netconfig.h"
#include "platform/linux/dns_snapshot.h"

namespace phpvpn {

std::unique_ptr<NetConfig> createNetlinkNetConfig(DnsSnapshotOptions options);

}  // namespace phpvpn
