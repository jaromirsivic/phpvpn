#include "platform/windows/paths_win.h"

#include <windows.h>

#include <aclapi.h>
#include <sddl.h>
#include <shlobj.h>

#include <algorithm>
#include <atomic>
#include <limits>
#include <system_error>
#include <vector>

#include "core/config/config.h"

#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "shell32.lib")

namespace phpvpn {
namespace {

constexpr const wchar_t* kProductDirectoryName = L"PHPVPN";
std::atomic<unsigned long> g_defaultConfigTemporarySequence{0};

bool pathAlreadyExistsError(DWORD error) {
    return error == ERROR_FILE_EXISTS || error == ERROR_ALREADY_EXISTS;
}

Status writeNewFile(const std::filesystem::path& path, std::string_view content) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                              FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return Error(ErrorCode::config_write_failed,
                     "the default configuration temporary file could not be created");
    }

    bool complete = true;
    std::size_t offset = 0;
    while (offset < content.size()) {
        const std::size_t remaining = content.size() - offset;
        const DWORD requested = static_cast<DWORD>(
            std::min<std::size_t>(remaining, std::numeric_limits<DWORD>::max()));
        DWORD written = 0;
        if (!WriteFile(file, content.data() + offset, requested, &written, nullptr) ||
            written != requested) {
            complete = false;
            break;
        }
        offset += written;
    }
    if (complete && !FlushFileBuffers(file)) {
        complete = false;
    }
    CloseHandle(file);

    if (!complete) {
        DeleteFileW(path.c_str());
        return Error(ErrorCode::config_write_failed,
                     "the default configuration could not be written");
    }
    return Status::success();
}

}  // namespace

Result<std::filesystem::path> executableDirectory() {
    std::vector<wchar_t> buffer(MAX_PATH);
    for (;;) {
        const DWORD written =
            GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (written == 0) {
            return Error(ErrorCode::internal, "the executable path could not be determined");
        }
        if (written < buffer.size()) {
            break;
        }
        if (buffer.size() >= 32768) {
            return Error(ErrorCode::internal, "the executable path is unexpectedly long");
        }
        buffer.resize(buffer.size() * 2);
    }

    std::filesystem::path path(buffer.data());
    return path.parent_path();
}

Result<std::filesystem::path> dataDirectory() {
    PWSTR programData = nullptr;
    const HRESULT result =
        SHGetKnownFolderPath(FOLDERID_ProgramData, KF_FLAG_CREATE, nullptr, &programData);
    if (FAILED(result) || programData == nullptr) {
        if (programData != nullptr) {
            CoTaskMemFree(programData);
        }
        return Error(ErrorCode::internal, "the shared data directory could not be resolved");
    }

    std::filesystem::path path(programData);
    CoTaskMemFree(programData);
    path /= kProductDirectoryName;

    std::error_code code;
    std::filesystem::create_directories(path, code);
    if (code && !std::filesystem::exists(path)) {
        return Error(ErrorCode::config_write_failed, "the data directory could not be created");
    }
    return path;
}

std::filesystem::path configFilePath(const std::filesystem::path& dataDirectory) {
    return dataDirectory / "config.json";
}

std::filesystem::path journalFilePath(const std::filesystem::path& dataDirectory) {
    return dataDirectory / "ownership.json";
}

std::filesystem::path logDirectory(const std::filesystem::path& dataDirectory) {
    return dataDirectory / "logs";
}

Status ensureDefaultConfigFile(const std::filesystem::path& path) {
    const DWORD attributes = GetFileAttributesW(path.c_str());
    if (attributes != INVALID_FILE_ATTRIBUTES) {
        return Status::success();
    }
    const DWORD lookupError = GetLastError();
    if (lookupError != ERROR_FILE_NOT_FOUND && lookupError != ERROR_PATH_NOT_FOUND) {
        return Error(ErrorCode::config_write_failed,
                     "the configuration path could not be inspected");
    }

    std::error_code directoryError;
    std::filesystem::create_directories(path.parent_path(), directoryError);
    if (directoryError && !std::filesystem::exists(path.parent_path())) {
        return Error(ErrorCode::config_write_failed,
                     "the configuration directory could not be created");
    }

    std::filesystem::path temporary;
    Status written = Error(ErrorCode::config_write_failed);
    for (int attempt = 0; attempt < 16; ++attempt) {
        temporary = path;
        temporary += L".default." + std::to_wstring(GetCurrentProcessId()) + L"." +
                     std::to_wstring(g_defaultConfigTemporarySequence.fetch_add(1)) + L".tmp";
        written = writeNewFile(temporary, kDefaultConfigDocument);
        if (written.ok()) {
            break;
        }
        std::error_code collisionError;
        if (!std::filesystem::exists(temporary, collisionError)) {
            return written;
        }
    }
    if (!written.ok()) {
        return written;
    }

    if (MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_WRITE_THROUGH)) {
        return Status::success();
    }

    const DWORD moveError = GetLastError();
    DeleteFileW(temporary.c_str());
    if (pathAlreadyExistsError(moveError)) {
        return Status::success();
    }
    return Error(ErrorCode::config_write_failed,
                 "the default configuration could not be published");
}

Status restrictDirectoryToAdministrators(const std::filesystem::path& directory) {
    // Protected DACL that grants full control to SYSTEM and to the local
    // Administrators group and nothing to anybody else, so the stored keys stay
    // unreadable for a standard user.
    static constexpr const wchar_t* kDescriptor = L"D:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)";

    PSECURITY_DESCRIPTOR descriptor = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(kDescriptor, SDDL_REVISION_1,
                                                              &descriptor, nullptr)) {
        return Error(ErrorCode::internal, "the access control list could not be built");
    }

    BOOL present = FALSE;
    BOOL defaulted = FALSE;
    PACL acl = nullptr;
    if (!GetSecurityDescriptorDacl(descriptor, &present, &acl, &defaulted)) {
        LocalFree(descriptor);
        return Error(ErrorCode::internal, "the access control list could not be read");
    }

    std::wstring path = directory.wstring();
    const DWORD status = SetNamedSecurityInfoW(path.data(), SE_FILE_OBJECT,
                                               DACL_SECURITY_INFORMATION |
                                                   PROTECTED_DACL_SECURITY_INFORMATION,
                                               nullptr, nullptr, acl, nullptr);
    LocalFree(descriptor);

    if (status != ERROR_SUCCESS) {
        return Error(ErrorCode::internal, "the directory permissions could not be applied");
    }
    return Status::success();
}

}  // namespace phpvpn
