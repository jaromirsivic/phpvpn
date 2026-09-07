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

Result<std::filesystem::path> dataDirectory(bool create) {
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
    if (!create) return path;

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

Status restrictDirectoryToAdministrators(const std::filesystem::path& directory) {
    // Protected DACL that grants full control to SYSTEM and to the local
    // Administrators group and nothing to anybody else, so the stored keys stay
    // unreadable for a standard user.
    static constexpr const wchar_t* kDescriptor = L"O:BAG:BAD:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)";

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

    PSID owner = nullptr;
    if (!GetSecurityDescriptorOwner(descriptor, &owner, &defaulted)) {
        LocalFree(descriptor);
        return Error(ErrorCode::internal, "the directory owner could not be read");
    }
    HANDLE handle = CreateFileW(directory.c_str(), READ_CONTROL | WRITE_DAC | WRITE_OWNER,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        LocalFree(descriptor);
        return Error(ErrorCode::config_unreadable, "the data directory cannot be secured");
    }
    FILE_ATTRIBUTE_TAG_INFO attributes{};
    if (!GetFileInformationByHandleEx(handle, FileAttributeTagInfo, &attributes, sizeof(attributes)) ||
        (attributes.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
        CloseHandle(handle);
        LocalFree(descriptor);
        return Error(ErrorCode::config_unreadable, "the data directory is not a regular directory");
    }
    const DWORD status = SetSecurityInfo(handle, SE_FILE_OBJECT,
        OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION,
        owner, nullptr, acl, nullptr);
    CloseHandle(handle);
    LocalFree(descriptor);

    if (status != ERROR_SUCCESS) {
        return Error(ErrorCode::internal, "the directory permissions could not be applied");
    }
    return Status::success();
}

}  // namespace phpvpn
