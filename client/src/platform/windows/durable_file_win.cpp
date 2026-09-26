#include "platform/iface/durable_file.h"

#include <fstream>
#include <system_error>

namespace phpvpn {

// Same semantics as the ownership journal write of core/runtime/tunnel_session.cpp.
Status durableReplaceFile(const std::filesystem::path& path, std::string_view bytes) {
    std::error_code code;
    std::filesystem::create_directories(path.parent_path(), code);

    // The content is written to a temporary file and then renamed, so a crash
    // during the write cannot leave a half written record behind.
    std::filesystem::path temporary = path;
    temporary += ".tmp";
    {
        std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
        if (!stream) {
            return Error(ErrorCode::config_write_failed, "the ownership journal could not be written");
        }
        stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        stream.flush();
        if (!stream) {
            return Error(ErrorCode::config_write_failed, "the ownership journal could not be written");
        }
    }
    std::filesystem::rename(temporary, path, code);
    if (code) {
        std::filesystem::remove(temporary, code);
        return Error(ErrorCode::config_write_failed, "the ownership journal could not be replaced");
    }
    return Status::success();
}

}  // namespace phpvpn
