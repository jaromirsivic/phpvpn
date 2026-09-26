#pragma once
#include <span>
#include <string_view>

namespace phpvpn::tray {
// Element 0 is the program name, exactly as in argv. Only an exact match of a
// later argument requests the notice.
inline bool requestsInstallationNotice(int argc, const wchar_t* const* argv) {
    for (int i = 1; i < argc; ++i)
        if (std::wstring_view(argv[i]) == L"--installation-complete") return true;
    return false;
}
// UTF-8 argv of the Linux tray, same semantics as the wide variant.
inline bool requestsInstallationNotice(std::span<const std::string_view> arguments) {
    for (std::size_t i = 1; i < arguments.size(); ++i)
        if (arguments[i] == "--installation-complete") return true;
    return false;
}
inline bool requestsInstallationNotice(int argc, const char* const* argv) {
    for (int i = 1; i < argc; ++i)
        if (std::string_view(argv[i]) == "--installation-complete") return true;
    return false;
}
} // namespace phpvpn::tray
