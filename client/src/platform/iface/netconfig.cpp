#include "platform/iface/netconfig.h"

namespace phpvpn {

// Shared by every platform: the comparison only looks at the snapshot fields,
// never at how a platform collected them.
bool NetworkSnapshot::sameDefaultRouteAndDns(const NetworkSnapshot& other) const {
    if (hasDefaultRoute != other.hasDefaultRoute) {
        return false;
    }
    if (hasDefaultRoute && (defaultRouteInterfaceLuid != other.defaultRouteInterfaceLuid ||
                            defaultRouteMetric != other.defaultRouteMetric)) {
        return false;
    }
    return dnsServers == other.dnsServers;
}

}  // namespace phpvpn
