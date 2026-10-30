#include "platform/macos/route_codec.h"

// <sys/types.h> and <sys/socket.h> first: the BSD network headers below use
// their types without including them on every SDK.
#include <sys/types.h>
#include <sys/socket.h>

#include <net/if_dl.h>
#include <net/route.h>
#include <netinet/in.h>

#include <algorithm>
#include <bit>
#include <cstring>
#include <string>
#include <utility>

namespace phpvpn::routing {
namespace {

#ifdef RTF_LLINFO
static_assert(kFlagLlinfo == RTF_LLINFO, "RTF_LLINFO differs from the xnu ABI");
#endif
#ifdef RTF_WASCLONED
static_assert(kFlagWasCloned == RTF_WASCLONED, "RTF_WASCLONED differs from the xnu ABI");
#endif
#ifdef RTF_LOCAL
static_assert(kFlagLocal == RTF_LOCAL, "RTF_LOCAL differs from the xnu ABI");
#endif
#ifdef RTF_BROADCAST
static_assert(kFlagBroadcast == RTF_BROADCAST, "RTF_BROADCAST differs from the xnu ABI");
#endif
#ifdef RTF_MULTICAST
static_assert(kFlagMulticast == RTF_MULTICAST, "RTF_MULTICAST differs from the xnu ABI");
#endif
#ifdef RTF_IFSCOPE
static_assert(kFlagIfScope == RTF_IFSCOPE, "RTF_IFSCOPE differs from the xnu ABI");
#endif

// The addresses start four byte aligned right after the header.
static_assert(sizeof(rt_msghdr) % sizeof(std::uint32_t) == 0);

// Offset of sin_addr; the shortest AF_INET address that carries one ends right
// after it (the kernel itself requires at least that much on input).
constexpr std::size_t kInetAddressOffset = offsetof(sockaddr_in, sin_addr);
constexpr std::size_t kInetMinimum = kInetAddressOffset + sizeof(in_addr);
// The fixed part of a sockaddr_dl before sdl_data, which holds sdl_index.
constexpr std::size_t kLinkMinimum = offsetof(sockaddr_dl, sdl_data);
// rtm_msglen, rtm_version and rtm_type, common to every routing message.
constexpr std::size_t kCommonHeader = offsetof(rt_msghdr, rtm_type) + sizeof(u_char);

// ROUNDUP of xnu rtsock.c: a multiple of sizeof(uint32_t), and four bytes for
// a zero length.
constexpr std::size_t roundUp(std::size_t length) noexcept {
    return length == 0 ? sizeof(std::uint32_t)
                       : (length + sizeof(std::uint32_t) - 1) & ~(sizeof(std::uint32_t) - 1);
}

Error malformed(const char* what) {
    return Error(ErrorCode::netconfig_apply_failed, std::string("malformed routing ") + what);
}

template <class T>
T readRaw(std::span<const std::uint8_t> bytes, std::size_t offset = 0) noexcept {
    T value{};
    std::memcpy(&value, bytes.data() + offset, sizeof(T));
    return value;
}

void appendPadded(Bytes& bytes, const void* data, std::size_t size) {
    const auto* begin = static_cast<const std::uint8_t*>(data);
    bytes.insert(bytes.end(), begin, begin + size);
    bytes.resize(bytes.size() + (roundUp(size) - size), 0);
}

sockaddr_in inetSockaddr(Ipv4Address address) noexcept {
    sockaddr_in result{};
    result.sin_len = static_cast<std::uint8_t>(sizeof(sockaddr_in));
    result.sin_family = AF_INET;
    std::uint8_t bytes[4] = {};
    address.toNetworkBytes(bytes);
    std::memcpy(&result.sin_addr, bytes, sizeof(bytes));
    return result;
}

// An address-less sockaddr_dl: the kernel resolves the interface by sdl_index.
sockaddr_dl linkSockaddr(int index) noexcept {
    sockaddr_dl result{};
    result.sdl_len = static_cast<std::uint8_t>(sizeof(sockaddr_dl));
    result.sdl_family = AF_LINK;
    result.sdl_index = static_cast<u_short>(index);
    return result;
}

// Callers checked that the address reaches at least kInetMinimum bytes.
Ipv4Address inetAddressAt(std::span<const std::uint8_t> address) noexcept {
    return Ipv4Address::fromNetworkBytes(address.data() + kInetAddressOffset);
}

// Callers checked that the address reaches at least kLinkMinimum bytes.
int linkIndexAt(std::span<const std::uint8_t> address) noexcept {
    return readRaw<u_short>(address, offsetof(sockaddr_dl, sdl_index));
}

}  // namespace

Ipv4Address netmaskForPrefix(int prefixLength) noexcept {
    const int length = std::clamp(prefixLength, 0, 32);
    return Ipv4Address(length == 0 ? 0u : 0xFFFFFFFFu << (32 - length));
}

Result<int> prefixLengthOfNetmask(Ipv4Address netmask) {
    // A contiguous mask has its host bits as one run of trailing ones, so
    // adding one to them carries through the whole run.
    const std::uint32_t hostBits = ~netmask.value();
    if ((hostBits & (hostBits + 1u)) != 0) {
        return Error(ErrorCode::netconfig_apply_failed,
                     "the netmask " + netmask.toString() + " is not contiguous");
    }
    return std::popcount(netmask.value());
}

Ipv4Address netmaskFromSockaddr(std::span<const std::uint8_t> address) noexcept {
    if (address.empty()) return Ipv4Address(0);
    const std::size_t length = std::min<std::size_t>(address[0], address.size());
    std::uint8_t bytes[4] = {};
    for (std::size_t i = 0; i < sizeof(bytes) && kInetAddressOffset + i < length; ++i) {
        bytes[i] = address[kInetAddressOffset + i];
    }
    return Ipv4Address::fromNetworkBytes(bytes);
}

Result<int> routePrefixLength(const RouteMessage& message) {
    if ((message.flags & RTF_HOST) != 0 || !message.netmask.has_value()) return 32;
    return prefixLengthOfNetmask(*message.netmask);
}

int routeInterfaceIndex(const RouteMessage& message) noexcept {
    if (message.index > 0) return message.index;
    return message.interfaceIndex.has_value() && *message.interfaceIndex > 0 ? *message.interfaceIndex : 0;
}

Bytes encode(const RouteRequest& request) {
    // Socket addresses in RTA_* bit order: DST, GATEWAY, NETMASK, IFP.
    Bytes addresses;
    int addrs = RTA_DST;
    const sockaddr_in destination = inetSockaddr(request.destination);
    appendPadded(addresses, &destination, sizeof(destination));
    if (request.gateway.has_value()) {
        const sockaddr_in gateway = inetSockaddr(*request.gateway);
        appendPadded(addresses, &gateway, sizeof(gateway));
        addrs |= RTA_GATEWAY;
    } else if (request.gatewayInterfaceIndex > 0) {
        const sockaddr_dl gateway = linkSockaddr(request.gatewayInterfaceIndex);
        appendPadded(addresses, &gateway, sizeof(gateway));
        addrs |= RTA_GATEWAY;
    }
    if (request.netmask.has_value()) {
        // A full sockaddr_in even for 0.0.0.0: a zero sa_len would end the
        // kernel's address parsing (rt_xaddrs) at this slot.
        const sockaddr_in netmask = inetSockaddr(*request.netmask);
        appendPadded(addresses, &netmask, sizeof(netmask));
        addrs |= RTA_NETMASK;
    }
    if (request.interfaceIndex.has_value()) {
        const sockaddr_dl ifp = linkSockaddr(std::max(*request.interfaceIndex, 0));
        appendPadded(addresses, &ifp, sizeof(ifp));
        addrs |= RTA_IFP;
    }

    rt_msghdr header{};
    header.rtm_msglen = static_cast<u_short>(sizeof(header) + addresses.size());
    header.rtm_version = RTM_VERSION;
    header.rtm_type = static_cast<u_char>(request.type);
    header.rtm_flags = request.flags;
    header.rtm_addrs = addrs;
    header.rtm_pid = request.pid;
    header.rtm_seq = request.sequence;

    Bytes message(sizeof(header));
    std::memcpy(message.data(), &header, sizeof(header));
    message.insert(message.end(), addresses.begin(), addresses.end());
    return message;
}

bool isReplyTo(std::span<const std::uint8_t> message, int type, int sequence, int pid) noexcept {
    if (message.size() < sizeof(rt_msghdr)) return false;
    const auto header = readRaw<rt_msghdr>(message);
    return header.rtm_version == RTM_VERSION && header.rtm_type == type &&
           header.rtm_seq == sequence && header.rtm_pid == pid;
}

Result<RouteMessage> decode(std::span<const std::uint8_t> bytes) {
    if (bytes.size() < sizeof(rt_msghdr)) return malformed("message header");
    const auto header = readRaw<rt_msghdr>(bytes);
    const std::size_t length = header.rtm_msglen;
    if (length < sizeof(rt_msghdr) || length > bytes.size()) return malformed("message length");
    if (header.rtm_version != RTM_VERSION) return malformed("message version");
    // RTM_ADD to RTM_RESOLVE carry an rt_msghdr; interface and address
    // messages (RTM_NEWADDR, RTM_IFINFO, ...) have other headers.
    if (header.rtm_type < RTM_ADD || header.rtm_type > RTM_RESOLVE) return malformed("message type");

    RouteMessage message;
    message.length = header.rtm_msglen;
    message.version = header.rtm_version;
    message.type = header.rtm_type;
    message.index = header.rtm_index;
    message.flags = header.rtm_flags;
    message.addrs = header.rtm_addrs;
    message.pid = header.rtm_pid;
    message.sequence = header.rtm_seq;
    message.errnoValue = header.rtm_errno;

    std::size_t offset = sizeof(rt_msghdr);
    for (int slot = 0; slot < RTAX_MAX; ++slot) {
        if ((header.rtm_addrs & (1 << slot)) == 0) continue;
        if (offset >= length) return malformed("address list");
        const std::size_t addressLength = bytes[offset];
        if (addressLength > length - offset) return malformed("address length");
        const auto address = bytes.subspan(offset, addressLength);
        // The kernel pads every address; a missing pad after the last address
        // is tolerated because nothing follows it.
        offset += std::min(roundUp(addressLength), length - offset);
        const int family = addressLength >= 2 ? address[1] : AF_UNSPEC;

        switch (slot) {
            case RTAX_DST:
                message.destinationFamily = family;
                if (family == AF_INET) {
                    if (addressLength < kInetMinimum) return malformed("destination address");
                    message.destination = inetAddressAt(address);
                }
                break;
            case RTAX_GATEWAY:
                if (family == AF_INET) {
                    if (addressLength < kInetMinimum) return malformed("gateway address");
                    message.gateway = inetAddressAt(address);
                } else if (family == AF_LINK) {
                    if (addressLength < kLinkMinimum) return malformed("gateway link address");
                    message.gatewayLinkIndex = linkIndexAt(address);
                }
                break;
            case RTAX_NETMASK:
                // The family byte of a trimmed mask is meaningless.
                message.netmask = netmaskFromSockaddr(address);
                break;
            case RTAX_IFP:
                if (family == AF_LINK && addressLength >= kLinkMinimum) {
                    message.interfaceIndex = linkIndexAt(address);
                }
                break;
            case RTAX_IFA:
                if (family == AF_INET && addressLength >= kInetMinimum) {
                    message.interfaceAddress = inetAddressAt(address);
                }
                break;
            default:
                break;  // RTA_GENMASK, RTA_AUTHOR and RTA_BRD are not used
        }
    }
    return message;
}

Result<std::vector<RouteMessage>> decodeDump(std::span<const std::uint8_t> buffer) {
    std::vector<RouteMessage> messages;
    std::size_t offset = 0;
    while (offset < buffer.size()) {
        const auto rest = buffer.subspan(offset);
        if (rest.size() < kCommonHeader) return malformed("dump record");
        const std::size_t length = readRaw<u_short>(rest, offsetof(rt_msghdr, rtm_msglen));
        if (length < kCommonHeader || length > rest.size()) return malformed("dump record length");
        const auto record = rest.first(length);
        offset += length;
        if (record[offsetof(rt_msghdr, rtm_version)] != RTM_VERSION ||
            record[offsetof(rt_msghdr, rtm_type)] != RTM_GET) {
            continue;
        }
        RouteMessage message;
        PHPVPN_TRY_ASSIGN(message, decode(record));
        messages.push_back(std::move(message));
    }
    return messages;
}

}  // namespace phpvpn::routing
