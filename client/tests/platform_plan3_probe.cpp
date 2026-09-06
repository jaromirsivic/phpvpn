// Isolated B01 API feasibility probe. No installed service, routes or secrets.
#include <winsock2.h>
#include <windows.h>
#include <winhttp.h>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "ws2_32.lib")

void check(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
struct HttpContext {
    HANDLE closed = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    std::atomic<unsigned> closing{0};
    ~HttpContext() { CloseHandle(closed); }
};
void CALLBACK callback(HINTERNET, DWORD_PTR context, DWORD status, void*, DWORD) {
    auto* state = reinterpret_cast<HttpContext*>(context);
    if (state && status == WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING) {
        ++state->closing;
        SetEvent(state->closed);
    }
}
void httpProbe(bool tls) {
    SOCKET listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    check(bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0, "bind");
    check(listen(listener, 1) == 0, "listen");
    int size = sizeof(address);
    check(getsockname(listener, reinterpret_cast<sockaddr*>(&address), &size) == 0, "port");
    HANDLE release = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    std::thread peer([&] {
        SOCKET socket = accept(listener, nullptr, nullptr);
        if (socket != INVALID_SOCKET) {
            WaitForSingleObject(release, 5000);
            closesocket(socket);
        }
    });
    HINTERNET session = WinHttpOpen(L"PHPVPN-B01", WINHTTP_ACCESS_TYPE_NO_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, WINHTTP_FLAG_ASYNC);
    check(session != nullptr, "session");
    HINTERNET connection = WinHttpConnect(session, L"127.0.0.1", ntohs(address.sin_port), 0);
    HINTERNET request = WinHttpOpenRequest(connection, L"POST", L"/", nullptr,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, tls ? WINHTTP_FLAG_SECURE : 0);
    HttpContext context;
    DWORD_PTR pointer = reinterpret_cast<DWORD_PTR>(&context);
    check(WinHttpSetOption(request, WINHTTP_OPTION_CONTEXT_VALUE, &pointer, sizeof(pointer)), "context");
    check(WinHttpSetStatusCallback(request, callback, WINHTTP_CALLBACK_FLAG_ALL_NOTIFICATIONS, 0)
        != WINHTTP_INVALID_STATUS_CALLBACK, "callback");
    check(WinHttpSendRequest(request, nullptr, 0, nullptr, 0, 0, pointer), "send");
    Sleep(100);
    const auto begin = GetTickCount64();
    check(WinHttpCloseHandle(request), "close");
    check(WaitForSingleObject(context.closed, 2000) == WAIT_OBJECT_0, "terminal callback timeout");
    check(context.closing == 1, "duplicate terminal callback");
    std::cout << (tls ? "TLS" : "HTTP") << " terminal close ms=" << GetTickCount64() - begin << '\n';
    WinHttpCloseHandle(connection);
    WinHttpCloseHandle(session);
    SetEvent(release);
    peer.join();
    CloseHandle(release);
    closesocket(listener);
}
void pipeProbe() {
    const auto name = L"\\\\.\\pipe\\phpvpn-b01-" + std::to_wstring(GetCurrentProcessId());
    HANDLE pipe = CreateNamedPipeW(name.c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
        PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
        1, 4096, 4096, 0, nullptr);
    check(pipe != INVALID_HANDLE_VALUE, "pipe");
    OVERLAPPED io{};
    io.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    ConnectNamedPipe(pipe, &io);
    HANDLE client = CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    check(client != INVALID_HANDLE_VALUE, "client");
    DWORD transferred = 0;
    check(GetOverlappedResult(pipe, &io, &transferred, TRUE), "connect completion");
    for (bool write : {false, true}) {
        std::string buffer(write ? 1024 * 1024 : 4096, 'x');
        ResetEvent(io.hEvent);
        const BOOL result = write ? WriteFile(pipe, buffer.data(), static_cast<DWORD>(buffer.size()), nullptr, &io)
                                  : ReadFile(pipe, buffer.data(), static_cast<DWORD>(buffer.size()), nullptr, &io);
        check(!result && GetLastError() == ERROR_IO_PENDING, "pending I/O");
        const auto begin = GetTickCount64();
        check(CancelIoEx(pipe, &io), "cancel I/O");
        check(WaitForSingleObject(io.hEvent, 2000) == WAIT_OBJECT_0, "terminal I/O timeout");
        check(!GetOverlappedResult(pipe, &io, &transferred, FALSE) && GetLastError() == ERROR_OPERATION_ABORTED,
              "terminal I/O result");
        std::cout << "pipe " << (write ? "write" : "read") << " cancel ms=" << GetTickCount64() - begin << '\n';
    }
    CloseHandle(client);
    CloseHandle(pipe);
    CloseHandle(io.hEvent);
}
void fileProbe() {
    auto directory = std::filesystem::temp_directory_path() / (L"phpvpn-b01-" + std::to_wstring(GetCurrentProcessId()));
    check(std::filesystem::create_directory(directory), "private probe directory");
    auto target = directory / "config.json";
    auto temporary = directory / "new.json";
    auto backup = directory / "previous.json";
    auto write = [](const std::filesystem::path& path, const char* data) {
        HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, 0, nullptr);
        check(file != INVALID_HANDLE_VALUE, "create new");
        DWORD written = 0;
        check(WriteFile(file, data, 3, &written, nullptr) && written == 3, "write");
        check(FlushFileBuffers(file), "flush");
        CloseHandle(file);
    };
    write(target, "old");
    write(temporary, "new");
    HANDLE locked = CreateFileW(target.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    check(!ReplaceFileW(target.c_str(), temporary.c_str(), backup.c_str(), 0, nullptr, nullptr), "locked replace must fail");
    check(std::filesystem::exists(target) && std::filesystem::exists(temporary), "failed replace preserved files");
    CloseHandle(locked);
    check(ReplaceFileW(target.c_str(), temporary.c_str(), backup.c_str(), 0, nullptr, nullptr), "replace");
    std::string current, previous;
    std::ifstream(target) >> current;
    std::ifstream(backup) >> previous;
    check(current == "new" && previous == "old", "commit contents");
    check(!MoveFileExW(backup.c_str(), target.c_str(), MOVEFILE_WRITE_THROUGH), "create-only must not replace");
    std::cout << "file locked failure, flush, replacement, backup and create-only: PASS\n";
    // Only the three exact paths created above are removed.
    std::filesystem::remove(target);
    std::filesystem::remove(backup);
    std::filesystem::remove(directory);
}
int main() {
    try {
        WSADATA data{};
        check(WSAStartup(MAKEWORD(2, 2), &data) == 0, "winsock");
        httpProbe(false);
        httpProbe(true);
        pipeProbe();
        fileProbe();
        WSACleanup();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
