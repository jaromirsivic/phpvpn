// Crash safe whole file replacement for small state files.
//
// Used for the ownership journal of the tunnel session. A reader sees either
// the previous content or the complete new content, never a partial write. How
// far the replacement survives a power loss is up to the platform: Linux syncs
// the file and its directory, Windows keeps the std::filesystem rename.
#pragma once

#include <filesystem>
#include <string_view>

#include "core/base/result.h"

namespace phpvpn {

// Creates the parent directory when missing, writes `bytes` to a temporary file
// next to `path` and renames it over `path`. Fails with config_write_failed and
// leaves no temporary file behind.
Status durableReplaceFile(const std::filesystem::path& path, std::string_view bytes);

}  // namespace phpvpn
