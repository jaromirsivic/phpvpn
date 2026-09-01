// Console client of the PHPVPN service.
//
// The application is a pure IPC client: it never loads the tunnel library, never
// opens the adapter and never reads or writes the configuration file directly.
// Installing, removing, starting and stopping the service are deliberately not
// IPC operations, so this application does not offer them.

#include <windows.h>

#include <cstdio>
#include <string>
#include <vector>

#include "core/ipc/ipc_protocol.h"
#include "core/protocol/json.h"
#include "core/crypto/crypto_provider.h"
#include "platform/windows/ipc_pipe.h"

namespace {

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

std::string readFile(const std::string& path, bool& ok) {
    ok = false;
    FILE* file = nullptr;
    if (fopen_s(&file, path.c_str(), "rb") != 0 || file == nullptr) {
        return std::string();
    }
    std::string content;
    char buffer[8192];
    std::size_t read = 0;
    while ((read = std::fread(buffer, 1, sizeof(buffer), file)) > 0) {
        content.append(buffer, read);
    }
    std::fclose(file);
    ok = true;
    return content;
}

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

}  // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc < 2) {
        return printUsage();
    }

    const std::string command = narrow(argv[1]);

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
        if (argc < 3) {
            std::fputs("set-config needs the path of a configuration document\n", stderr);
            return 2;
        }
        bool ok = false;
        configuration = readFile(narrow(argv[2]), ok);
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

    auto response = phpvpn::sendIpcRequest(request);
    if (!response.ok()) {
        std::fprintf(stderr, "%s\n", response.error().message().c_str());
        return 1;
    }

    return printResponse(response.value(), expectsEnvelope);
}
