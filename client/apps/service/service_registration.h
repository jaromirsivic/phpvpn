// Constants that define the Windows Service registration contract.
#pragma once

namespace phpvpn {

// SCM dependency lists are double-NUL-terminated MULTI_SZ values. Wintun is
// loaded by the service from wintun.dll and is not itself a Windows Service.
inline constexpr wchar_t kServiceDependencies[] = L"Tcpip\0";

}  // namespace phpvpn
