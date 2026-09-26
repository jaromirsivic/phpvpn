// Console client of the PHPVPN service.
//
// The application is a pure IPC client: it never loads the tunnel library, never
// opens the adapter and never reads or writes the configuration file directly.
// Installing, removing, starting and stopping the service are deliberately not
// IPC operations, so this application does not offer them.
//
// The same commands, output formats, exit codes (0 success, 1 failure, 2 usage)
// and the 10 s IPC timeout apply on Windows (named pipe, wide argv) and Linux
// (Unix socket, UTF-8 argv). Linux only adds a hint line after an unreachable
// service error; the error text and code stay the same.

#if defined(_WIN32)
#include <windows.h>
#endif

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "core/config/config_document.h"
#include "core/ipc/ipc_protocol.h"
#include "core/protocol/json.h"
#include "core/crypto/crypto_provider.h"
#if defined(_WIN32)
#include "platform/windows/ipc_pipe.h"
#else
#include "platform/linux/ipc_unix.h"
#endif

namespace {

// One byte over the configuration limit is enough for the service to answer
// config_too_large, so a larger file is never read completely.
constexpr std::size_t kSetConfigReadLimit = phpvpn::kMaxConfigFileBytes + 1;
constexpr std::int64_t kIpcTimeoutMs = 10000;

int printUsage() {
    std::fputs(
        "usage: phpvpn <command> [argument]\n"
        "\n"
        "  status         print the current service and connection state\n"
        "  connect        ask the service to establish the connection\n"
        "  disconnect     ask the service to stop the connection\n"
        "  info           print the verified server information\n"
        "  get-config     print the stored configuration document\n"
        "  set-config F   store the configuration document from file F\n"
        "  diagnostics    print the extended local diagnostics snapshot\n",
        stdout);
    return 2;
}

// The path is native (UTF-16 on Windows), so a non-ASCII file name opens too.
std::string readFile(const std::filesystem::path& path, bool& ok) {
    ok = false;
    std::ifstream stream(path, std::ios::in | std::ios::binary);
    if (!stream.is_open()) {
        return std::string();
    }
    std::string content(kSetConfigReadLimit, '\0');
    stream.read(content.data(), static_cast<std::streamsize>(content.size()));
    if (stream.bad()) {
        return std::string();
    }
    content.resize(static_cast<std::size_t>(stream.gcount()));
    ok = true;
    return content;
}

#if defined(_WIN32)
std::string narrow(const wchar_t* text) {
    if (text == nullptr) {
        return std::string();
    }
    const int required = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    if (required <= 1) {
        return std::string();
    }
    std::string out(static_cast<std::size_t>(required - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text, -1, out.data(), required, nullptr, nullptr);
    return out;
}
#endif

// Renders the error envelope of client.md chapter "Verejne lokalni IPC" as one
// readable line. A failure always arrives inside an envelope, even for the two
// commands whose successful answer carries none.
bool printErrorEnvelope(const phpvpn::JsonValue& document) {
    const phpvpn::JsonValue* version = document.find("ipcVersion");
    const phpvpn::JsonValue* error = document.find("error");
    if (version == nullptr || error == nullptr || !error->isObject()) {
        return false;
    }

    const phpvpn::JsonValue* code = error->find("code");
    const phpvpn::JsonValue* description = error->find("description");
    std::fprintf(stderr, "%s: %s\n",
                 code != nullptr && code->isString() ? code->stringValue().c_str() : "error",
                 description != nullptr && description->isString()
                     ? description->stringValue().c_str()
                     : "the command failed");
    return true;
}

// Prints an IPC envelope in a readable form, or the raw document when the
// command answered without an envelope.
int printResponse(const std::string& response, bool expectsEnvelope) {
    auto document = phpvpn::Json::parse(response);
    if (!document.ok() || !document.value().isObject()) {
        if (!expectsEnvelope) {
            // `get-info` and `get-config` return the verified server plaintext
            // and the exact stored document, so anything the service sends is
            // forwarded unchanged rather than reformatted.
            std::fputs(response.c_str(), stdout);
            std::fputc('\n', stdout);
            return 0;
        }
        std::fputs("the service returned an unreadable response\n", stderr);
        return 1;
    }

    if (printErrorEnvelope(document.value())) {
        return 1;
    }

    if (!expectsEnvelope) {
        std::fputs(response.c_str(), stdout);
        std::fputc('\n', stdout);
        return 0;
    }

    std::fputs(phpvpn::Json::serializePretty(document.value()).c_str(), stdout);
    std::fputc('\n', stdout);
    return 0;
}

#if !defined(_WIN32)
// Explanations for the Linux cases the error text cannot tell apart. The code
// and the first line stay exactly those of the service error.
void printLinuxHint(phpvpn::IpcSocketFailure failure) {
    const char* hint = nullptr;
    switch (failure) {
        case phpvpn::IpcSocketFailure::serviceNotRunning:
            hint = "the phpvpn service is not running or its socket is missing (systemctl status phpvpn)";
            break;
        case phpvpn::IpcSocketFailure::accessDenied:
            hint = "access to the phpvpn control socket was denied (check the permissions of /run/phpvpn)";
            break;
        case phpvpn::IpcSocketFailure::invalidPeer:
            hint = "the control socket is not served by the root phpvpn service";
            break;
        case phpvpn::IpcSocketFailure::connectionClosed:
            hint = "the phpvpn service closed the connection (journalctl -u phpvpn)";
            break;
        default:
            break;
    }
    if (hint != nullptr) std::fprintf(stderr, "hint: %s\n", hint);
}
#endif

// Runs one command. `file` is the native path argument of set-config, null
// when the command line has none.
int run(const std::string& command, const std::filesystem::path* file, const std::string& socketPath) {
    phpvpn::IpcCommand ipcCommand = phpvpn::IpcCommand::get_status;
    bool expectsEnvelope = true;
    std::string configuration;

    if (command == "status") {
        ipcCommand = phpvpn::IpcCommand::get_status;
    } else if (command == "connect") {
        ipcCommand = phpvpn::IpcCommand::connect;
    } else if (command == "disconnect") {
        ipcCommand = phpvpn::IpcCommand::disconnect;
    } else if (command == "diagnostics") {
        ipcCommand = phpvpn::IpcCommand::get_diagnostics;
    } else if (command == "info") {
        ipcCommand = phpvpn::IpcCommand::get_info;
        expectsEnvelope = false;
    } else if (command == "get-config") {
        ipcCommand = phpvpn::IpcCommand::get_config;
        expectsEnvelope = false;
    } else if (command == "set-config") {
        if (file == nullptr) {
            std::fputs("set-config needs the path of a configuration document\n", stderr);
            return 2;
        }
        bool ok = false;
        configuration = readFile(*file, ok);
        if (!ok) {
            std::fputs("the configuration document could not be read\n", stderr);
            return 1;
        }
        ipcCommand = phpvpn::IpcCommand::set_config;
    } else {
        return printUsage();
    }

    auto crypto = phpvpn::createPlatformCryptoProvider();
    if (!crypto.ok()) {
        std::fputs("the platform cryptography is unavailable\n", stderr);
        return 1;
    }
    auto requestId = crypto.value()->randomUuidV4();
    if (!requestId.ok()) {
        std::fputs("a request identifier could not be generated\n", stderr);
        return 1;
    }

    const std::string request =
        phpvpn::IpcProtocol::buildRequest(ipcCommand, requestId.value(), configuration);

#if defined(_WIN32)
    (void)socketPath;
    auto response = phpvpn::sendIpcRequest(request, kIpcTimeoutMs);
    if (!response.ok()) {
        std::fprintf(stderr, "%s\n", response.error().message().c_str());
        return 1;
    }
#else
    phpvpn::IpcSocketClientOptions options;
    options.socketPath = socketPath;
    phpvpn::IpcExchangeOutcome outcome;
    auto response = phpvpn::IpcSocketClient(options).exchange(request, kIpcTimeoutMs, -1, &outcome);
    if (!response.ok()) {
        std::fprintf(stderr, "%s\n", response.error().message().c_str());
        printLinuxHint(outcome.failure);
        return 1;
    }
#endif

    return printResponse(response.value(), expectsEnvelope);
}

}  // namespace

#if defined(_WIN32)
int wmain(int argc, wchar_t** argv) {
    if (argc < 2) {
        return printUsage();
    }
    const std::filesystem::path file = argc >= 3 ? std::filesystem::path(argv[2]) : std::filesystem::path();
    return run(narrow(argv[1]), argc >= 3 ? &file : nullptr, std::string());
}
#else
int main(int argc, char** argv) {
    // Test only, deliberately absent from the usage text: `--socket PATH` in
    // front of the command talks to an isolated service instance of the end to
    // end tests. The peer must still be root, so it cannot divert secrets.
    std::string socketPath;
    int first = 1;
    if (argc >= 3 && std::string(argv[1]) == "--socket") {
        socketPath = argv[2];
        first = 3;
    }
    if (argc - first < 1) {
        return printUsage();
    }
    const bool hasFile = argc - first >= 2;
    const std::filesystem::path file = hasFile ? std::filesystem::path(argv[first + 1]) : std::filesystem::path();
    return run(argv[first], hasFile ? &file : nullptr, socketPath);
}
#endif
