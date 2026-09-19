#include "script_runner.h"
#include <algorithm>
#include <array>
#include <stdexcept>
#include <vector>

namespace phpvpn::package {
namespace {
struct Handle {
    HANDLE value = nullptr;
    ~Handle() { reset(); }
    void reset(HANDLE next = nullptr) {
        if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value);
        value = next;
    }
};
Result failure(const wchar_t* operation, DWORD code = GetLastError()) {
    return {0, std::wstring(operation) + L" (Windows error " + std::to_wstring(code) + L")."};
}
}
bool Utf8Decoder::append(std::string_view bytes, bool final, std::wstring& output) {
    pending_.append(bytes);
    std::size_t complete = 0;
    while (complete < pending_.size()) {
        const auto lead = static_cast<unsigned char>(pending_[complete]);
        // The text control is NUL-terminated. Reject binary NUL instead of losing the
        // remainder of a valid UTF-8 batch silently at the display boundary.
        if (lead == 0) return false;
        const std::size_t count = lead < 0x80 ? 1 : lead >= 0xc2 && lead <= 0xdf ? 2 :
            lead >= 0xe0 && lead <= 0xef ? 3 : lead >= 0xf0 && lead <= 0xf4 ? 4 : 0;
        if (!count) return false;
        if (pending_.size() - complete < count) break;
        complete += count;
    }
    if (complete) {
        const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, pending_.data(),
            static_cast<int>(complete), nullptr, 0);
        if (!length) return false;
        const auto offset = output.size();
        output.resize(offset + static_cast<std::size_t>(length));
        if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, pending_.data(),
            static_cast<int>(complete), output.data() + offset, length)) return false;
        pending_.erase(0, complete);
    }
    return !final || pending_.empty();
}
std::filesystem::path executableDirectory() {
    std::wstring path(32768, L'\0');
    const auto size = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (!size || size >= path.size()) throw std::runtime_error("Cannot locate the launcher executable.");
    path.resize(size);
    return std::filesystem::path(path).parent_path();
}
std::filesystem::path windowsPowerShell() {
    std::array<wchar_t, 32768> system{};
    const auto size = GetSystemDirectoryW(system.data(), static_cast<UINT>(system.size()));
    if (!size || size >= system.size()) return {};
    return std::filesystem::path(system.data()) / L"WindowsPowerShell" / L"v1.0" / L"powershell.exe";
}
std::wstring quoteArgument(std::wstring_view value) {
    std::wstring result = L"\"";
    std::size_t slashes = 0;
    for (const auto ch : value) {
        if (ch == L'\\') { ++slashes; continue; }
        result.append(slashes * (ch == L'\"' ? 2 : 1), L'\\');
        slashes = 0;
        if (ch == L'\"') result += L'\\';
        result += ch;
    }
    result.append(slashes * 2, L'\\');
    return result + L'\"';
}
Result runScript(Mode mode, const std::filesystem::path& directory, const Output& output,
                 const std::atomic_bool& cancel, const ProcessApi& api,
                 const std::filesystem::path& powerShell) {
    try {
        if (cancel.load()) return {0, L"Output capture interrupted by session shutdown. Operation result is unknown."};
        const auto script = directory / scriptName(mode);
        if (!directory.is_absolute() || !std::filesystem::is_regular_file(script))
            return {0, L"The package script is missing: " + script.wstring()};
        if (!powerShell.is_absolute() || !std::filesystem::is_regular_file(powerShell))
            return {0, L"Windows PowerShell could not be found in the system directory."};
        SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
        Handle reader, writer, input, process, thread;
        if (!api.createPipe(&reader.value, &writer.value, &security, 0)) return failure(L"CreatePipe failed");
        if (!SetHandleInformation(reader.value, HANDLE_FLAG_INHERIT, 0)) return failure(L"Pipe inheritance setup failed");
        input.value = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
            &security, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (input.value == INVALID_HANDLE_VALUE) return failure(L"Opening standard input failed");
        SIZE_T bytes = 0;
        InitializeProcThreadAttributeList(nullptr, 1, 0, &bytes);
        std::vector<unsigned char> storage(bytes);
        auto* attributes = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
        if (!InitializeProcThreadAttributeList(attributes, 1, 0, &bytes)) return failure(L"Process attributes failed");
        struct AttributeLifetime { LPPROC_THREAD_ATTRIBUTE_LIST list; ~AttributeLifetime() { DeleteProcThreadAttributeList(list); } } lifetime{attributes};
        HANDLE inherited[]{writer.value, input.value};
        if (!UpdateProcThreadAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
            inherited, sizeof(inherited), nullptr, nullptr)) return failure(L"Handle allowlist failed");
        STARTUPINFOEXW startup{};
        startup.StartupInfo.cb = sizeof(startup);
        startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
        startup.StartupInfo.hStdOutput = startup.StartupInfo.hStdError = writer.value;
        startup.StartupInfo.hStdInput = input.value;
        startup.lpAttributeList = attributes;
        auto command = quoteArgument(powerShell.wstring()) +
            L" -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -File " + quoteArgument(script.wstring());
        PROCESS_INFORMATION child{};
        if (!api.createProcess(powerShell.c_str(), command.data(), nullptr, nullptr, TRUE,
            CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT, nullptr, directory.c_str(),
            &startup.StartupInfo, &child)) return failure(L"CreateProcess failed");
        process.value = child.hProcess;
        thread.value = child.hThread;
        writer.reset();
        input.reset();
        Utf8Decoder decoder;
        bool pipeClosed = false;
        auto drain = [&](DWORD available) -> Result {
            std::array<char, 16384> buffer{};
            while (available) {
                if (cancel.load()) return {0, L"Output capture interrupted by session shutdown. Operation result is unknown."};
                DWORD read = 0;
                if (!api.read(reader.value, buffer.data(), (std::min)(available, static_cast<DWORD>(buffer.size())), &read, nullptr))
                    return failure(L"Reading script output failed");
                if (!read) return {0, L"The output pipe returned no data while bytes were pending."};
                std::wstring decoded;
                if (!decoder.append({buffer.data(), read}, false, decoded)) return {0, L"Script output contains invalid UTF-8 or a binary NUL character."};
                if (!decoded.empty()) output(std::move(decoded));
                available -= read;
            }
            return {};
        };
        for (;;) {
            if (cancel.load()) return {0, L"Output capture interrupted by session shutdown. Operation result is unknown."};
            const auto wait = api.wait(process.value, 0);
            if (wait != WAIT_OBJECT_0 && wait != WAIT_TIMEOUT) return failure(L"Waiting for PowerShell failed");
            DWORD available = 0;
            if (!pipeClosed && !api.peek(reader.value, nullptr, 0, nullptr, &available, nullptr)) {
                if (GetLastError() != ERROR_BROKEN_PIPE) return failure(L"Inspecting script output failed");
                pipeClosed = true;
            }
            // Snapshot the bytes after direct child exit. Never wait for EOF:
            // a successfully launched tray may retain the inherited write handle.
            if (auto result = drain(available); !result.error.empty()) return result;
            if (wait == WAIT_OBJECT_0) {
                DWORD code = 0;
                if (!api.exitCode(process.value, &code)) return failure(L"Reading PowerShell exit code failed");
                std::wstring tail;
                if (!decoder.append({}, true, tail)) return {code, L"Script output ended in an incomplete UTF-8 character."};
                if (!tail.empty()) output(std::move(tail));
                return {code, {}};
            }
            Sleep(10);
        }
    } catch (const std::exception&) {
        return {0, L"The script runner could not access its files or capture output."};
    } catch (...) {
        return {0, L"Unexpected script runner failure."};
    }
}
} // namespace phpvpn::package
