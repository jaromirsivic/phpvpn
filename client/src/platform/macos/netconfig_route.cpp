// PF_ROUTE network configuration of macOS (plan-macos-v001, limitation 2 and
// step 5), the counterpart of platform/linux/netconfig_netlink.cpp.
//
// Interfaces are addressed by the platform identity of interface_identity.h;
// before every change the ifindex must still carry the ownership mark in its
// description, so a recycled ifindex of a foreign interface is never touched.
// The address goes onto the point-to-point utun with SIOCAIFADDR, local and
// destination address both the virtual IP and the mask of the VPN prefix; for
// a point-to-point interface the kernel then adds only the host route of that
// destination on the utun, never a prefix route. The VPN route is an RTM_ADD
// interface route (RTF_UP|RTF_STATIC, gateway the sockaddr_dl of the utun), or
// a gateway route (RTF_GATEWAY, pinned to the utun with RTA_IFP) for a next
// hop other than 0.0.0.0. macOS routes have no metric: the snapshot reports 0
// and an existing route counts as the same route when destination, mask,
// interface and next hop match; the metric of a RouteEntry is not applied.
// The snapshot feeds the fail-closed preflight and the verification that the
// default route and the DNS servers stayed untouched.

#include "platform/iface/netconfig.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/sockio.h>

#include <fcntl.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <net/if_dl.h>
#include <net/route.h>
#include <netdb.h>
#include <netinet/in.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstddef>
#include <cstring>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "core/base/utf8.h"
#include "platform/macos/dns_snapshot.h"
#include "core/base/logger.h"
#include "platform/macos/interface_identity.h"
#include "platform/macos/route_codec.h"
#include "platform/macos/route_socket.h"
#include "platform/posix/fd.h"

namespace phpvpn {
namespace {

using routing::RouteMessage;
using routing::RouteRequest;
using routing::RouteSocket;

// macOS keeps interface indexes in 16 bits (sdl_index, rtm_index).
constexpr int kMaxInterfaceIndex = 0xFFFF;

Error applyFailed(const std::string& what, int error) {
    return Error(ErrorCode::netconfig_apply_failed, what + " (" + routing::errnoText(error) + ")");
}

Error ambiguous(const char* what) { return Error(ErrorCode::journal_ambiguous, what); }

// The same construction as the Windows and Linux snapshots: only prefixes the
// shared Ipv4Cidr type can express keep their value, every other prefix stays
// at the default. The preflight only compares prefixes of the VPN prefix length.
Ipv4Cidr cidrOf(Ipv4Address network, int prefixLength) {
    const auto parsed = Ipv4Cidr::parse(network.toString() + "/" + std::to_string(prefixLength));
    return parsed.has_value() ? *parsed : Ipv4Cidr();
}

// AF_INET address of a socket address from getifaddrs, if it carries one.
std::optional<Ipv4Address> inetAddressOf(const sockaddr* address) {
    constexpr std::size_t kAddressOffset = offsetof(sockaddr_in, sin_addr);
    if (address == nullptr || address->sa_family != AF_INET ||
        static_cast<std::size_t>(address->sa_len) < kAddressOffset + sizeof(in_addr)) {
        return std::nullopt;
    }
    return Ipv4Address::fromNetworkBytes(reinterpret_cast<const std::uint8_t*>(address) +
                                         kAddressOffset);
}

// sdl_index of an AF_LINK socket address from getifaddrs, 0 for anything else.
int linkIndexOf(const sockaddr* address) {
    constexpr std::size_t kIndexOffset = offsetof(sockaddr_dl, sdl_index);
    if (address == nullptr || address->sa_family != AF_LINK ||
        static_cast<std::size_t>(address->sa_len) < kIndexOffset + sizeof(u_short)) {
        return 0;
    }
    u_short index = 0;
    std::memcpy(&index, reinterpret_cast<const std::uint8_t*>(address) + kIndexOffset, sizeof(index));
    return index;
}

// Prefix length of an interface address. The kernel keeps the mask with
// sin_len 8, so it is decoded like a routing message mask.
Result<int> prefixOfEntry(const ifaddrs& entry) {
    if (entry.ifa_netmask == nullptr) return 32;
    const auto* bytes = reinterpret_cast<const std::uint8_t*>(entry.ifa_netmask);
    return routing::prefixLengthOfNetmask(routing::netmaskFromSockaddr(
        std::span<const std::uint8_t>(bytes, entry.ifa_netmask->sa_len)));
}

using IfaddrsList = std::unique_ptr<ifaddrs, decltype(&freeifaddrs)>;

Result<IfaddrsList> readInterfaceAddresses() {
    ifaddrs* raw = nullptr;
    if (::getifaddrs(&raw) != 0) {
        const int error = errno;
        return applyFailed("the interface addresses could not be read", error);
    }
    return IfaddrsList(raw, &freeifaddrs);
}

// Tag of the named interface; an interface that vanished meanwhile carries no
// mark. Every other lookup failure stays an error.
Result<std::uint32_t> tagOfName(const std::string& name) {
    auto tag = ownerTagOfInterface(name);
    if (tag.ok()) return tag.value();
    if (::if_nametoindex(name.c_str()) == 0) return 0u;
    return tag.error();
}

// Owner tags by ifindex, read at most once per operation.
class TagCache {
public:
    // Identity of the interface with this index; `name` saves the name lookup
    // when the caller already knows it.
    Result<std::uint64_t> identity(int index, const char* name = nullptr) {
        if (index <= 0) return composeInterfaceIdentity(0, index);
        const auto found = tags_.find(index);
        if (found != tags_.end()) return composeInterfaceIdentity(found->second, index);
        std::uint32_t tag = 0;
        char buffer[IF_NAMESIZE] = {};
        if (name == nullptr) name = ::if_indextoname(static_cast<unsigned int>(index), buffer);
        // An index without a name vanished meanwhile and carries no mark.
        if (name != nullptr) PHPVPN_TRY_ASSIGN(tag, tagOfName(name));
        tags_[index] = tag;
        return composeInterfaceIdentity(tag, index);
    }

private:
    std::map<int, std::uint32_t> tags_;
};

// A change is only ever made on an identity that carries an ownership tag.
Status requireOwnedIdentity(std::uint64_t identity) {
    const int index = identityIfindex(identity);
    if (identityOwnerTag(identity) == 0 || index <= 0 || index > kMaxInterfaceIndex) {
        return Error(ErrorCode::netconfig_apply_failed,
                     "the interface identity is not owned by this service");
    }
    return Status::success();
}

// Whether the owned interface behind `identity` still exists with its mark.
// Changing a foreign interface is refused outright.
Result<bool> ownedInterfacePresent(std::uint64_t identity) {
    PHPVPN_TRY(requireOwnedIdentity(identity));
    return interfaceCarriesIdentity(identity);
}

// Name of the owned interface behind `identity`; nullopt when the identity no
// longer exists (the utun vanished, or its index now belongs to an interface
// without this mark).
Result<std::optional<std::string>> ownedInterfaceName(std::uint64_t identity) {
    PHPVPN_TRY(requireOwnedIdentity(identity));
    auto name = interfaceNameForIdentity(identity);
    if (name.ok()) {
        if (name->empty() || name->size() >= IFNAMSIZ) {
            return Error(ErrorCode::netconfig_apply_failed, "the tunnel interface name is invalid");
        }
        return std::optional<std::string>(std::move(name).value());
    }
    bool present = false;
    PHPVPN_TRY_ASSIGN(present, interfaceCarriesIdentity(identity));
    if (!present) return std::optional<std::string>();
    return name.error();
}

// Datagram socket for the interface address ioctls.
Result<UniqueFd> openControlSocket() {
    UniqueFd fd(::socket(AF_INET, SOCK_DGRAM, 0));
    if (!fd) {
        const int error = errno;
        return applyFailed("the interface control socket could not be opened", error);
    }
    if (::fcntl(fd.get(), F_SETFD, FD_CLOEXEC) != 0) {
        const int error = errno;
        return applyFailed("the interface control socket could not be marked close-on-exec", error);
    }
    return fd;
}

void copyInterfaceName(char (&target)[IFNAMSIZ], const std::string& name) {
    // Callers validated name.size() < IFNAMSIZ; the target is zeroed.
    std::memcpy(target, name.data(), std::min<std::size_t>(name.size(), IFNAMSIZ - 1));
}

sockaddr_in inetSockaddr(Ipv4Address address) {
    sockaddr_in result{};
    result.sin_len = static_cast<std::uint8_t>(sizeof(sockaddr_in));
    result.sin_family = AF_INET;
    std::uint8_t bytes[4] = {};
    address.toNetworkBytes(bytes);
    std::memcpy(&result.sin_addr, bytes, sizeof(bytes));
    return result;
}

// Prefix length of `address` on the interface `name`, nullopt when the
// interface does not hold that address.
Result<std::optional<int>> assignedPrefix(const std::string& name, Ipv4Address address) {
    IfaddrsList list(nullptr, &freeifaddrs);
    PHPVPN_TRY_ASSIGN(list, readInterfaceAddresses());
    for (const ifaddrs* entry = list.get(); entry != nullptr; entry = entry->ifa_next) {
        const auto value = inetAddressOf(entry->ifa_addr);
        if (!value.has_value() || *value != address || entry->ifa_name == nullptr ||
            name != entry->ifa_name) {
            continue;
        }
        int prefix = 0;
        PHPVPN_TRY_ASSIGN(prefix, prefixOfEntry(*entry));
        return std::optional<int>(prefix);
    }
    return std::optional<int>();
}

// Routes of the dump that describe the table instead of the kernel's caches:
// kept are the AF_INET routes the kernel did not clone (network, host, gateway,
// interface, reject and interface scoped duplicates); skipped are clones
// (RTF_WASCLONED: ARP entries and per destination host clones), link-layer
// entries (RTF_LLINFO) and the broadcast and multicast entries.
bool keptInSnapshot(const RouteMessage& route) {
    constexpr int kSkipped = routing::kFlagWasCloned | routing::kFlagLlinfo |
                             routing::kFlagBroadcast | routing::kFlagMulticast;
    return route.destinationFamily == AF_INET && route.destination.has_value() &&
           (route.flags & kSkipped) == 0;
}

// The request for the route of `entry`: destination and mask of the prefix and
// either the utun as the gateway or the next hop pinned to the utun.
RouteRequest routeRequestOf(const RouteEntry& entry, int type) {
    RouteRequest request;
    request.type = type;
    request.flags = RTF_UP | RTF_STATIC;
    request.destination = entry.destination.network();
    request.netmask = routing::netmaskForPrefix(entry.destination.prefixLength());
    const int index = identityIfindex(entry.interfaceLuid);
    if (entry.nextHop != Ipv4Address(0)) {
        request.flags |= RTF_GATEWAY;
        request.gateway = entry.nextHop;
        request.interfaceIndex = index;
    } else {
        request.gatewayInterfaceIndex = index;
    }
    return request;
}

// Whether the route the kernel holds for exactly the prefix of `entry` is this
// route: same destination, mask, interface and next hop (macOS keeps no metric
// to compare). An absent prefix is false.
Result<bool> exactRoutePresent(RouteSocket& routes, const RouteEntry& entry) {
    RouteRequest query;
    query.type = RTM_GET;
    query.destination = entry.destination.network();
    query.netmask = routing::netmaskForPrefix(entry.destination.prefixLength());
    query.interfaceIndex = 0;
    RouteMessage reply;
    PHPVPN_TRY_ASSIGN(reply, routes.request(query));
    if (reply.errnoValue == ESRCH) return false;
    if (reply.errnoValue != 0) return applyFailed("the tunnel route could not be queried", reply.errnoValue);

    if (reply.destination != entry.destination.network()) return false;
    const auto prefix = routing::routePrefixLength(reply);
    if (!prefix.ok() || prefix.value() != entry.destination.prefixLength()) return false;
    if (routing::routeInterfaceIndex(reply) != identityIfindex(entry.interfaceLuid)) return false;
    const bool viaGateway = (reply.flags & RTF_GATEWAY) != 0;
    if (entry.nextHop == Ipv4Address(0)) return !viaGateway;
    return viaGateway && reply.gateway == entry.nextHop;
}

// Absence of the identity alone is not enough for the phantom check: the mark
// may not exist under another index either.
Status requireMarkAbsent(std::uint32_t tag) {
    struct if_nameindex* raw = ::if_nameindex();
    if (raw == nullptr) {
        const int error = errno;
        return applyFailed("the interfaces could not be listed", error);
    }
    const std::unique_ptr<struct if_nameindex, decltype(&if_freenameindex)> list(raw, &if_freenameindex);
    for (const struct if_nameindex* entry = raw; entry->if_index != 0 && entry->if_name != nullptr;
         ++entry) {
        std::uint32_t found = 0;
        PHPVPN_TRY_ASSIGN(found, tagOfName(entry->if_name));
        if (found == tag) return ambiguous("the ownership mark exists under another identity");
    }
    return Status::success();
}

class RouteNetConfig final : public NetConfig {
public:
    // Every failure of a change or of the snapshot is logged once here (operation
    // and message, never an address of the configuration), because the core maps
    // the error to the public tunnel_start_failed without its detail.
    template <typename T>
    static T logged(const char* operation, T result) {
        if (!result.ok()) {
            PHPVPN_LOG_WARN("netconfig", std::string(operation) + " failed: " + result.error().message());
        }
        return result;
    }
    Result<NetworkSnapshot> snapshot() override { return logged("snapshot", snapshotImpl()); }
    Status addUnicastAddress(const UnicastAddressEntry& entry) override { return logged("add address", addUnicastAddressImpl(entry)); }
    Status removeUnicastAddress(const UnicastAddressEntry& entry) override { return logged("remove address", removeUnicastAddressImpl(entry)); }
    Status addRoute(const RouteEntry& entry) override { return logged("add route", addRouteImpl(entry)); }
    Status removeRoute(const RouteEntry& entry) override { return logged("remove route", removeRouteImpl(entry)); }
    Result<NetworkSnapshot> snapshotImpl() {
        NetworkSnapshot result;
        TagCache tags;

        IfaddrsList list(nullptr, &freeifaddrs);
        PHPVPN_TRY_ASSIGN(list, readInterfaceAddresses());
        // Indexes from the link-level entries of the same list.
        std::map<std::string, int> indexes;
        for (const ifaddrs* entry = list.get(); entry != nullptr; entry = entry->ifa_next) {
            const int index = linkIndexOf(entry->ifa_addr);
            if (index > 0 && entry->ifa_name != nullptr) indexes[entry->ifa_name] = index;
        }
        for (const ifaddrs* entry = list.get(); entry != nullptr; entry = entry->ifa_next) {
            const auto address = inetAddressOf(entry->ifa_addr);
            if (!address.has_value() || entry->ifa_name == nullptr) continue;
            const auto found = indexes.find(entry->ifa_name);
            const int index = found != indexes.end()
                                  ? found->second
                                  : static_cast<int>(::if_nametoindex(entry->ifa_name));
            if (index <= 0) continue;  // the interface vanished meanwhile
            UnicastAddressEntry row;
            PHPVPN_TRY_ASSIGN(row.interfaceLuid, tags.identity(index, entry->ifa_name));
            row.address = *address;
            PHPVPN_TRY_ASSIGN(row.prefixLength, prefixOfEntry(*entry));
            result.addresses.push_back(row);
        }

        std::vector<RouteMessage> routes;
        PHPVPN_TRY_ASSIGN(routes, RouteSocket::dump());
        for (const RouteMessage& route : routes) {
            if (!keptInSnapshot(route)) continue;
            int prefix = 0;
            PHPVPN_TRY_ASSIGN(prefix, routing::routePrefixLength(route));
            RouteEntry row;
            PHPVPN_TRY_ASSIGN(row.interfaceLuid, tags.identity(routing::routeInterfaceIndex(route)));
            row.destination = cidrOf(*route.destination, prefix);
            row.nextHop = (route.flags & RTF_GATEWAY) != 0 && route.gateway.has_value()
                              ? *route.gateway
                              : Ipv4Address(0);
            row.metric = 0;
            result.routes.push_back(row);

            // Default route: the unscoped 0.0.0.0/0 (the scoped duplicates
            // belong to the non-primary interfaces), the lowest identity if
            // the table ever held several.
            if (prefix == 0 && route.destination->value() == 0 &&
                (route.flags & routing::kFlagIfScope) == 0 &&
                (!result.hasDefaultRoute || row.interfaceLuid < result.defaultRouteInterfaceLuid)) {
                result.hasDefaultRoute = true;
                result.defaultRouteInterfaceLuid = row.interfaceLuid;
                result.defaultRouteMetric = 0;
            }
        }
        if (!result.hasDefaultRoute) PHPVPN_TRY(queryDefaultRoute(result, tags));

        PHPVPN_TRY_ASSIGN(result.dnsServers, collectDnsServers());
        return result;
    }

    Result<bool> interfaceExists(std::uint64_t interfaceLuid) override {
        if (!interfaceLuid) return Error(ErrorCode::netconfig_apply_failed);
        return interfaceCarriesIdentity(interfaceLuid);
    }

    // A utun cannot be persistent: it disappears with the descriptor of the
    // process that created it. An absent identity is therefore the only
    // removable state, and a live utun carrying the mark fails closed.
    Status removeOwnedPhantomInterface(const std::wstring& alias,
                                       const std::wstring& expectedGuid,
                                       std::uint64_t interfaceLuid) override {
        // The recorded alias is the logical adapter name, not a kernel name.
        const auto name = toUtf8(alias);
        const auto guid = canonicalInstallationGuid(expectedGuid);
        if (!name.ok() || name->empty() || !guid.ok()) {
            return ambiguous("the recorded interface identity is invalid");
        }
        const std::uint32_t tag = identityOwnerTag(interfaceLuid);
        const int index = identityIfindex(interfaceLuid);
        if (tag != ownerTagForGuid(guid.value()) || index <= 0 || index > kMaxInterfaceIndex) {
            return ambiguous("the recorded interface identity does not match the GUID");
        }

        bool present = false;
        PHPVPN_TRY_ASSIGN(present, interfaceCarriesIdentity(interfaceLuid));
        if (present) {
            return ambiguous("a live utun interface still carries the ownership mark of this installation");
        }
        PHPVPN_TRY(requireMarkAbsent(tag));
        return Status::success();
    }

    Status addUnicastAddressImpl(const UnicastAddressEntry& entry) {
        if (entry.prefixLength < 0 || entry.prefixLength > 32) {
            return Error(ErrorCode::invalid_argument, "the prefix length is out of range");
        }
        std::optional<std::string> name;
        PHPVPN_TRY_ASSIGN(name, ownedInterfaceName(entry.interfaceLuid));
        if (!name.has_value()) {
            return Error(ErrorCode::netconfig_apply_failed, "the tunnel interface no longer exists");
        }

        // SIOCAIFADDR also changes the mask of an address that already exists,
        // so an existing address is never passed to it: already existing is
        // success only for the exact same row.
        std::optional<int> existing;
        PHPVPN_TRY_ASSIGN(existing, assignedPrefix(*name, entry.address));
        if (existing.has_value()) {
            if (*existing == entry.prefixLength) return Status::success();
            return Error(ErrorCode::netconfig_apply_failed, "a conflicting tunnel address already exists");
        }

        UniqueFd control;
        PHPVPN_TRY_ASSIGN(control, openControlSocket());
        ifaliasreq request{};
        copyInterfaceName(request.ifra_name, *name);
        // ifra_broadaddr is the destination of a point-to-point interface.
        const sockaddr_in address = inetSockaddr(entry.address);
        const sockaddr_in mask = inetSockaddr(routing::netmaskForPrefix(entry.prefixLength));
        static_assert(sizeof(sockaddr_in) == sizeof(sockaddr));
        std::memcpy(&request.ifra_addr, &address, sizeof(address));
        std::memcpy(&request.ifra_broadaddr, &address, sizeof(address));
        std::memcpy(&request.ifra_mask, &mask, sizeof(mask));

        // Recheck as late as possible; the ioctl names the interface, not the
        // identity.
        PHPVPN_TRY(requireSameOwnedName(entry.interfaceLuid, *name));
        if (::ioctl(control.get(), SIOCAIFADDR, &request) == 0) return Status::success();
        const int error = errno;
        if (error != EEXIST) return applyFailed("the tunnel address could not be assigned", error);

        PHPVPN_TRY_ASSIGN(existing, assignedPrefix(*name, entry.address));
        if (existing == entry.prefixLength) return Status::success();
        return Error(ErrorCode::netconfig_apply_failed, "a conflicting tunnel address already exists");
    }

    Status removeUnicastAddressImpl(const UnicastAddressEntry& entry) {
        if (entry.prefixLength < 0 || entry.prefixLength > 32) {
            return Error(ErrorCode::invalid_argument, "the prefix length is out of range");
        }
        std::optional<std::string> name;
        PHPVPN_TRY_ASSIGN(name, ownedInterfaceName(entry.interfaceLuid));
        // The kernel removed the address together with the owned interface.
        if (!name.has_value()) return Status::success();

        UniqueFd control;
        PHPVPN_TRY_ASSIGN(control, openControlSocket());
        ifreq request{};
        copyInterfaceName(request.ifr_name, *name);
        const sockaddr_in address = inetSockaddr(entry.address);
        std::memcpy(&request.ifr_addr, &address, sizeof(address));
        if (::ioctl(control.get(), SIOCDIFADDR, &request) == 0) return Status::success();
        const int error = errno;
        if (error == EADDRNOTAVAIL) return Status::success();
        if (error == ENXIO) {
            bool present = false;
            PHPVPN_TRY_ASSIGN(present, interfaceCarriesIdentity(entry.interfaceLuid));
            if (!present) return Status::success();
        }
        return applyFailed("the tunnel address could not be removed", error);
    }

    Status addRouteImpl(const RouteEntry& entry) {
        bool present = false;
        PHPVPN_TRY_ASSIGN(present, ownedInterfacePresent(entry.interfaceLuid));
        if (!present) {
            return Error(ErrorCode::netconfig_apply_failed, "the tunnel interface no longer exists");
        }
        auto opened = RouteSocket::open();
        if (!opened.ok()) return opened.error();
        RouteSocket routes = std::move(opened).value();

        RouteMessage reply;
        PHPVPN_TRY_ASSIGN(reply, routes.request(routeRequestOf(entry, RTM_ADD)));
        if (reply.errnoValue == 0) return Status::success();
        if (reply.errnoValue != EEXIST) {
            return applyFailed("the tunnel route could not be added", reply.errnoValue);
        }

        // Already existing is success only for the exact same route.
        bool same = false;
        PHPVPN_TRY_ASSIGN(same, exactRoutePresent(routes, entry));
        if (same) return Status::success();
        return Error(ErrorCode::netconfig_apply_failed, "a conflicting tunnel route already exists");
    }

    Status removeRouteImpl(const RouteEntry& entry) {
        bool present = false;
        PHPVPN_TRY_ASSIGN(present, ownedInterfacePresent(entry.interfaceLuid));
        // The kernel removed the route together with the owned interface.
        if (!present) return Status::success();
        auto opened = RouteSocket::open();
        if (!opened.ok()) return opened.error();
        RouteSocket routes = std::move(opened).value();

        // RTM_DELETE matches destination and mask only, so the route of that
        // prefix must first prove to be this route. Any other route is not
        // this one and stays untouched.
        bool same = false;
        PHPVPN_TRY_ASSIGN(same, exactRoutePresent(routes, entry));
        if (!same) return Status::success();

        RouteMessage reply;
        PHPVPN_TRY_ASSIGN(reply, routes.request(routeRequestOf(entry, RTM_DELETE)));
        if (reply.errnoValue != 0 && reply.errnoValue != ESRCH) {
            return applyFailed("the tunnel route could not be removed", reply.errnoValue);
        }
        return Status::success();
    }

    // On macOS the configured adapter name is a logical name; only a kernel
    // interface name (utunN) resolves.
    Result<std::uint64_t> interfaceLuidForAlias(const std::wstring& alias) override {
        std::string name;
        PHPVPN_TRY_ASSIGN(name, toUtf8(alias));
        if (name.empty() || name.size() >= IFNAMSIZ || ::if_nametoindex(name.c_str()) == 0) {
            return Error(ErrorCode::netconfig_apply_failed, "the tunnel interface could not be resolved");
        }
        return interfaceIdentityForName(name);
    }

    Result<bool> isHostReachableOutsideInterface(const std::string& host,
                                                 std::uint64_t excludeLuid) override {
        addrinfo hints{};
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        addrinfo* resolved = nullptr;
        if (::getaddrinfo(host.c_str(), nullptr, &hints, &resolved) != 0 || resolved == nullptr) {
            // A name that cannot be resolved is not treated as reachable, so the
            // caller keeps the fail-closed behaviour.
            return false;
        }
        const std::unique_ptr<addrinfo, decltype(&freeaddrinfo)> guard(resolved, &freeaddrinfo);

        auto opened = RouteSocket::open();
        if (!opened.ok()) return opened.error();
        RouteSocket routes = std::move(opened).value();
        TagCache tags;
        for (const addrinfo* entry = resolved; entry != nullptr; entry = entry->ai_next) {
            if (entry->ai_family != AF_INET || entry->ai_addr == nullptr ||
                entry->ai_addrlen < sizeof(sockaddr_in)) {
                continue;
            }
            sockaddr_in address{};
            std::memcpy(&address, entry->ai_addr, sizeof(address));

            RouteRequest query;
            query.type = RTM_GET;
            query.destination =
                Ipv4Address::fromNetworkBytes(reinterpret_cast<const std::uint8_t*>(&address.sin_addr));
            query.interfaceIndex = 0;
            RouteMessage reply;
            PHPVPN_TRY_ASSIGN(reply, routes.request(query));
            // Unreachable destinations report an error or a reject route;
            // try the next address.
            if (reply.errnoValue != 0 || (reply.flags & (RTF_REJECT | RTF_BLACKHOLE)) != 0) continue;
            const int index = routing::routeInterfaceIndex(reply);
            if (index <= 0) continue;
            std::uint64_t identity = 0;
            PHPVPN_TRY_ASSIGN(identity, tags.identity(index));
            if (identity != excludeLuid) return true;
        }
        return false;
    }

private:
    // Fallback when the dump held no unscoped default route: the exact
    // 0.0.0.0/0 lookup. ESRCH means there is none.
    static Status queryDefaultRoute(NetworkSnapshot& result, TagCache& tags) {
        auto opened = RouteSocket::open();
        if (!opened.ok()) return opened.error();
        RouteSocket routes = std::move(opened).value();

        RouteRequest query;
        query.type = RTM_GET;
        query.destination = Ipv4Address(0);
        query.netmask = Ipv4Address(0);
        query.interfaceIndex = 0;
        RouteMessage reply;
        PHPVPN_TRY_ASSIGN(reply, routes.request(query));
        if (reply.errnoValue == ESRCH) return Status::success();
        if (reply.errnoValue != 0) return applyFailed("the default route could not be queried", reply.errnoValue);

        const auto prefix = routing::routePrefixLength(reply);
        const int index = routing::routeInterfaceIndex(reply);
        if (reply.destination != Ipv4Address(0) || !prefix.ok() || prefix.value() != 0 || index <= 0) {
            return Status::success();
        }
        PHPVPN_TRY_ASSIGN(result.defaultRouteInterfaceLuid, tags.identity(index));
        result.hasDefaultRoute = true;
        result.defaultRouteMetric = 0;
        return Status::success();
    }

    // The name used for an ioctl must still belong to the owned identity.
    static Status requireSameOwnedName(std::uint64_t identity, const std::string& name) {
        std::optional<std::string> current;
        PHPVPN_TRY_ASSIGN(current, ownedInterfaceName(identity));
        if (current != name) {
            return Error(ErrorCode::netconfig_apply_failed, "the tunnel interface no longer exists");
        }
        return Status::success();
    }
};

}  // namespace

std::unique_ptr<NetConfig> createPlatformNetConfig() { return std::make_unique<RouteNetConfig>(); }

}  // namespace phpvpn
