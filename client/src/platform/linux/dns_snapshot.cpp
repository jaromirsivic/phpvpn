#include "platform/linux/dns_snapshot.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#include <systemd/sd-bus.h>

#include <algorithm>
#include <cerrno>
#include <memory>

#include "core/base/logger.h"
#include "platform/linux/fd.h"

namespace phpvpn {
namespace {

constexpr const char* kComponent = "dns-snapshot";
constexpr std::size_t kMaxResolvConfBytes = 64 * 1024;
// Bounded wait for resolved; the sd-bus default of 25 s would stall a connect.
constexpr std::uint64_t kBusTimeoutUsec = 2'000'000;

struct BusDeleter {
    void operator()(sd_bus* bus) const noexcept { sd_bus_flush_close_unref(bus); }
};
struct MessageDeleter {
    void operator()(sd_bus_message* message) const noexcept { sd_bus_message_unref(message); }
};
struct BusError {
    // SD_BUS_ERROR_NULL is a C compound literal, which Clang rejects in C++.
    sd_bus_error value{nullptr, nullptr, 0};
    ~BusError() { sd_bus_error_free(&value); }
};

bool isSpace(char character) noexcept {
    return character == ' ' || character == '\t' || character == '\r' || character == '\v' ||
           character == '\f';
}

}  // namespace

std::vector<std::string> parseResolvConfNameservers(std::string_view text) {
    std::vector<std::string> servers;
    std::size_t lineStart = 0;
    while (lineStart < text.size()) {
        std::size_t lineEnd = text.find('\n', lineStart);
        if (lineEnd == std::string_view::npos) lineEnd = text.size();
        std::string_view line = text.substr(lineStart, lineEnd - lineStart);
        lineStart = lineEnd + 1;

        std::size_t position = 0;
        auto nextToken = [&]() -> std::string_view {
            while (position < line.size() && isSpace(line[position])) ++position;
            const std::size_t begin = position;
            while (position < line.size() && !isSpace(line[position])) ++position;
            return line.substr(begin, position - begin);
        };
        const std::string_view keyword = nextToken();
        if (keyword != "nameserver") continue;  // also skips '#' and ';' comments
        const std::string address(nextToken());
        in_addr parsed{};
        if (address.empty() || inet_pton(AF_INET, address.c_str(), &parsed) != 1) continue;
        char canonical[INET_ADDRSTRLEN] = {};
        if (inet_ntop(AF_INET, &parsed, canonical, sizeof(canonical)) != nullptr) {
            servers.emplace_back(canonical);
        }
    }
    return servers;
}

Result<std::vector<std::string>> readResolvConfNameservers(const std::filesystem::path& path) {
    // O_NONBLOCK keeps a FIFO planted at the path from blocking the open; the
    // descriptor is rejected below unless it refers to a regular file.
    UniqueFd fd(::open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOCTTY | O_NONBLOCK));
    if (!fd) {
        if (errno == ENOENT) return std::vector<std::string>();
        return Error(ErrorCode::netconfig_apply_failed, "the resolver configuration could not be opened");
    }
    struct stat status{};
    if (::fstat(fd.get(), &status) != 0 || !S_ISREG(status.st_mode)) {
        return Error(ErrorCode::netconfig_apply_failed, "the resolver configuration is not a regular file");
    }
    std::string content;
    char chunk[4096];
    for (;;) {
        const ssize_t count = ::read(fd.get(), chunk, sizeof(chunk));
        if (count < 0) {
            if (errno == EINTR) continue;
            return Error(ErrorCode::netconfig_apply_failed, "the resolver configuration could not be read");
        }
        if (count == 0) break;
        content.append(chunk, static_cast<std::size_t>(count));
        if (content.size() > kMaxResolvConfBytes) {
            return Error(ErrorCode::netconfig_apply_failed, "the resolver configuration is too large");
        }
    }
    return parseResolvConfNameservers(content);
}

std::vector<std::string> resolvedDnsServers() {
    std::vector<std::string> servers;
    sd_bus* rawBus = nullptr;
    if (sd_bus_open_system(&rawBus) < 0 || rawBus == nullptr) {
        PHPVPN_LOG_DEBUG(kComponent, "the system bus is unavailable, resolved is not consulted");
        return servers;
    }
    std::unique_ptr<sd_bus, BusDeleter> bus(rawBus);
    sd_bus_set_method_call_timeout(bus.get(), kBusTimeoutUsec);

    // Asking the bus first avoids activating resolved when it is not running.
    {
        BusError error;
        sd_bus_message* rawReply = nullptr;
        if (sd_bus_call_method(bus.get(), "org.freedesktop.DBus", "/org/freedesktop/DBus",
                               "org.freedesktop.DBus", "NameHasOwner", &error.value, &rawReply,
                               "s", "org.freedesktop.resolve1") < 0) {
            return servers;
        }
        std::unique_ptr<sd_bus_message, MessageDeleter> reply(rawReply);
        int hasOwner = 0;
        if (sd_bus_message_read(reply.get(), "b", &hasOwner) < 0 || hasOwner == 0) {
            return servers;
        }
    }

    BusError error;
    sd_bus_message* rawReply = nullptr;
    if (sd_bus_get_property(bus.get(), "org.freedesktop.resolve1", "/org/freedesktop/resolve1",
                            "org.freedesktop.resolve1.Manager", "DNS", &error.value, &rawReply,
                            "a(iiay)") < 0) {
        PHPVPN_LOG_DEBUG(kComponent, "the resolved DNS property could not be read");
        return servers;
    }
    std::unique_ptr<sd_bus_message, MessageDeleter> reply(rawReply);
    if (sd_bus_message_enter_container(reply.get(), 'a', "(iiay)") < 0) return {};
    for (;;) {
        const int entered = sd_bus_message_enter_container(reply.get(), 'r', "iiay");
        if (entered < 0) return {};
        if (entered == 0) break;
        std::int32_t ifindex = 0;
        std::int32_t family = 0;
        const void* data = nullptr;
        std::size_t size = 0;
        if (sd_bus_message_read(reply.get(), "ii", &ifindex, &family) < 0 ||
            sd_bus_message_read_array(reply.get(), 'y', &data, &size) < 0 ||
            sd_bus_message_exit_container(reply.get()) < 0) {
            return {};
        }
        (void)ifindex;
        if (family != AF_INET || size != 4 || data == nullptr) continue;
        char text[INET_ADDRSTRLEN] = {};
        if (inet_ntop(AF_INET, data, text, sizeof(text)) != nullptr) servers.emplace_back(text);
    }
    return servers;
}

Result<std::vector<std::string>> collectDnsServers(const DnsSnapshotOptions& options) {
    std::vector<std::string> servers;
    PHPVPN_TRY_ASSIGN(servers, readResolvConfNameservers(options.resolvConfPath));
    if (options.queryResolved) {
        auto resolved = resolvedDnsServers();
        servers.insert(servers.end(), resolved.begin(), resolved.end());
    }
    std::sort(servers.begin(), servers.end());
    servers.erase(std::unique(servers.begin(), servers.end()), servers.end());
    return servers;
}

}  // namespace phpvpn
