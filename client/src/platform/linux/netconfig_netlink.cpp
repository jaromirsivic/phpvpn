// rtnetlink network configuration (plan-v001 section 4.4).
//
// Every route carries an explicit destination prefix, next hop, interface and
// metric in table main, and addresses are added with IFA_F_NOPREFIXROUTE, so the
// kernel never adds a route of its own. Interfaces are addressed by the platform
// identity of interface_identity.h; before every change the ifindex must still
// carry the ownership mark, so a recycled ifindex of a foreign interface is never
// touched. The snapshot feeds the fail-closed preflight and the verification that
// the default route and the DNS servers stayed untouched.

#include "platform/linux/netconfig_netlink.h"

#include <arpa/inet.h>
#include <net/if.h>
#include <netdb.h>
#include <sys/socket.h>

#include <linux/if_tun.h>
#include <linux/rtnetlink.h>

#include <cerrno>
#include <map>
#include <memory>
#include <tuple>

#include "core/base/utf8.h"
#include "platform/linux/interface_identity.h"
#include "platform/linux/netlink_socket.h"

namespace phpvpn {
namespace {

Error applyFailed(const std::string& what, int error) {
    return Error(ErrorCode::netconfig_apply_failed, what + " (" + netlink::errnoText(error) + ")");
}

Error ambiguous(const char* what) { return Error(ErrorCode::journal_ambiguous, what); }

Result<netlink::Socket> openSocket() {
    return netlink::Socket::open(ErrorCode::netconfig_apply_failed);
}

// The same construction as the Windows snapshot: only prefixes the shared
// Ipv4Cidr type can express keep their value, every other prefix stays at the
// default. The preflight only compares prefixes of the VPN prefix length.
Ipv4Cidr cidrOf(Ipv4Address network, int prefixLength) {
    const auto parsed = Ipv4Cidr::parse(network.toString() + "/" + std::to_string(prefixLength));
    return parsed.has_value() ? *parsed : Ipv4Cidr();
}

std::uint32_t tagOfLink(const netlink::LinkRecord& link) {
    return ownerTagForAlias(link.alias.value_or(std::string()));
}

// Whether the owned interface behind `identity` still exists with its mark.
// Changing a foreign interface is refused outright.
Result<bool> ownedInterfacePresent(netlink::Socket& socket, std::uint64_t identity) {
    const std::uint32_t tag = identityOwnerTag(identity);
    const int index = identityIfindex(identity);
    if (tag == 0 || index <= 0) {
        return Error(ErrorCode::netconfig_apply_failed, "the interface identity is not owned by this service");
    }
    std::optional<netlink::LinkRecord> link;
    PHPVPN_TRY_ASSIGN(link, socket.linkByIndex(index));
    return link.has_value() && tagOfLink(*link) == tag;
}

Status verifyPhantom(const netlink::LinkRecord& link, const std::string& name,
                     const std::string& marker) {
    if (link.name != name) return ambiguous("the recorded interface has another name");
    if (link.alias.value_or(std::string()) != marker) {
        return ambiguous("the recorded interface does not carry the ownership mark");
    }
    if (link.kind != "tun" || !link.tun.present || link.tun.type != IFF_TUN ||
        link.tun.multiQueue) {
        return ambiguous("the recorded interface is not a single queue TUN interface");
    }
    if (!link.tun.persist) return ambiguous("the recorded interface is not persistent");
    // The carrier of a TUN device is on exactly while a descriptor is attached,
    // independent of IFF_UP.
    if (!link.carrier.has_value() || *link.carrier != 0) {
        return ambiguous("the recorded interface is still attached to a process");
    }
    if (link.tun.owner.has_value() && *link.tun.owner != 0) {
        return ambiguous("the recorded interface belongs to another user");
    }
    return Status::success();
}

class NetlinkNetConfig final : public NetConfig {
public:
    explicit NetlinkNetConfig(DnsSnapshotOptions options) : options_(std::move(options)) {}

    Result<NetworkSnapshot> snapshot() override {
        auto opened = openSocket();
        if (!opened.ok()) return opened.error();
        netlink::Socket socket = std::move(opened).value();

        std::map<int, std::uint32_t> tags;
        std::vector<netlink::Bytes> messages;
        PHPVPN_TRY_ASSIGN(messages, socket.dump(netlink::encodeLinkDump));
        for (const auto& message : messages) {
            netlink::LinkRecord link;
            PHPVPN_TRY_ASSIGN(link, netlink::decodeLink(message));
            tags[link.index] = tagOfLink(link);
        }
        auto identityOf = [&tags](int index) {
            const auto found = tags.find(index);
            return composeInterfaceIdentity(found == tags.end() ? 0u : found->second, index);
        };

        NetworkSnapshot result;
        PHPVPN_TRY_ASSIGN(messages, socket.dump(netlink::encodeAddressDump));
        for (const auto& message : messages) {
            netlink::AddressRecord record;
            PHPVPN_TRY_ASSIGN(record, netlink::decodeAddress(message));
            if (record.family != AF_INET) continue;
            const auto address = record.local.has_value() ? record.local : record.address;
            if (!address.has_value()) continue;
            UnicastAddressEntry entry;
            entry.interfaceLuid = identityOf(record.index);
            entry.address = *address;
            entry.prefixLength = record.prefixLength;
            result.addresses.push_back(entry);
        }

        PHPVPN_TRY_ASSIGN(messages, socket.dump(netlink::encodeRouteDump));
        for (const auto& message : messages) {
            netlink::RouteRecord record;
            PHPVPN_TRY_ASSIGN(record, netlink::decodeRoute(message));
            // Unicast routes of every table except local, which holds the
            // local and broadcast entries, and no cached clones.
            if (record.family != AF_INET || record.type != RTN_UNICAST ||
                record.table == RT_TABLE_LOCAL || (record.flags & RTM_F_CLONED) != 0) {
                continue;
            }
            for (const auto& hop : record.nextHops) {
                RouteEntry entry;
                entry.interfaceLuid = identityOf(hop.oif);
                entry.destination = cidrOf(record.destination, record.destinationLength);
                entry.nextHop = hop.gateway.value_or(Ipv4Address(0));
                entry.metric = record.priority;
                result.routes.push_back(entry);

                // Default route: 0.0.0.0/0 of table main with the lowest metric.
                if (record.table == RT_TABLE_MAIN && record.destinationLength == 0 &&
                    (!result.hasDefaultRoute ||
                     std::tie(entry.metric, entry.interfaceLuid) <
                         std::tie(result.defaultRouteMetric, result.defaultRouteInterfaceLuid))) {
                    result.hasDefaultRoute = true;
                    result.defaultRouteInterfaceLuid = entry.interfaceLuid;
                    result.defaultRouteMetric = entry.metric;
                }
            }
        }

        PHPVPN_TRY_ASSIGN(result.dnsServers, collectDnsServers(options_));
        return result;
    }

    Result<bool> interfaceExists(std::uint64_t interfaceLuid) override {
        if (!interfaceLuid) return Error(ErrorCode::netconfig_apply_failed);
        auto opened = openSocket();
        if (!opened.ok()) return opened.error();
        std::optional<netlink::LinkRecord> link;
        PHPVPN_TRY_ASSIGN(link, opened->linkByIndex(identityIfindex(interfaceLuid)));
        if (!link.has_value()) return false;
        return tagOfLink(*link) == identityOwnerTag(interfaceLuid);
    }

    // Removes only a persistent, detached, root owned TUN interface whose name,
    // mark and identity all match; it cannot be produced by this client, which
    // never sets TUNSETPERSIST, so every other state fails closed.
    Status removeOwnedPhantomInterface(const std::wstring& alias,
                                       const std::wstring& expectedGuid,
                                       std::uint64_t interfaceLuid) override {
        const auto name = toUtf8(alias);
        const auto guid = canonicalInstallationGuid(expectedGuid);
        if (!name.ok() || name->empty() || name->size() >= IFNAMSIZ || !guid.ok()) {
            return ambiguous("the recorded interface identity is invalid");
        }
        const std::string marker = ownershipMarker(guid.value());
        const int index = identityIfindex(interfaceLuid);
        if (identityOwnerTag(interfaceLuid) != ownerTagForGuid(guid.value()) || index <= 0) {
            return ambiguous("the recorded interface identity does not match the GUID");
        }

        auto opened = openSocket();
        if (!opened.ok()) return opened.error();
        netlink::Socket socket = std::move(opened).value();

        std::optional<netlink::LinkRecord> link;
        PHPVPN_TRY_ASSIGN(link, socket.linkByIndex(index));
        if (!link.has_value()) {
            // Absent index alone is not enough: neither the name nor the mark
            // may exist under another index.
            std::optional<netlink::LinkRecord> byName;
            PHPVPN_TRY_ASSIGN(byName, socket.linkByName(name.value()));
            if (byName.has_value()) return ambiguous("the interface name exists under another identity");
            std::vector<netlink::Bytes> messages;
            PHPVPN_TRY_ASSIGN(messages, socket.dump(netlink::encodeLinkDump));
            for (const auto& message : messages) {
                netlink::LinkRecord other;
                PHPVPN_TRY_ASSIGN(other, netlink::decodeLink(message));
                if (other.alias.value_or(std::string()) == marker) {
                    return ambiguous("the ownership mark exists under another identity");
                }
            }
            return Status::success();
        }
        PHPVPN_TRY(verifyPhantom(*link, name.value(), marker));

        // Recheck as late as possible; netlink has no conditional delete.
        PHPVPN_TRY_ASSIGN(link, socket.linkByIndex(index));
        if (!link.has_value()) return Status::success();
        PHPVPN_TRY(verifyPhantom(*link, name.value(), marker));

        netlink::Reply reply;
        PHPVPN_TRY_ASSIGN(reply, socket.request([index](std::uint32_t sequence) {
            return netlink::encodeLinkDelete(sequence, index);
        }));
        if (reply.error != 0 && reply.error != ENODEV) {
            return applyFailed("the owned phantom interface could not be removed", reply.error);
        }
        return Status::success();
    }

    Status addUnicastAddress(const UnicastAddressEntry& entry) override {
        if (entry.prefixLength < 0 || entry.prefixLength > 32) {
            return Error(ErrorCode::invalid_argument, "the prefix length is out of range");
        }
        auto opened = openSocket();
        if (!opened.ok()) return opened.error();
        netlink::Socket socket = std::move(opened).value();
        bool present = false;
        PHPVPN_TRY_ASSIGN(present, ownedInterfacePresent(socket, entry.interfaceLuid));
        if (!present) {
            return Error(ErrorCode::netconfig_apply_failed, "the tunnel interface no longer exists");
        }

        const netlink::AddressSpec spec{identityIfindex(entry.interfaceLuid), entry.address,
                                        static_cast<std::uint8_t>(entry.prefixLength)};
        netlink::Reply reply;
        PHPVPN_TRY_ASSIGN(reply, socket.request([&spec](std::uint32_t sequence) {
            return netlink::encodeAddressAdd(sequence, spec);
        }));
        if (reply.error == 0) return Status::success();
        if (reply.error != EEXIST) return applyFailed("the tunnel address could not be assigned", reply.error);

        // Already existing is success only for the exact same row.
        std::vector<netlink::Bytes> messages;
        PHPVPN_TRY_ASSIGN(messages, socket.dump(netlink::encodeAddressDump));
        for (const auto& message : messages) {
            netlink::AddressRecord record;
            PHPVPN_TRY_ASSIGN(record, netlink::decodeAddress(message));
            if (record.family == AF_INET && record.index == spec.index &&
                record.prefixLength == spec.prefixLength && record.local == spec.address) {
                return Status::success();
            }
        }
        return Error(ErrorCode::netconfig_apply_failed, "a conflicting tunnel address already exists");
    }

    Status removeUnicastAddress(const UnicastAddressEntry& entry) override {
        if (entry.prefixLength < 0 || entry.prefixLength > 32) {
            return Error(ErrorCode::invalid_argument, "the prefix length is out of range");
        }
        auto opened = openSocket();
        if (!opened.ok()) return opened.error();
        netlink::Socket socket = std::move(opened).value();
        bool present = false;
        PHPVPN_TRY_ASSIGN(present, ownedInterfacePresent(socket, entry.interfaceLuid));
        // The kernel removed the address together with the owned interface.
        if (!present) return Status::success();

        const netlink::AddressSpec spec{identityIfindex(entry.interfaceLuid), entry.address,
                                        static_cast<std::uint8_t>(entry.prefixLength)};
        netlink::Reply reply;
        PHPVPN_TRY_ASSIGN(reply, socket.request([&spec](std::uint32_t sequence) {
            return netlink::encodeAddressDelete(sequence, spec);
        }));
        if (reply.error != 0 && reply.error != EADDRNOTAVAIL && reply.error != ENODEV &&
            reply.error != ENOENT) {
            return applyFailed("the tunnel address could not be removed", reply.error);
        }
        return Status::success();
    }

    Status addRoute(const RouteEntry& entry) override {
        auto opened = openSocket();
        if (!opened.ok()) return opened.error();
        netlink::Socket socket = std::move(opened).value();
        bool present = false;
        PHPVPN_TRY_ASSIGN(present, ownedInterfacePresent(socket, entry.interfaceLuid));
        if (!present) {
            return Error(ErrorCode::netconfig_apply_failed, "the tunnel interface no longer exists");
        }

        const netlink::RouteSpec spec = specOf(entry);
        netlink::Reply reply;
        PHPVPN_TRY_ASSIGN(reply, socket.request([&spec](std::uint32_t sequence) {
            return netlink::encodeRouteAdd(sequence, spec);
        }));
        if (reply.error == 0) return Status::success();
        if (reply.error != EEXIST) return applyFailed("the tunnel route could not be added", reply.error);

        std::vector<netlink::Bytes> messages;
        PHPVPN_TRY_ASSIGN(messages, socket.dump(netlink::encodeRouteDump));
        for (const auto& message : messages) {
            netlink::RouteRecord record;
            PHPVPN_TRY_ASSIGN(record, netlink::decodeRoute(message));
            if (record.family != AF_INET || record.table != RT_TABLE_MAIN ||
                record.type != RTN_UNICAST || record.destinationLength != spec.destinationLength ||
                record.destination != spec.destination || record.priority != spec.metric ||
                record.nextHops.size() != 1) {
                continue;
            }
            const auto& hop = record.nextHops.front();
            if (hop.oif == spec.oif && hop.gateway.value_or(Ipv4Address(0)) == spec.gateway) {
                return Status::success();
            }
        }
        return Error(ErrorCode::netconfig_apply_failed, "a conflicting tunnel route already exists");
    }

    Status removeRoute(const RouteEntry& entry) override {
        auto opened = openSocket();
        if (!opened.ok()) return opened.error();
        netlink::Socket socket = std::move(opened).value();
        bool present = false;
        PHPVPN_TRY_ASSIGN(present, ownedInterfacePresent(socket, entry.interfaceLuid));
        if (!present) return Status::success();

        const netlink::RouteSpec spec = specOf(entry);
        netlink::Reply reply;
        PHPVPN_TRY_ASSIGN(reply, socket.request([&spec](std::uint32_t sequence) {
            return netlink::encodeRouteDelete(sequence, spec);
        }));
        if (reply.error != 0 && reply.error != ESRCH && reply.error != ENOENT &&
            reply.error != ENODEV) {
            return applyFailed("the tunnel route could not be removed", reply.error);
        }
        return Status::success();
    }

    Result<std::uint64_t> interfaceLuidForAlias(const std::wstring& alias) override {
        std::string name;
        PHPVPN_TRY_ASSIGN(name, toUtf8(alias));
        auto opened = openSocket();
        if (!opened.ok()) return opened.error();
        std::optional<netlink::LinkRecord> link;
        PHPVPN_TRY_ASSIGN(link, opened->linkByName(name));
        if (!link.has_value()) {
            return Error(ErrorCode::netconfig_apply_failed, "the tunnel interface could not be resolved");
        }
        return composeInterfaceIdentity(tagOfLink(*link), link->index);
    }

    Result<bool> isHostReachableOutsideInterface(const std::string& host,
                                                 std::uint64_t excludeLuid) override {
        addrinfo hints{};
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        addrinfo* resolved = nullptr;
        if (getaddrinfo(host.c_str(), nullptr, &hints, &resolved) != 0 || resolved == nullptr) {
            // A name that cannot be resolved is not treated as reachable, so the
            // caller keeps the fail-closed behaviour.
            return false;
        }
        std::unique_ptr<addrinfo, decltype(&freeaddrinfo)> guard(resolved, &freeaddrinfo);

        auto opened = openSocket();
        if (!opened.ok()) return opened.error();
        netlink::Socket socket = std::move(opened).value();
        for (const addrinfo* entry = resolved; entry != nullptr; entry = entry->ai_next) {
            if (entry->ai_family != AF_INET || entry->ai_addr == nullptr) continue;
            const auto* address = reinterpret_cast<const sockaddr_in*>(entry->ai_addr);
            const Ipv4Address destination(ntohl(address->sin_addr.s_addr));

            netlink::Reply reply;
            PHPVPN_TRY_ASSIGN(reply, socket.request([destination](std::uint32_t sequence) {
                return netlink::encodeRouteGet(sequence, destination);
            }));
            // Unreachable destinations report an error; try the next address.
            if (reply.error != 0 || reply.messages.empty()) continue;
            netlink::RouteRecord route;
            PHPVPN_TRY_ASSIGN(route, netlink::decodeRoute(reply.messages.front()));
            if (route.nextHops.empty() || route.nextHops.front().oif <= 0) continue;
            const int oif = route.nextHops.front().oif;
            std::optional<netlink::LinkRecord> link;
            PHPVPN_TRY_ASSIGN(link, socket.linkByIndex(oif));
            const std::uint32_t tag = link.has_value() ? tagOfLink(*link) : 0u;
            if (composeInterfaceIdentity(tag, oif) != excludeLuid) return true;
        }
        return false;
    }

private:
    static netlink::RouteSpec specOf(const RouteEntry& entry) {
        netlink::RouteSpec spec;
        spec.oif = identityIfindex(entry.interfaceLuid);
        spec.destination = entry.destination.network();
        spec.destinationLength = static_cast<std::uint8_t>(entry.destination.prefixLength());
        spec.gateway = entry.nextHop;
        spec.metric = entry.metric;
        return spec;
    }

    DnsSnapshotOptions options_;
};

}  // namespace

std::unique_ptr<NetConfig> createNetlinkNetConfig(DnsSnapshotOptions options) {
    return std::make_unique<NetlinkNetConfig>(std::move(options));
}

std::unique_ptr<NetConfig> createPlatformNetConfig() {
    return createNetlinkNetConfig(DnsSnapshotOptions{});
}

}  // namespace phpvpn
