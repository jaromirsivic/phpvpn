// Well known locations of the macOS installation, plan-macos-v001 section
// "Struktura" (the analogue of the Linux table in client.md "Cesty a opravneni").
//
// | Path                                 | Owner, mode     | Content                                     |
// |--------------------------------------|-----------------|---------------------------------------------|
// | /Library/Application Support/phpvpn/ | root:wheel 0700 | config.json 0600 and its publication files  |
// | /private/var/db/phpvpn/              | root:wheel 0700 | ownership.json (interface ownership record) |
// | /private/var/run/phpvpn/             | root:wheel 0755 | control-v1.sock 0666, service.lock 0600;    |
// |                                      |                 | created by the daemon at startup            |
// | /private/var/log/phpvpn/             | root:wheel 0755 | phpvpn-service.log, rotated 8 MiB x 5       |
//
// The paths are canonical on purpose. openTrustedDirectory opens every
// component with O_NOFOLLOW, and /var, /etc and /tmp are symbolic links into
// /private on macOS, so /var/db/phpvpn would be refused as an unsafe ancestor.
// /Library may carry the sticky bit and be writable by group admin; the shared
// ancestor rule accepts a sticky component, as it accepts /tmp on Linux.
// /Library/Application Support is root:admin 0755. /private/var/run is
// root:daemon 0775, which the macOS branch of the ancestor rule accepts because
// the groups wheel and daemon contain only root (paths_posix.cpp). The logs do
// not go to /Library/Logs: it is root:admin 0775 without the sticky bit and
// would be refused; /private/var/log is root:wheel 0755.
//
// The service uses these constants through paths_posix.h (kPosix*), never
// directly. Isolated end to end test instances replace whole directories
// through PosixDirectoryOverrides.
#pragma once

#include <sys/types.h>

namespace phpvpn {

inline constexpr const char* kMacosConfigDirectory = "/Library/Application Support/phpvpn";
inline constexpr const char* kMacosStateDirectory = "/private/var/db/phpvpn";
inline constexpr const char* kMacosRuntimeDirectory = "/private/var/run/phpvpn";
inline constexpr const char* kMacosLogDirectory = "/private/var/log/phpvpn";

inline constexpr mode_t kMacosConfigDirectoryMode = 0700;
inline constexpr mode_t kMacosStateDirectoryMode = 0700;
inline constexpr mode_t kMacosRuntimeDirectoryMode = 0755;
inline constexpr mode_t kMacosLogDirectoryMode = 0755;

}  // namespace phpvpn
