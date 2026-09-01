// Server URL parsing, plan_client.md decision 6.
//
// `serverUrl` must be an absolute http or https URL without userinfo, without a
// query and without a fragment, with a canonical host and a valid port. The
// query `?action=<action>` is appended by the transport.
#include <algorithm>
#include <cctype>

#include "core/config/config.h"

namespace phpvpn {
namespace {

bool isHostCharacter(char c) noexcept {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
           c == '-' || c == '.';
}

// Accepts a registered name or a literal IPv4 address. A bracketed IPv6 literal
// is rejected because WinHTTP would need a different code path and v1 has no
// requirement for it.
bool isValidHost(std::string_view host) noexcept {
    if (host.empty() || host.size() > 253) {
        return false;
    }
    if (host.front() == '.' || host.back() == '.' || host.front() == '-') {
        return false;
    }
    for (char c : host) {
        if (!isHostCharacter(c)) {
            return false;
        }
    }
    return host.find("..") == std::string_view::npos;
}

std::string toLowerAscii(std::string_view text) {
    std::string out(text);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return out;
}

}  // namespace

Result<ServerUrl> parseServerUrl(std::string_view text) {
    if (text.empty()) {
        return Error(ErrorCode::config_invalid_url, "serverUrl is empty");
    }
    for (char c : text) {
        const auto byte = static_cast<unsigned char>(c);
        if (byte <= 0x20 || byte >= 0x7F) {
            return Error(ErrorCode::config_invalid_url, "serverUrl contains an invalid byte");
        }
    }

    ServerUrl url;
    url.original.assign(text);

    std::string_view rest;
    const std::string lowered = toLowerAscii(text);
    if (lowered.rfind("https://", 0) == 0) {
        url.secure = true;
        url.port = 443;
        rest = text.substr(8);
    } else if (lowered.rfind("http://", 0) == 0) {
        url.secure = false;
        url.port = 80;
        rest = text.substr(7);
    } else {
        return Error(ErrorCode::config_invalid_url, "serverUrl must start with http:// or https://");
    }

    if (rest.find('#') != std::string_view::npos) {
        return Error(ErrorCode::config_invalid_url, "serverUrl must not contain a fragment");
    }
    if (rest.find('?') != std::string_view::npos) {
        return Error(ErrorCode::config_invalid_url, "serverUrl must not contain a query");
    }

    const std::size_t authorityEnd = rest.find('/');
    const std::string_view authority =
        authorityEnd == std::string_view::npos ? rest : rest.substr(0, authorityEnd);
    if (authority.find('@') != std::string_view::npos) {
        return Error(ErrorCode::config_invalid_url, "serverUrl must not contain userinfo");
    }

    std::string_view host = authority;
    const std::size_t colon = authority.rfind(':');
    if (colon != std::string_view::npos) {
        host = authority.substr(0, colon);
        const std::string_view portText = authority.substr(colon + 1);
        if (portText.empty() || portText.size() > 5) {
            return Error(ErrorCode::config_invalid_url, "serverUrl has an invalid port");
        }
        std::uint32_t port = 0;
        for (char c : portText) {
            if (c < '0' || c > '9') {
                return Error(ErrorCode::config_invalid_url, "serverUrl has a non numeric port");
            }
            port = port * 10 + static_cast<std::uint32_t>(c - '0');
        }
        if (port == 0 || port > 65535) {
            return Error(ErrorCode::config_invalid_url, "serverUrl port is out of range");
        }
        url.port = static_cast<std::uint16_t>(port);
    }

    if (!isValidHost(host)) {
        return Error(ErrorCode::config_invalid_url, "serverUrl has an invalid host");
    }
    url.host = toLowerAscii(host);

    if (authorityEnd == std::string_view::npos) {
        url.path = "/";
    } else {
        url.path.assign(rest.substr(authorityEnd));
        if (url.path.empty()) {
            url.path = "/";
        }
    }

    return url;
}

}  // namespace phpvpn
