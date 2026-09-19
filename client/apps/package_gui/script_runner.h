#pragma once
#include "operation_model.h"
#include <atomic>
#include <filesystem>
#include <functional>
#include <string_view>

namespace phpvpn::package {
// Narrow OS seam for failure tests. No production command-line override exists.
struct ProcessApi {
    decltype(&CreatePipe) createPipe = CreatePipe;
    decltype(&CreateProcessW) createProcess = CreateProcessW;
    decltype(&PeekNamedPipe) peek = PeekNamedPipe;
    decltype(&ReadFile) read = ReadFile;
    decltype(&WaitForSingleObject) wait = WaitForSingleObject;
    decltype(&GetExitCodeProcess) exitCode = GetExitCodeProcess;
};
class Utf8Decoder {
public:
    bool append(std::string_view bytes, bool final, std::wstring& output);
private:
    std::string pending_;
};
std::filesystem::path executableDirectory();
std::filesystem::path windowsPowerShell();
std::wstring quoteArgument(std::wstring_view value);
using Output = std::function<void(std::wstring)>;
Result runScript(Mode mode, const std::filesystem::path& directory,
                 const Output& output, const std::atomic_bool& cancel,
                 const ProcessApi& api = {},
                 const std::filesystem::path& powerShell = windowsPowerShell());
} // namespace phpvpn::package
