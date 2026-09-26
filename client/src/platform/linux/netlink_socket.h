// Minimal blocking rtnetlink client shared by the TUN driver and NetConfig.
//
// plan-v001 section 4.4: a NETLINK_ROUTE socket with SOCK_CLOEXEC, sequence
// numbers, acknowledged requests, a bounded receive timeout and dumps that are
// repeated at most three times when the kernel flags them NLM_F_DUMP_INTR. The
// message format lives in netlink_codec.h.
#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>

#include "core/base/result.h"
#include "platform/linux/fd.h"
#include "platform/linux/netlink_codec.h"

namespace phpvpn::netlink {

struct Reply {
    int error = 0;  // positive errno reported by the kernel, 0 on success
    std::vector<Bytes> messages;
};

class Socket {
public:
    // `failure` is the error code used for transport failures, so the tunnel
    // and the network configuration report errors of their own domain.
    static Result<Socket> open(ErrorCode failure = ErrorCode::netconfig_apply_failed);

    Socket(Socket&&) noexcept = default;
    Socket& operator=(Socket&&) noexcept = default;

    // Sends one request and collects its answer. A kernel error is a value in
    // Reply::error; a socket failure, a timeout or malformed data is an Error.
    Result<Reply> request(const std::function<Bytes(std::uint32_t)>& build);

    // Runs a dump and repeats it while the kernel reports an interrupted,
    // inconsistent dump. A kernel error is returned as an Error.
    Result<std::vector<Bytes>> dump(const std::function<Bytes(std::uint32_t)>& build);

    // Link by index or by name; nullopt when the kernel answers ENODEV.
    Result<std::optional<LinkRecord>> linkByIndex(int index);
    Result<std::optional<LinkRecord>> linkByName(const std::string& name);

    ErrorCode failureCode() const noexcept { return failure_; }

private:
    Socket(UniqueFd fd, ErrorCode failure, std::uint32_t sequence) noexcept
        : fd_(std::move(fd)), failure_(failure), sequence_(sequence) {}

    Result<Reply> transact(const std::function<Bytes(std::uint32_t)>& build, bool dump,
                           bool& interrupted);
    Result<std::optional<LinkRecord>> singleLink(Result<Reply> reply);

    UniqueFd fd_;
    ErrorCode failure_ = ErrorCode::netconfig_apply_failed;
    std::uint32_t sequence_ = 0;
};

std::string errnoText(int error);

}  // namespace phpvpn::netlink
