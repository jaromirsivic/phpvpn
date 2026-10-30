// Minimal blocking PF_ROUTE client of the macOS network configuration
// (plan-macos-v001 step 5).
//
// One raw routing socket per operation, like the rtnetlink client on Linux. A
// request carries its own sequence number and the process id; its answer is
// the first message of the same type, sequence and pid, read within 2 s.
// Everything else the kernel broadcasts on a routing socket (changes made by
// other processes, interface events, answers of other sockets) is ignored.
// The kernel reports a refused request twice, as the errno of write() and as
// rtm_errno of the echoed message; both end in RouteMessage::errnoValue. The
// routing table itself is read through sysctl(NET_RT_DUMP), not the socket.
// The message format lives in route_codec.h.
#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "core/base/result.h"
#include "platform/macos/route_codec.h"
#include "platform/posix/fd.h"

namespace phpvpn::routing {

class RouteSocket {
public:
    // socket(PF_ROUTE, SOCK_RAW, AF_INET) with FD_CLOEXEC. Opening and RTM_GET
    // need no privileges; RTM_ADD and RTM_DELETE need root.
    static Result<RouteSocket> open();

    RouteSocket(RouteSocket&&) noexcept = default;
    RouteSocket& operator=(RouteSocket&&) noexcept = default;

    // Sends one RTM_ADD, RTM_DELETE or RTM_GET; the sequence number and the
    // process id of `query` are filled in here. A kernel verdict (EEXIST,
    // ESRCH, ENETUNREACH, EPERM, ...) is the value RouteMessage::errnoValue,
    // so the caller decides which ones are success. A socket failure, a
    // timeout or a malformed answer is an Error.
    Result<RouteMessage> request(RouteRequest query);

    // The RTM_GET records of the AF_INET routing table, from
    // sysctl({CTL_NET, PF_ROUTE, 0, AF_INET, NET_RT_DUMP, 0}). The size query
    // and the read are repeated, at most three times, while the table outgrows
    // the buffer in between (ENOMEM).
    static Result<std::vector<RouteMessage>> dump();

private:
    RouteSocket(UniqueFd fd, std::uint32_t sequence, int pid) noexcept
        : fd_(std::move(fd)), sequence_(sequence), pid_(pid) {}

    UniqueFd fd_;
    std::uint32_t sequence_ = 0;
    int pid_ = 0;
};

// Only the number, like the Linux and Windows code, so no localized system
// text reaches logs or IPC error envelopes.
std::string errnoText(int error);

}  // namespace phpvpn::routing
