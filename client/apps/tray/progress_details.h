#pragma once
#include <string>

namespace phpvpn::tray {
struct ProgressDetails {
    std::string serverUrl, virtualIp;
    bool available = false;
    bool operator==(const ProgressDetails &) const = default;
};
// Consumes and wipes the received document on every path. Returns only two
// validated display values. No HWND, logging, filesystem or status dependency.
ProgressDetails consumeProgressConfig(std::string document);
} // namespace phpvpn::tray
