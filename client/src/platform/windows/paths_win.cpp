#include "platform/windows/paths_win.h"

#include <windows.h>

#include <aclapi.h>
#include <sddl.h>
#include <shlobj.h>

#include <system_error>
#include <vector>

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
