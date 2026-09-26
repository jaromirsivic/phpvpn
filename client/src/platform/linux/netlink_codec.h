// Pure rtnetlink message encoding and decoding.
//
// plan-v001 section 4.4 keeps the codec free of sockets and privileges, so the
// unit tests can feed it synthetic kernel answers: acknowledgements, errors,
// multipart dumps split over several datagrams and interrupted dumps. The
// transport in netlink_socket.h only moves the bytes.
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/base/result.h"
#include "core/protocol/ipv4.h"

namespace phpvpn::netlink {

using Bytes = std::vector<std::uint8_t>;

// One attribute of an rtattr or nlattr list. The nested and byte order flags are
// masked out of `type`; `payload` points into the parsed buffer.
struct Attribute {
    std::uint16_t type = 0;
    std::span<const std::uint8_t> payload;
};

// Strict parser: a length below the header, past the end of the buffer or a
// truncated tail is malformed instead of being skipped.
Result<std::vector<Attribute>> parseAttributes(std::span<const std::uint8_t> bytes);

const Attribute* findAttribute(const std::vector<Attribute>& attributes, std::uint16_t type);
std::optional<std::uint8_t> attributeU8(const std::vector<Attribute>& attributes, std::uint16_t type);
std::optional<std::uint32_t> attributeU32(const std::vector<Attribute>& attributes, std::uint16_t type);
// NUL terminated or unterminated string attribute, without the terminator.
std::optional<std::string> attributeString(const std::vector<Attribute>& attributes,
                                           std::uint16_t type);
std::optional<Ipv4Address> attributeIpv4(const std::vector<Attribute>& attributes,
                                         std::uint16_t type);

// Builds one request: the netlink header, one fixed family header and aligned
// attributes, optionally nested.
class MessageBuilder {
public:
    MessageBuilder(std::uint16_t type, std::uint16_t flags);

    void appendFixed(const void* data, std::size_t size);
    void appendAttribute(std::uint16_t type, const void* data, std::size_t size);
    void appendU8(std::uint16_t type, std::uint8_t value);
    void appendU32(std::uint16_t type, std::uint32_t value);
    // Adds the terminating NUL the kernel expects for names and aliases.
    void appendString(std::uint16_t type, std::string_view value);
    void appendIpv4(std::uint16_t type, Ipv4Address address);

    // Returns the offset to pass to endNested once the children are appended.
    std::size_t beginNested(std::uint16_t type);
    void endNested(std::size_t offset);

    Bytes finish(std::uint32_t sequence) const;

private:
    Bytes bytes_;
};

// One message of a datagram. `bytes` spans the whole message, header included.
struct Message {
    std::uint16_t type = 0;
    std::uint16_t flags = 0;
    std::uint32_t sequence = 0;
    std::uint32_t portId = 0;
    std::span<const std::uint8_t> bytes;
    std::span<const std::uint8_t> payload;
};

Result<std::vector<Message>> splitMessages(std::span<const std::uint8_t> datagram);

// Positive errno carried by an NLMSG_ERROR payload, 0 for an acknowledgement.
Result<int> parseErrorPayload(std::span<const std::uint8_t> payload);

// Collects the answer to one request across datagrams. Messages with another
// sequence number are stale answers to an earlier, timed out request and are
// ignored. A request completes with NLMSG_ERROR (acknowledgement or kernel
// error) and a dump additionally with NLMSG_DONE.
class ReplyCollector {
public:
    ReplyCollector(std::uint32_t sequence, bool dump);

    // True once the answer is complete. Malformed input and NLMSG_OVERRUN are
    // errors; data after completion is ignored.
    Result<bool> feed(std::span<const std::uint8_t> datagram);

    bool complete() const noexcept { return complete_; }
    // Positive errno reported by the kernel, 0 on success.
    int error() const noexcept { return error_; }
    // A dump message carried NLM_F_DUMP_INTR, so the result is inconsistent.
    bool interrupted() const noexcept { return interrupted_; }
    // Copies of every data message (header included) in arrival order.
    const std::vector<Bytes>& messages() const noexcept { return messages_; }

private:
    std::uint32_t sequence_ = 0;
    bool dump_ = false;
    bool complete_ = false;
    bool interrupted_ = false;
    int error_ = 0;
    std::vector<Bytes> messages_;
};

// ---- decoded objects -------------------------------------------------------

struct TunLinkData {
    bool present = false;         // IFLA_INFO_DATA of kind "tun" was reported
    std::uint8_t type = 0;        // IFF_TUN or IFF_TAP
    bool persist = false;
    bool multiQueue = false;
    bool packetInfo = false;
    std::optional<std::uint32_t> owner;  // absent when no owner was set
};

struct LinkRecord {
    int index = 0;
    std::uint16_t deviceType = 0;  // ARPHRD_*
    std::uint32_t flags = 0;       // IFF_*
    std::string name;
    std::optional<std::string> alias;
    std::optional<std::uint32_t> mtu;
    std::optional<std::uint8_t> carrier;
    std::string kind;  // IFLA_INFO_KIND, empty for devices without link ops
    TunLinkData tun;
};

struct AddressRecord {
    std::uint8_t family = 0;
    std::uint8_t prefixLength = 0;
    std::uint32_t flags = 0;  // IFA_FLAGS when present, else ifa_flags
    std::uint8_t scope = 0;
    int index = 0;
    std::optional<Ipv4Address> local;
    std::optional<Ipv4Address> address;
};

struct RouteNextHop {
    int oif = 0;
    std::optional<Ipv4Address> gateway;
};

struct RouteRecord {
    std::uint8_t family = 0;
    std::uint8_t destinationLength = 0;
    std::uint8_t protocol = 0;
    std::uint8_t scope = 0;
    std::uint8_t type = 0;
    std::uint32_t table = 0;  // RTA_TABLE when present, else rtm_table
    std::uint32_t flags = 0;
    Ipv4Address destination;
    std::uint32_t priority = 0;
    // One entry for a plain route, one per path for RTA_MULTIPATH.
    std::vector<RouteNextHop> nextHops;
};

Result<LinkRecord> decodeLink(std::span<const std::uint8_t> message);
Result<AddressRecord> decodeAddress(std::span<const std::uint8_t> message);
Result<RouteRecord> decodeRoute(std::span<const std::uint8_t> message);

// ---- requests ----------------------------------------------------------------

Bytes encodeLinkGetByIndex(std::uint32_t sequence, int index);
Bytes encodeLinkGetByName(std::uint32_t sequence, std::string_view name);
Bytes encodeLinkDump(std::uint32_t sequence);
Bytes encodeLinkDelete(std::uint32_t sequence, int index);

struct LinkChange {
    int index = 0;
    std::optional<std::string> alias;
    std::optional<std::uint32_t> mtu;
    std::optional<bool> up;
    std::optional<std::uint8_t> inet6AddrGenMode;
};
// RTM_NEWLINK on an existing index without NLM_F_CREATE, acknowledged.
Bytes encodeLinkChange(std::uint32_t sequence, const LinkChange& change);

struct AddressSpec {
    int index = 0;
    Ipv4Address address;
    std::uint8_t prefixLength = 0;
};
// RTM_NEWADDR with IFA_F_NOPREFIXROUTE and NLM_F_CREATE|NLM_F_EXCL.
Bytes encodeAddressAdd(std::uint32_t sequence, const AddressSpec& spec);
Bytes encodeAddressDelete(std::uint32_t sequence, const AddressSpec& spec);
Bytes encodeAddressDump(std::uint32_t sequence);

struct RouteSpec {
    int oif = 0;
    Ipv4Address destination;
    std::uint8_t destinationLength = 0;
    Ipv4Address gateway;  // 0.0.0.0 means on-link
    std::uint32_t metric = 0;
};
// Table main, RTPROT_STATIC, RT_SCOPE_LINK for an on-link route, otherwise
// RT_SCOPE_UNIVERSE with RTA_GATEWAY; RTA_OIF and RTA_PRIORITY always.
Bytes encodeRouteAdd(std::uint32_t sequence, const RouteSpec& spec);
Bytes encodeRouteDelete(std::uint32_t sequence, const RouteSpec& spec);
Bytes encodeRouteDump(std::uint32_t sequence);
// RTM_GETROUTE for the route the kernel would use towards `destination`.
Bytes encodeRouteGet(std::uint32_t sequence, Ipv4Address destination);

}  // namespace phpvpn::netlink
