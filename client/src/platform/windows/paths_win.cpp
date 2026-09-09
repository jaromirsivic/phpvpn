#include "platform/windows/paths_win.h"

#include <windows.h>

#include <aclapi.h>
#include <sddl.h>
#include <shlobj.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <atomic>
#include <limits>
#include <map>
#include <utility>
#include <functional>
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

namespace {
struct SecurityHandle {
    HANDLE value = INVALID_HANDLE_VALUE;
    explicit SecurityHandle(HANDLE handle) : value(handle) {}
    ~SecurityHandle() { if (value != INVALID_HANDLE_VALUE) CloseHandle(value); }
    SecurityHandle(const SecurityHandle&) = delete;
    SecurityHandle& operator=(const SecurityHandle&) = delete;
    SecurityHandle(SecurityHandle&& other) noexcept : value(std::exchange(other.value, INVALID_HANDLE_VALUE)) {}
};
struct DataEntry {
    std::filesystem::path path;
    SecurityHandle handle;
    BY_HANDLE_FILE_INFORMATION information;
};
struct DataDescriptor {
    PSECURITY_DESCRIPTOR value = nullptr;
    ~DataDescriptor() { if (value) LocalFree(value); }
    bool create(bool inherit) {
        return ConvertStringSecurityDescriptorToSecurityDescriptorW(inherit ?
            L"O:BAG:BAD:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)(A;OICI;FRFX;;;BU)" :
            L"O:BAG:BAD:P(A;;FA;;;SY)(A;;FA;;;BA)(A;;FRFX;;;BU)",
            SDDL_REVISION_1, &value, nullptr) != FALSE;
    }
    bool matches(HANDLE handle, bool& same) const {
        DataDescriptor actual;
        PSID actualOwner = nullptr, expectedOwner = nullptr;
        PACL actualAcl = nullptr, expectedAcl = nullptr;
        BOOL present = FALSE, defaulted = FALSE;
        SECURITY_DESCRIPTOR_CONTROL control = 0;
        DWORD revision = 0;
        if (GetSecurityInfo(handle, SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION,
                &actualOwner, nullptr, &actualAcl, nullptr, &actual.value) != ERROR_SUCCESS ||
            !GetSecurityDescriptorOwner(value, &expectedOwner, &defaulted) ||
            !GetSecurityDescriptorDacl(value, &present, &expectedAcl, &defaulted) ||
            !GetSecurityDescriptorControl(actual.value, &control, &revision)) return false;
        same = actualOwner && EqualSid(actualOwner, expectedOwner) && actualAcl &&
            (control & SE_DACL_PROTECTED) && actualAcl->AclSize == expectedAcl->AclSize &&
            std::memcmp(actualAcl, expectedAcl, actualAcl->AclSize) == 0;
        return true;
    }
    bool apply(const std::filesystem::path& path, HANDLE handle) const {
        bool same = false;
        // Read failure is not evidence of a mismatch and never triggers a repair.
        if (!matches(handle, same)) return false;
        if (same) return true;
        // All names and ancestors are pinned without delete sharing. Use the
        // low-level file setter: SetSecurityInfo automatically propagates changes
        // to uninspected children, including removal of previous inherited ACEs.
        // SetFileSecurity changes this pinned object only, never follows the tree.
        return SetFileSecurityW(path.c_str(), OWNER_SECURITY_INFORMATION |
            DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION, value) != FALSE;
    }
};
bool ordinary(HANDLE handle, BY_HANDLE_FILE_INFORMATION& info) {
    return handle != INVALID_HANDLE_VALUE && GetFileInformationByHandle(handle, &info) &&
        !(info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT);
}
}  // namespace

Status restrictDirectoryToAdministrators(const std::filesystem::path& directory) {
    if (!directory.is_absolute() || directory != directory.lexically_normal() ||
        directory.root_name().wstring().size() != 2 || directory == directory.root_path())
        return Error(ErrorCode::invalid_argument);
    std::array<wchar_t, 32> filesystem{};
    if (GetDriveTypeW(directory.root_path().c_str()) != DRIVE_FIXED ||
        !GetVolumeInformationW(directory.root_path().c_str(), nullptr, 0, nullptr, nullptr, nullptr,
            filesystem.data(), static_cast<DWORD>(filesystem.size())) ||
        _wcsicmp(filesystem.data(), L"NTFS") != 0) return Error(ErrorCode::not_supported);
    const auto failure = [] { return Error(ErrorCode::config_unreadable, "the data tree cannot be safely secured"); };
    DataDescriptor flat, inherited;
    if (!flat.create(false) || !inherited.create(true)) return failure();
    // Pin every ancestor before creating or securing the product directory.
    std::vector<SecurityHandle> ancestors;
    auto current = directory.root_path();
    auto pinAncestor = [&](const std::filesystem::path& path) {
        SecurityHandle handle(CreateFileW(path.c_str(), FILE_LIST_DIRECTORY | READ_CONTROL,
            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
        BY_HANDLE_FILE_INFORMATION info{};
        if (!ordinary(handle.value, info) || !(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) return false;
        ancestors.push_back(std::move(handle));
        return true;
    };
    if (!pinAncestor(current)) return failure();
    for (const auto& part : directory.parent_path().relative_path()) {
        if (part.wstring().find(L':') != std::wstring::npos) return failure();
        current /= part;
        if (!pinAncestor(current)) return failure();
    }
    if (directory.filename().wstring().find(L':') != std::wstring::npos) return failure();
    SECURITY_ATTRIBUTES security{sizeof(security), inherited.value, FALSE};
    if (!CreateDirectoryW(directory.c_str(), &security) && GetLastError() != ERROR_ALREADY_EXISTS) return failure();

    std::vector<DataEntry> entries;
    std::function<bool(const std::filesystem::path&)> collect = [&](const std::filesystem::path& path) {
        SecurityHandle handle(CreateFileW(path.c_str(), READ_CONTROL | WRITE_DAC | WRITE_OWNER | FILE_READ_ATTRIBUTES,
            FILE_SHARE_READ, nullptr, OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
        BY_HANDLE_FILE_INFORMATION info{};
        if (!ordinary(handle.value, info)) return false;
        const bool isDirectory = (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        if (path == directory && !isDirectory) return false;
        // First close directory mutation rights without inheritable ACEs. This
        // cannot propagate ACL changes through any as-yet uninspected child.
        if (isDirectory) {
            bool canonical = false;
            if (!inherited.matches(handle.value, canonical)) return false;
            if (!canonical && !flat.apply(path, handle.value)) return false;
        }
        entries.push_back({path, std::move(handle), info});
        if (isDirectory) {
            std::error_code error;
            std::filesystem::directory_iterator it(path, error), end;
            if (error) return false;
            while (it != end) {
                if (!collect(it->path())) return false;
                it.increment(error);
                if (error) return false;
            }
        }
        return true;
    };
    if (!collect(directory)) return failure();

    // Every name is pinned without delete sharing. An inode with more NTFS links
    // than names collected here has at least one name outside this data tree.
    // Never change its ACL. config.json/config.previous count as two names of
    // one inode, preserving the existing hardlink and journal identity protocol.
    using FileKey = std::pair<DWORD, std::uint64_t>;
    const auto key = [](const BY_HANDLE_FILE_INFORMATION& info) {
        return FileKey{info.dwVolumeSerialNumber,
            (static_cast<std::uint64_t>(info.nFileIndexHigh) << 32) | info.nFileIndexLow};
    };
    std::map<FileKey, DWORD> names;
    for (const auto& entry : entries)
        if (!(entry.information.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) ++names[key(entry.information)];
    const auto linksUnchanged = [&](const DataEntry& entry) {
        BY_HANDLE_FILE_INFORMATION info{};
        return ordinary(entry.handle.value, info) && key(info) == key(entry.information) &&
            ((info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || names[key(info)] == info.nNumberOfLinks);
    };
    for (const auto& entry : entries) if (!linksUnchanged(entry)) return failure();
    // Protect every file first, then directories bottom-up. All existing children
    // are protected before adding inheritable ACEs. Future descendants inherit the
    // complete read-only policy. No recursive OS ACL propagation is requested.
    for (const auto& entry : entries) {
        if (!(entry.information.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
            (!linksUnchanged(entry) || !flat.apply(entry.path, entry.handle.value) || !linksUnchanged(entry)))
            return failure();
    }
    for (auto it = entries.rbegin(); it != entries.rend(); ++it)
        if ((it->information.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && !inherited.apply(it->path, it->handle.value))
            return failure();
    // A new external hardlink can be added even while names are pinned. The
    // accepted startup-maintenance race shares the repaired inode's ACL with that
    // new name; detected count changes still fail closed, never report success.
    for (const auto& entry : entries) if (!linksUnchanged(entry)) return failure();
    return ok();
}

}  // namespace phpvpn
