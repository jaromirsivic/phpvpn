#include <windows.h>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <vector>
#include <cstring>

namespace {
using Bytes = std::vector<unsigned char>;
void require(bool value, const char* reason) { if (!value) throw std::runtime_error(reason); }
std::uint16_t u16(const unsigned char* p) { return static_cast<std::uint16_t>(p[0] | (p[1] << 8)); }
std::uint32_t u32(const unsigned char* p) { return std::uint32_t(p[0]) | (std::uint32_t(p[1]) << 8) | (std::uint32_t(p[2]) << 16) | (std::uint32_t(p[3]) << 24); }
Bytes resource(HMODULE module, WORD id, LPCWSTR type) {
    auto found = FindResourceW(module, MAKEINTRESOURCEW(id), type);
    require(found != nullptr, "required PE resource missing");
    const auto size = SizeofResource(module, found);
    const auto* p = static_cast<const unsigned char*>(LockResource(LoadResource(module, found)));
    require(p && size, "resource cannot be loaded");
    return Bytes(p, p + size);
}
void verify(HMODULE module, WORD id, const std::filesystem::path& source) {
    std::ifstream file(source, std::ios::binary);
    require(file.good(), "source ICO missing");
    const Bytes ico{std::istreambuf_iterator<char>(file), {}};
    const auto group = resource(module, id, RT_GROUP_ICON);
    require(ico.size() >= 6 && group.size() >= 6 && u16(ico.data()) == 0 && u16(ico.data()+2) == 1, "invalid ICO header");
    const auto count = u16(ico.data() + 4);
    require(count == 3 && group.size() == 6u + 14u * count && ico.size() >= 6u + 16u * count, "invalid group size");
    require(std::memcmp(ico.data(), group.data(), 6) == 0, "group header differs from source");
    for (unsigned i = 0; i < count; ++i) {
        const auto* entry = ico.data() + 6 + 16 * i;
        const auto* embedded = group.data() + 6 + 14 * i;
        require(std::memcmp(entry, embedded, 12) == 0, "icon frame metadata differs");
        const auto length = u32(entry + 8), offset = u32(entry + 12);
        require(offset <= ico.size() && length <= ico.size() - offset, "frame outside ICO");
        auto frame = resource(module, u16(embedded + 12), RT_ICON);
        require(frame.size() == length && std::memcmp(frame.data(), ico.data() + offset, length) == 0, "embedded icon bytes differ from source");
        const int width = entry[0] ? entry[0] : 256, height = entry[1] ? entry[1] : 256;
        auto decoded = CreateIconFromResourceEx(frame.data(), static_cast<DWORD>(frame.size()), TRUE, 0x00030000, width, height, 0);
        require(decoded != nullptr, "icon frame cannot be decoded");
        DestroyIcon(decoded);
        std::cout << " group " << id << " frame " << width << 'x' << height << " matches and decodes\n";
    }
}
BOOL CALLBACK countGroups(HMODULE, LPCWSTR, LPWSTR, LONG_PTR value) { ++*reinterpret_cast<unsigned*>(value); return TRUE; }
}
int wmain(int argc, wchar_t** argv) {
    try {
        require(argc == 3, "usage: phpvpn-icon-resource-tests <apps-directory> <source-assets>");
        const std::filesystem::path apps(argv[1]), assets(argv[2]);
        for (const auto* name : {L"phpvpn-service.exe", L"phpvpn-tray.exe", L"phpvpn.exe", L"install-phpvpn.exe", L"uninstall-phpvpn.exe"}) {
            const auto path = apps / name;
            HMODULE module = LoadLibraryExW(path.c_str(), nullptr, LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);
            require(module != nullptr, "PE could not be opened read-only");
            try {
                std::wcout << name << L'\n';
                const bool install = std::wstring(name) == L"install-phpvpn.exe";
                const bool uninstall = std::wstring(name) == L"uninstall-phpvpn.exe";
                verify(module, 100, assets / (install ? L"connected.ico" : uninstall ? L"disconnected.ico" : L"default.ico"));
                const bool tray = std::wstring(name) == L"phpvpn-tray.exe";
                if (tray) {
                    verify(module, 101, assets / L"disconnected.ico");
                    verify(module, 102, assets / L"connecting.ico");
                    verify(module, 103, assets / L"connected.ico");
                }
                unsigned count = 0;
                require(EnumResourceNamesW(module, RT_GROUP_ICON, countGroups, reinterpret_cast<LONG_PTR>(&count)) != FALSE, "group enumeration failed");
                require(count == (tray ? 4u : 1u), "unexpected/duplicate icon groups");
                const auto manifest = resource(module, 1, RT_MANIFEST);
                const std::string xml(manifest.begin(), manifest.end());
                const auto level = install || uninstall ? "requireAdministrator" : "asInvoker";
                require(xml.find(std::string("level=\"") + level + "\"") != std::string::npos, "incorrect execution level");
                require(xml.find("permonitorv2") != std::string::npos && xml.find("Microsoft.Windows.Common-Controls") != std::string::npos, "missing DPI or visual styles");
                count = 0;
                require(EnumResourceNamesW(module, RT_MANIFEST, countGroups, reinterpret_cast<LONG_PTR>(&count)) && count == 1, "duplicate manifests");
                (void)resource(module, 1, RT_VERSION);
                const auto versionSize = GetFileVersionInfoSizeW(path.c_str(), nullptr);
                require(versionSize != 0, "version metadata unavailable");
                Bytes version(versionSize);
                require(GetFileVersionInfoW(path.c_str(), 0, versionSize, version.data()) != FALSE, "version metadata could not be read");
                void* originalName = nullptr;
                UINT originalLength = 0;
                require(VerQueryValueW(version.data(), L"\\StringFileInfo\\040904B0\\OriginalFilename", &originalName, &originalLength) != FALSE && originalLength > 1,
                    "original filename metadata missing");
                require(std::wstring(static_cast<const wchar_t*>(originalName)) == name, "wrong original filename metadata");
            } catch (...) { FreeLibrary(module); throw; }
            FreeLibrary(module);
        }
        std::cout << "PASS: all production PE icon groups match exact ICO frames, manifest and version preserved\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
