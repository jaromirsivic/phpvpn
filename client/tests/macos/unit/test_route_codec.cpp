// macOS platform tests of the PF_ROUTE codec (plan-macos-v001 step 7).
//
// Pure encoding and decoding of routing messages. Nothing here opens a
// routing socket or needs privileges; kernel answers and sysctl NET_RT_DUMP
// buffers are built by hand in the exact wire layout of <net/route.h>.
#include "harness.h"

#include <sys/types.h>
#include <sys/socket.h>

#include <net/if_dl.h>
#include <net/route.h>
#include <netinet/in.h>

#include <cerrno>
#include <cstddef>
#include <cstring>
#include <initializer_list>
#include <span>
#include <vector>

#include "platform/macos/route_codec.h"

namespace phpvpn {
namespace {

using routing::Bytes;

template <class T>
T readAt(const Bytes& bytes, std::size_t offset) {
    T value{};
    std::memcpy(&value, bytes.data() + offset, sizeof(T));
    return value;
}

Ipv4Address ip(const char* text) { return *Ipv4Address::parse(text); }

// Appends `length` bytes of `data` and the zero padding to four bytes, with
// four bytes for a zero length, like the kernel.
void appendPadded(Bytes& bytes, const void* data, std::size_t length) {
    const auto* begin = static_cast<const std::uint8_t*>(data);
    bytes.insert(bytes.end(), begin, begin + length);
    const std::size_t padded = length == 0 ? 4 : (length + 3) & ~std::size_t{3};
    bytes.resize(bytes.size() + (padded - length), 0);
}

sockaddr_in inetSockaddr(const char* text) {
    sockaddr_in address{};
    address.sin_len = sizeof(sockaddr_in);
    address.sin_family = AF_INET;
    std::uint8_t bytes[4] = {};
    ip(text).toNetworkBytes(bytes);
    std::memcpy(&address.sin_addr, bytes, sizeof(bytes));
    return address;
}

// A trimmed kernel netmask: sa_len, an unreliable family byte, the port and
// the leading mask bytes.
Bytes trimmedMask(std::initializer_list<std::uint8_t> maskBytes, std::uint8_t family = 0xFF) {
    Bytes mask = {static_cast<std::uint8_t>(4 + maskBytes.size()), family, 0, 0};
    mask.insert(mask.end(), maskBytes.begin(), maskBytes.end());
    return mask;
}

rt_msghdr headerOf(const Bytes& message) { return readAt<rt_msghdr>(message, 0); }

Bytes kernelMessage(int type, int index, int flags, int addrs, const Bytes& addresses,
                    int sequence = 0, int pid = 0, int version = RTM_VERSION, int error = 0) {
    rt_msghdr header{};
    header.rtm_msglen = static_cast<u_short>(sizeof(header) + addresses.size());
    header.rtm_version = static_cast<u_char>(version);
    header.rtm_type = static_cast<u_char>(type);
    header.rtm_index = static_cast<u_short>(index);
    header.rtm_flags = flags;
    header.rtm_addrs = addrs;
    header.rtm_pid = pid;
    header.rtm_seq = sequence;
    header.rtm_errno = error;
    Bytes message(sizeof(header));
    std::memcpy(message.data(), &header, sizeof(header));
    message.insert(message.end(), addresses.begin(), addresses.end());
    return message;
}

}  // namespace

PHPVPN_TEST(RouteCodec, EncodesInterfaceRouteWithLinkGateway) {
    routing::RouteRequest request;
    request.type = RTM_ADD;
    request.flags = RTF_UP | RTF_STATIC;
    request.sequence = 42;
    request.pid = 1234;
    request.destination = ip("10.8.0.0");
    request.netmask = routing::netmaskForPrefix(24);
    request.gatewayInterfaceIndex = 7;
    const Bytes message = routing::encode(request);

    constexpr std::size_t kHeader = sizeof(rt_msghdr);
    PHPVPN_CHECK_EQ(message.size(), kHeader + sizeof(sockaddr_in) + sizeof(sockaddr_dl) + sizeof(sockaddr_in));
    const rt_msghdr header = headerOf(message);
    PHPVPN_CHECK_EQ(static_cast<std::size_t>(header.rtm_msglen), message.size());
    PHPVPN_CHECK_EQ(static_cast<int>(header.rtm_version), RTM_VERSION);
    PHPVPN_CHECK_EQ(static_cast<int>(header.rtm_type), RTM_ADD);
    PHPVPN_CHECK_EQ(header.rtm_addrs, RTA_DST | RTA_GATEWAY | RTA_NETMASK);
    PHPVPN_CHECK_EQ(header.rtm_flags, RTF_UP | RTF_STATIC);
    PHPVPN_CHECK_EQ(header.rtm_seq, 42);
    PHPVPN_CHECK_EQ(static_cast<int>(header.rtm_pid), 1234);
    PHPVPN_CHECK_EQ(header.rtm_errno, 0);

    // RTA_DST: a full sockaddr_in right after the header.
    const auto destination = readAt<sockaddr_in>(message, kHeader);
    PHPVPN_CHECK_EQ(static_cast<int>(destination.sin_len), static_cast<int>(sizeof(sockaddr_in)));
    PHPVPN_CHECK_EQ(static_cast<int>(destination.sin_family), AF_INET);
    PHPVPN_CHECK_EQ(message[kHeader + 4], 10);
    PHPVPN_CHECK_EQ(message[kHeader + 5], 8);
    PHPVPN_CHECK_EQ(message[kHeader + 6], 0);
    PHPVPN_CHECK_EQ(message[kHeader + 7], 0);

    // RTA_GATEWAY: an address-less sockaddr_dl naming the interface by index.
    const std::size_t gatewayAt = kHeader + sizeof(sockaddr_in);
    const auto gateway = readAt<sockaddr_dl>(message, gatewayAt);
    PHPVPN_CHECK_EQ(static_cast<int>(gateway.sdl_len), static_cast<int>(sizeof(sockaddr_dl)));
    PHPVPN_CHECK_EQ(static_cast<int>(gateway.sdl_family), AF_LINK);
    PHPVPN_CHECK_EQ(static_cast<int>(gateway.sdl_index), 7);
    PHPVPN_CHECK_EQ(static_cast<int>(gateway.sdl_nlen), 0);
    PHPVPN_CHECK_EQ(static_cast<int>(gateway.sdl_alen), 0);

    // RTA_NETMASK: a full sockaddr_in, padded slots stay four byte aligned.
    const std::size_t maskAt = gatewayAt + sizeof(sockaddr_dl);
    PHPVPN_CHECK_EQ(maskAt % 4, std::size_t{0});
    const auto mask = readAt<sockaddr_in>(message, maskAt);
    PHPVPN_CHECK_EQ(static_cast<int>(mask.sin_len), static_cast<int>(sizeof(sockaddr_in)));
    PHPVPN_CHECK_EQ(message[maskAt + 4], 255);
    PHPVPN_CHECK_EQ(message[maskAt + 5], 255);
    PHPVPN_CHECK_EQ(message[maskAt + 6], 255);
    PHPVPN_CHECK_EQ(message[maskAt + 7], 0);

    // The codec reads its own request back.
    auto decoded = routing::decode(message);
    PHPVPN_REQUIRE_OK(decoded);
    PHPVPN_CHECK(decoded->destination == ip("10.8.0.0"));
    PHPVPN_CHECK(decoded->gatewayLinkIndex == 7);
    PHPVPN_CHECK(!decoded->gateway.has_value());
    PHPVPN_CHECK(decoded->netmask == ip("255.255.255.0"));
    PHPVPN_CHECK_EQ(routing::routePrefixLength(decoded.value()).valueOr(-1), 24);
    PHPVPN_CHECK(routing::isReplyTo(message, RTM_ADD, 42, 1234));
    PHPVPN_CHECK(!routing::isReplyTo(message, RTM_ADD, 43, 1234));
    PHPVPN_CHECK(!routing::isReplyTo(message, RTM_ADD, 42, 1235));
    PHPVPN_CHECK(!routing::isReplyTo(message, RTM_DELETE, 42, 1234));
}

PHPVPN_TEST(RouteCodec, EncodesGatewayRouteAndInterfaceQueries) {
    routing::RouteRequest add;
    add.type = RTM_ADD;
    add.flags = RTF_UP | RTF_STATIC | RTF_GATEWAY;
    add.destination = ip("10.9.0.0");
    add.netmask = routing::netmaskForPrefix(29);
    add.gateway = ip("10.8.0.1");
    add.gatewayInterfaceIndex = 7;  // ignored next to an inet gateway
    add.interfaceIndex = 7;
    const Bytes message = routing::encode(add);
    constexpr std::size_t kHeader = sizeof(rt_msghdr);
    PHPVPN_CHECK_EQ(message.size(), kHeader + 3 * sizeof(sockaddr_in) + sizeof(sockaddr_dl));
    PHPVPN_CHECK_EQ(headerOf(message).rtm_addrs, RTA_DST | RTA_GATEWAY | RTA_NETMASK | RTA_IFP);
    const auto gateway = readAt<sockaddr_in>(message, kHeader + sizeof(sockaddr_in));
    PHPVPN_CHECK_EQ(static_cast<int>(gateway.sin_family), AF_INET);
    PHPVPN_CHECK_EQ(message[kHeader + sizeof(sockaddr_in) + 7], 1);
    const auto ifp = readAt<sockaddr_dl>(message, kHeader + 3 * sizeof(sockaddr_in));
    PHPVPN_CHECK_EQ(static_cast<int>(ifp.sdl_family), AF_LINK);
    PHPVPN_CHECK_EQ(static_cast<int>(ifp.sdl_index), 7);

    // A longest match lookup: no netmask, the empty RTA_IFP query form.
    routing::RouteRequest get;
    get.type = RTM_GET;
    get.destination = ip("192.0.2.10");
    get.interfaceIndex = 0;
    const Bytes query = routing::encode(get);
    PHPVPN_CHECK_EQ(query.size(), kHeader + sizeof(sockaddr_in) + sizeof(sockaddr_dl));
    PHPVPN_CHECK_EQ(headerOf(query).rtm_addrs, RTA_DST | RTA_IFP);
    PHPVPN_CHECK_EQ(static_cast<int>(readAt<sockaddr_dl>(query, kHeader + sizeof(sockaddr_in)).sdl_index), 0);

    // The default route query sends a full zero mask, never sa_len 0.
    routing::RouteRequest defaultRoute;
    defaultRoute.type = RTM_GET;
    defaultRoute.destination = Ipv4Address(0);
    defaultRoute.netmask = Ipv4Address(0);
    const Bytes zeroMask = routing::encode(defaultRoute);
    PHPVPN_CHECK_EQ(headerOf(zeroMask).rtm_addrs, RTA_DST | RTA_NETMASK);
    PHPVPN_CHECK_EQ(static_cast<int>(zeroMask[kHeader + sizeof(sockaddr_in)]),
                    static_cast<int>(sizeof(sockaddr_in)));
}

PHPVPN_TEST(RouteCodec, DecodesKernelGetReply) {
    // Default route answer: DST 0.0.0.0, GATEWAY 192.168.1.1, a zero length
    // NETMASK, IFP en0 with its name and MAC address, IFA 192.168.1.10.
    Bytes addresses;
    const sockaddr_in destination = inetSockaddr("0.0.0.0");
    appendPadded(addresses, &destination, sizeof(destination));
    const sockaddr_in gateway = inetSockaddr("192.168.1.1");
    appendPadded(addresses, &gateway, sizeof(gateway));
    addresses.insert(addresses.end(), 4, 0);  // sa_len 0 still takes four bytes
    sockaddr_dl ifp{};
    ifp.sdl_family = AF_LINK;
    ifp.sdl_index = 4;
    ifp.sdl_nlen = 3;
    ifp.sdl_alen = 6;
    const std::uint8_t data[9] = {'e', 'n', '0', 0x02, 0x11, 0x22, 0x33, 0x44, 0x55};
    std::memcpy(ifp.sdl_data, data, sizeof(data));
    const std::size_t ifpLength = offsetof(sockaddr_dl, sdl_data) + sizeof(data);  // 17
    ifp.sdl_len = static_cast<u_char>(ifpLength);
    appendPadded(addresses, &ifp, ifpLength);
    const sockaddr_in local = inetSockaddr("192.168.1.10");
    appendPadded(addresses, &local, sizeof(local));

    const Bytes reply = kernelMessage(RTM_GET, 4, RTF_UP | RTF_GATEWAY | RTF_STATIC | RTF_DONE,
                                      RTA_DST | RTA_GATEWAY | RTA_NETMASK | RTA_IFP | RTA_IFA,
                                      addresses, 9, 4321);
    PHPVPN_CHECK(routing::isReplyTo(reply, RTM_GET, 9, 4321));
    auto decoded = routing::decode(reply);
    PHPVPN_REQUIRE_OK(decoded);
    PHPVPN_CHECK_EQ(decoded->type, RTM_GET);
    PHPVPN_CHECK_EQ(decoded->index, 4);
    PHPVPN_CHECK_EQ(decoded->sequence, 9);
    PHPVPN_CHECK_EQ(decoded->pid, 4321);
    PHPVPN_CHECK_EQ(decoded->errnoValue, 0);
    PHPVPN_CHECK_EQ(decoded->destinationFamily, AF_INET);
    PHPVPN_CHECK(decoded->destination == Ipv4Address(0));
    PHPVPN_CHECK(decoded->gateway == ip("192.168.1.1"));
    PHPVPN_CHECK(!decoded->gatewayLinkIndex.has_value());
    PHPVPN_CHECK(decoded->netmask == Ipv4Address(0));
    PHPVPN_CHECK(decoded->interfaceIndex == 4);
    PHPVPN_CHECK(decoded->interfaceAddress == ip("192.168.1.10"));
    PHPVPN_CHECK_EQ(routing::routePrefixLength(decoded.value()).valueOr(-1), 0);
    PHPVPN_CHECK_EQ(routing::routeInterfaceIndex(decoded.value()), 4);

    // rtm_index 0 falls back to the RTA_IFP index.
    const Bytes withoutIndex = kernelMessage(RTM_GET, 0, RTF_UP | RTF_GATEWAY,
                                             RTA_DST | RTA_GATEWAY | RTA_NETMASK | RTA_IFP | RTA_IFA,
                                             addresses);
    auto fallback = routing::decode(withoutIndex);
    PHPVPN_REQUIRE_OK(fallback);
    PHPVPN_CHECK_EQ(routing::routeInterfaceIndex(fallback.value()), 4);

    // A refused request echoes rtm_errno; a host lookup has no netmask.
    Bytes host;
    const sockaddr_in hostDestination = inetSockaddr("10.8.0.9");
    appendPadded(host, &hostDestination, sizeof(hostDestination));
    auto refused = routing::decode(kernelMessage(RTM_GET, 0, 0, RTA_DST, host, 3, 1, RTM_VERSION, ESRCH));
    PHPVPN_REQUIRE_OK(refused);
    PHPVPN_CHECK_EQ(refused->errnoValue, ESRCH);
    PHPVPN_CHECK(!refused->netmask.has_value());
    PHPVPN_CHECK_EQ(routing::routePrefixLength(refused.value()).valueOr(-1), 32);
}

PHPVPN_TEST(RouteCodec, DecodesDumpAndSkipsForeignRecords) {
    // 10.8.0.0/24 interface route on index 7, mask trimmed to sa_len 7.
    Bytes network;
    const sockaddr_in networkDestination = inetSockaddr("10.8.0.0");
    appendPadded(network, &networkDestination, sizeof(networkDestination));
    sockaddr_dl linkGateway{};
    linkGateway.sdl_len = sizeof(sockaddr_dl);
    linkGateway.sdl_family = AF_LINK;
    linkGateway.sdl_index = 7;
    appendPadded(network, &linkGateway, sizeof(linkGateway));
    const Bytes mask = trimmedMask({255, 255, 255});
    appendPadded(network, mask.data(), mask.size());
    const Bytes first = kernelMessage(RTM_GET, 7, RTF_UP | RTF_STATIC,
                                      RTA_DST | RTA_GATEWAY | RTA_NETMASK, network);

    // 10.8.0.1 host route without a netmask, gateway link index 7.
    Bytes hostAddresses;
    const sockaddr_in hostDestination = inetSockaddr("10.8.0.1");
    appendPadded(hostAddresses, &hostDestination, sizeof(hostDestination));
    appendPadded(hostAddresses, &linkGateway, sizeof(linkGateway));
    const Bytes second = kernelMessage(RTM_GET, 7, RTF_UP | RTF_HOST, RTA_DST | RTA_GATEWAY, hostAddresses);

    // A record of another version and one of another type follow; both are
    // skipped by their length.
    const Bytes foreignVersion = kernelMessage(RTM_GET, 7, RTF_UP, RTA_DST, hostAddresses, 0, 0, RTM_VERSION - 1);
    const Bytes foreignType = kernelMessage(RTM_ADD, 7, RTF_UP, RTA_DST, hostAddresses);

    Bytes dump;
    for (const Bytes* record : {&first, &second, &foreignType, &foreignVersion}) {
        dump.insert(dump.end(), record->begin(), record->end());
    }
    auto messages = routing::decodeDump(dump);
    PHPVPN_REQUIRE_OK(messages);
    PHPVPN_CHECK_EQ(messages->size(), std::size_t{2});
    if (messages->size() != 2) return;
    const auto& networkRoute = messages->at(0);
    PHPVPN_CHECK(networkRoute.destination == ip("10.8.0.0"));
    PHPVPN_CHECK(networkRoute.netmask == ip("255.255.255.0"));
    PHPVPN_CHECK(networkRoute.gatewayLinkIndex == 7);
    PHPVPN_CHECK_EQ(routing::routePrefixLength(networkRoute).valueOr(-1), 24);
    PHPVPN_CHECK_EQ(routing::routeInterfaceIndex(networkRoute), 7);
    const auto& hostRoute = messages->at(1);
    PHPVPN_CHECK(hostRoute.destination == ip("10.8.0.1"));
    PHPVPN_CHECK(!hostRoute.netmask.has_value());
    PHPVPN_CHECK_EQ(routing::routePrefixLength(hostRoute).valueOr(-1), 32);

    // An empty table and a clean end are fine; a record running past the end,
    // a zero length and a short tail are malformed.
    auto empty = routing::decodeDump(Bytes());
    PHPVPN_REQUIRE_OK(empty);
    PHPVPN_CHECK(empty->empty());
    Bytes truncated = first;
    truncated.resize(truncated.size() - 4);
    PHPVPN_REQUIRE_ERROR(routing::decodeDump(truncated), ErrorCode::netconfig_apply_failed);
    Bytes zeroLength = first;
    zeroLength[0] = 0;
    zeroLength[1] = 0;
    PHPVPN_REQUIRE_ERROR(routing::decodeDump(zeroLength), ErrorCode::netconfig_apply_failed);
    Bytes shortTail = first;
    shortTail.push_back(0);
    shortTail.push_back(0);
    PHPVPN_REQUIRE_ERROR(routing::decodeDump(shortTail), ErrorCode::netconfig_apply_failed);
}

PHPVPN_TEST(RouteCodec, MalformedMessagesAreErrors) {
    Bytes addresses;
    const sockaddr_in destination = inetSockaddr("10.8.0.0");
    appendPadded(addresses, &destination, sizeof(destination));
    const Bytes valid = kernelMessage(RTM_GET, 1, RTF_UP, RTA_DST, addresses);
    PHPVPN_REQUIRE_OK(routing::decode(valid));

    // Shorter than the header.
    PHPVPN_REQUIRE_ERROR(routing::decode(Bytes(valid.begin(), valid.begin() + 8)),
                         ErrorCode::netconfig_apply_failed);
    // rtm_msglen past the buffer.
    Bytes cut = valid;
    cut.resize(cut.size() - 1);
    PHPVPN_REQUIRE_ERROR(routing::decode(cut), ErrorCode::netconfig_apply_failed);
    // Another version or an interface message type.
    PHPVPN_REQUIRE_ERROR(routing::decode(kernelMessage(RTM_GET, 1, RTF_UP, RTA_DST, addresses, 0, 0, RTM_VERSION + 1)),
                         ErrorCode::netconfig_apply_failed);
    PHPVPN_REQUIRE_ERROR(routing::decode(kernelMessage(RTM_IFINFO, 1, RTF_UP, RTA_DST, addresses)),
                         ErrorCode::netconfig_apply_failed);
    // An address bit without an address, and an address longer than the rest.
    PHPVPN_REQUIRE_ERROR(routing::decode(kernelMessage(RTM_GET, 1, RTF_UP, RTA_DST | RTA_GATEWAY, addresses)),
                         ErrorCode::netconfig_apply_failed);
    Bytes overlong = addresses;
    overlong[0] = 32;
    PHPVPN_REQUIRE_ERROR(routing::decode(kernelMessage(RTM_GET, 1, RTF_UP, RTA_DST, overlong)),
                         ErrorCode::netconfig_apply_failed);
    // An AF_INET destination too short to hold sin_addr.
    Bytes shortInet = {6, AF_INET, 0, 0, 10, 8, 0, 0};
    PHPVPN_REQUIRE_ERROR(routing::decode(kernelMessage(RTM_GET, 1, RTF_UP, RTA_DST, shortInet)),
                         ErrorCode::netconfig_apply_failed);
}

PHPVPN_TEST(RouteCodec, NetmaskForms) {
    // Prefix to mask, clamped.
    PHPVPN_CHECK(routing::netmaskForPrefix(0) == Ipv4Address(0));
    PHPVPN_CHECK(routing::netmaskForPrefix(24) == ip("255.255.255.0"));
    PHPVPN_CHECK(routing::netmaskForPrefix(29) == ip("255.255.255.248"));
    PHPVPN_CHECK(routing::netmaskForPrefix(32) == ip("255.255.255.255"));
    PHPVPN_CHECK(routing::netmaskForPrefix(40) == ip("255.255.255.255"));
    PHPVPN_CHECK(routing::netmaskForPrefix(-1) == Ipv4Address(0));

    // Every kernel mask form: zero length, sa_len ending inside sin_addr,
    // sin_len 8 and a full sockaddr_in; the family byte does not matter.
    PHPVPN_CHECK(routing::netmaskFromSockaddr({}) == Ipv4Address(0));
    const Bytes zero = {0, 0, 0, 0};
    PHPVPN_CHECK(routing::netmaskFromSockaddr(zero) == Ipv4Address(0));
    const Bytes headerOnly = {4, AF_INET, 0, 0};
    PHPVPN_CHECK(routing::netmaskFromSockaddr(headerOnly) == Ipv4Address(0));
    const Bytes eight = trimmedMask({255});
    PHPVPN_CHECK(routing::netmaskFromSockaddr(eight) == ip("255.0.0.0"));
    const Bytes twentyFour = trimmedMask({255, 255, 255}, 0);
    PHPVPN_CHECK(routing::netmaskFromSockaddr(twentyFour) == ip("255.255.255.0"));
    const Bytes sinLen8 = {8, AF_INET, 0, 0, 255, 255, 255, 128};
    PHPVPN_CHECK(routing::netmaskFromSockaddr(sinLen8) == ip("255.255.255.128"));
    const sockaddr_in full = inetSockaddr("255.255.255.252");
    const auto* fullBytes = reinterpret_cast<const std::uint8_t*>(&full);
    PHPVPN_CHECK(routing::netmaskFromSockaddr(std::span<const std::uint8_t>(fullBytes, sizeof(full))) ==
                 ip("255.255.255.252"));
    // sa_len beyond the span is bounded by the span.
    const Bytes clipped = {16, AF_INET, 0, 0, 255, 255};
    PHPVPN_CHECK(routing::netmaskFromSockaddr(clipped) == ip("255.255.0.0"));

    // Mask to prefix: contiguous masks only.
    PHPVPN_CHECK_EQ(routing::prefixLengthOfNetmask(Ipv4Address(0)).valueOr(-1), 0);
    PHPVPN_CHECK_EQ(routing::prefixLengthOfNetmask(ip("255.0.0.0")).valueOr(-1), 8);
    PHPVPN_CHECK_EQ(routing::prefixLengthOfNetmask(ip("255.255.255.128")).valueOr(-1), 25);
    PHPVPN_CHECK_EQ(routing::prefixLengthOfNetmask(ip("255.255.255.255")).valueOr(-1), 32);
    PHPVPN_REQUIRE_ERROR(routing::prefixLengthOfNetmask(ip("255.0.255.0")), ErrorCode::netconfig_apply_failed);
    PHPVPN_REQUIRE_ERROR(routing::prefixLengthOfNetmask(ip("0.255.255.255")), ErrorCode::netconfig_apply_failed);
    PHPVPN_REQUIRE_ERROR(routing::prefixLengthOfNetmask(ip("255.255.255.1")), ErrorCode::netconfig_apply_failed);

    // A route with a non-contiguous mask is an error, RTF_HOST is /32.
    Bytes addresses;
    const sockaddr_in destination = inetSockaddr("10.0.0.0");
    appendPadded(addresses, &destination, sizeof(destination));
    const Bytes odd = trimmedMask({255, 0, 255});
    appendPadded(addresses, odd.data(), odd.size());
    auto oddRoute = routing::decode(kernelMessage(RTM_GET, 1, RTF_UP, RTA_DST | RTA_NETMASK, addresses));
    PHPVPN_REQUIRE_OK(oddRoute);
    PHPVPN_REQUIRE_ERROR(routing::routePrefixLength(oddRoute.value()), ErrorCode::netconfig_apply_failed);
    auto hostRoute = routing::decode(kernelMessage(RTM_GET, 1, RTF_UP | RTF_HOST, RTA_DST | RTA_NETMASK, addresses));
    PHPVPN_REQUIRE_OK(hostRoute);
    PHPVPN_CHECK_EQ(routing::routePrefixLength(hostRoute.value()).valueOr(-1), 32);
}

}  // namespace phpvpn
