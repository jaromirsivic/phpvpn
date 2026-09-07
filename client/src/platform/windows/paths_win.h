// Well known locations of the Windows installation.
//
// The configuration, the logs and the ownership journal live under ProgramData
// so that the service can write them while running as LocalSystem, and the
// Wintun library is loaded from the installation directory only.
#pragma once

#include <filesystem>
#include <string>

#include "core/base/result.h"

namespace phpvpn {

// Directory of the running executable.
Result<std::filesystem::path> executableDirectory();

// %ProgramData%\PHPVPN, created when missing.
Result<std::filesystem::path> dataDirectory(bool create = true);

std::filesystem::path configFilePath(const std::filesystem::path& dataDirectory);
std::filesystem::path journalFilePath(const std::filesystem::path& dataDirectory);
std::filesystem::path logDirectory(const std::filesystem::path& dataDirectory);

// Restricts a directory to SYSTEM and the local Administrators group, so a
// standard user cannot read the stored keys.
Status restrictDirectoryToAdministrators(const std::filesystem::path& directory);

}  // namespace phpvpn
