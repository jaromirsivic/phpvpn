#pragma once
#include <string_view>

namespace phpvpn::tray {
inline bool requestsInstallationNotice(int argc, const wchar_t* const* argv) {
    for (int i = 1; i < argc; ++i)
        if (std::wstring_view(argv[i]) == L"--installation-complete") return true;
    return false;
}
} // namespace phpvpn::tray
