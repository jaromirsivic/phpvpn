// WinHTTP transport.
//
// One session is reused for the whole connection so that the TLS handshake and
// the TCP connection are amortised across the polling requests. Redirects are
// disabled, no proxy is used and the response is read in bounded chunks with a
// hard cap, so a hostile server cannot exhaust the client memory.
//
// The total deadline covers name resolution, the connect, the TLS handshake and
// the whole read. When it expires the request handle is closed from the
// cancellation path, which unblocks the synchronous calls immediately.

#include <windows.h>

#include <winhttp.h>

#include <atomic>
#include <mutex>
#include <string>
#include <vector>

#include "core/base/logger.h"
#include "core/config/config.h"
#include "platform/iface/transport.h"

#pragma comment(lib, "winhttp.lib")

namespace phpvpn {
namespace {

constexpr const char* kComponent = "transport";
constexpr DWORD kReadChunkBytes = 64 * 1024;

std::wstring widen(std::string_view text) {
    if (text.empty()) {
        return std::wstring();
    }
    const int required = MultiByteToWideChar(CP_UTF8, 0, text.data(),
                                             static_cast<int>(text.size()), nullptr, 0);
    if (required <= 0) {
        return std::wstring();
    }
    std::wstring out(static_cast<std::size_t>(required), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(),
                        required);
    return out;
}

ErrorCode mapWinHttpError(DWORD code) noexcept {
    switch (code) {
        case ERROR_WINHTTP_NAME_NOT_RESOLVED:
            return ErrorCode::transport_dns_failed;
        case ERROR_WINHTTP_CANNOT_CONNECT:
        case ERROR_WINHTTP_CONNECTION_ERROR:
            return ErrorCode::transport_connect_failed;
        case ERROR_WINHTTP_SECURE_FAILURE:
        case ERROR_WINHTTP_CLIENT_AUTH_CERT_NEEDED:
            return ErrorCode::transport_tls_failed;
        case ERROR_WINHTTP_TIMEOUT:
            return ErrorCode::transport_timeout;
        case ERROR_WINHTTP_OPERATION_CANCELLED:
            return ErrorCode::cancelled;
        default:
            return ErrorCode::transport_receive_failed;
    }
}

class HandleGuard {
public:
    explicit HandleGuard(HINTERNET handle = nullptr) : handle_(handle) {}
    ~HandleGuard() { reset(); }

    HandleGuard(const HandleGuard&) = delete;
    HandleGuard& operator=(const HandleGuard&) = delete;

    HINTERNET get() const noexcept { return handle_; }
    explicit operator bool() const noexcept { return handle_ != nullptr; }

    void reset(HINTERNET handle = nullptr) noexcept {
        if (handle_ != nullptr) {
            WinHttpCloseHandle(handle_);
        }
        handle_ = handle;
    }

    HINTERNET release() noexcept {
        HINTERNET handle = handle_;
        handle_ = nullptr;
        return handle;
    }

private:
    HINTERNET handle_;
};

class WinHttpTransport final : public Transport {
public:
    ~WinHttpTransport() override { closeAll(); }

    Status configure(const TransportSettings& settings) override {
        std::lock_guard<std::mutex> lock(mutex_);
        closeAll();

        auto parsed = parseServerUrl(settings.serverUrl);
        if (!parsed.ok()) {
            return parsed.error();
        }
        url_ = std::move(parsed).value();
        settings_ = settings;

        session_.reset(WinHttpOpen(L"PHPVPN/1.0", WINHTTP_ACCESS_TYPE_NO_PROXY,
                                   WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
        if (!session_) {
            return Error(ErrorCode::transport_unavailable, "the HTTP session could not be created");
        }

        const DWORD timeoutMs =
            static_cast<DWORD>(settings.responseTimeoutSeconds > 0
                                   ? settings.responseTimeoutSeconds * 1000
                                   : 30000);
        WinHttpSetTimeouts(session_.get(), static_cast<int>(timeoutMs), static_cast<int>(timeoutMs),
                           static_cast<int>(timeoutMs), static_cast<int>(timeoutMs));

        // TLS 1.2 and TLS 1.3 only; the older protocol versions are refused.
        DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
#ifdef WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3
        protocols |= WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
#endif
        WinHttpSetOption(session_.get(), WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols,
                         sizeof(protocols));

        connection_.reset(WinHttpConnect(session_.get(), widen(url_.host).c_str(),
                                         static_cast<INTERNET_PORT>(url_.port), 0));
        if (!connection_) {
            return Error(ErrorCode::transport_connect_failed,
                         "the HTTP connection could not be opened");
        }
        return Status::success();
    }

    Result<HttpResponse> post(HttpAction action, std::string_view body) override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!session_ || !connection_) {
            return Error(ErrorCode::transport_unavailable, "the transport is not configured");
        }
        if (body.size() > 0x7FFFFFFFull) {
            return Error(ErrorCode::wire_body_too_large, "the request body is too large to send");
        }

        std::wstring target = widen(url_.path);
        target += L"?action=";
        target += widen(httpActionName(action));

        const DWORD flags = url_.secure ? WINHTTP_FLAG_SECURE : 0u;
        HINTERNET request = WinHttpOpenRequest(connection_.get(), L"POST", target.c_str(), nullptr,
                                               WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                               flags);
        if (request == nullptr) {
            return Error(ErrorCode::transport_send_failed, "the HTTP request could not be created");
        }

        DWORD noRedirect = WINHTTP_DISABLE_REDIRECTS;
        WinHttpSetOption(request, WINHTTP_OPTION_DISABLE_FEATURE, &noRedirect, sizeof(noRedirect));

        if (url_.secure && settings_.trustAnyTlsCertificate) {
            // The transport security of this protocol does not rest on the
            // certificate: every message carries its own authentication tag.
            // The relaxation is still an accepted risk reported by
            // `get-diagnostics`.
            DWORD securityFlags = SECURITY_FLAG_IGNORE_UNKNOWN_CA |
                                  SECURITY_FLAG_IGNORE_CERT_DATE_INVALID |
                                  SECURITY_FLAG_IGNORE_CERT_CN_INVALID |
                                  SECURITY_FLAG_IGNORE_CERT_WRONG_USAGE;
            WinHttpSetOption(request, WINHTTP_OPTION_SECURITY_FLAGS, &securityFlags,
                             sizeof(securityFlags));
        }

        // The handle is published so that a cancellation from another role can
        // close it. Ownership stays with exactly one of the two paths, which is
        // decided under `currentMutex_`, so the handle is never closed twice.
        {
            std::lock_guard<std::mutex> currentLock(currentMutex_);
            current_ = request;
        }

        const std::wstring headers =
            std::wstring(L"Content-Type: ") + widen(httpActionContentType(action)) + L"\r\n";

        auto outcome = performRequest(request, headers, body);

        bool ownsHandle = false;
        {
            std::lock_guard<std::mutex> currentLock(currentMutex_);
            ownsHandle = current_ == request;
            current_ = nullptr;
        }
        if (ownsHandle) {
            WinHttpCloseHandle(request);
        } else {
            return Error(ErrorCode::cancelled, "the request was cancelled");
        }
        return outcome;
    }

    void cancelCurrentRequest() override {
        HINTERNET request = nullptr;
        {
            std::lock_guard<std::mutex> lock(currentMutex_);
            request = current_;
            current_ = nullptr;
        }
        if (request != nullptr) {
            // Closing the handle unblocks every synchronous WinHTTP call that is
            // waiting on it, which is the documented cancellation path.
            WinHttpCloseHandle(request);
        }
    }

private:
    Result<HttpResponse> performRequest(HINTERNET request,
                                        const std::wstring& headers,
                                        std::string_view body) {
        if (!WinHttpSendRequest(request, headers.c_str(), static_cast<DWORD>(headers.size()),
                                const_cast<char*>(body.data()), static_cast<DWORD>(body.size()),
                                static_cast<DWORD>(body.size()), 0)) {
            return Error(mapWinHttpError(GetLastError()), "the request could not be sent");
        }

        if (!WinHttpReceiveResponse(request, nullptr)) {
            return Error(mapWinHttpError(GetLastError()), "no response was received");
        }

        DWORD statusCode = 0;
        DWORD statusSize = sizeof(statusCode);
        if (!WinHttpQueryHeaders(request,
                                 WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                 WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &statusSize,
                                 WINHTTP_NO_HEADER_INDEX)) {
            return Error(ErrorCode::transport_receive_failed,
                         "the response status could not be read");
        }

        HttpResponse response;
        response.statusCode = static_cast<int>(statusCode);

        const std::size_t cap = settings_.maxResponseBodyBytes > 0
                                    ? static_cast<std::size_t>(settings_.maxResponseBodyBytes)
                                    : static_cast<std::size_t>(kDefaultMaxResponseBodyBytes);

        std::vector<char> chunk(kReadChunkBytes);
        for (;;) {
            DWORD read = 0;
            if (!WinHttpReadData(request, chunk.data(), static_cast<DWORD>(chunk.size()), &read)) {
                return Error(mapWinHttpError(GetLastError()), "the response could not be read");
            }
            if (read == 0) {
                break;
            }
            if (response.body.size() + read > cap) {
                return Error(ErrorCode::transport_response_too_large,
                             "the response exceeds the configured limit");
            }
            response.body.append(chunk.data(), read);
        }

        return response;
    }

    void closeAll() {
        connection_.reset();
        session_.reset();
    }

    std::mutex mutex_;
    HandleGuard session_;
    HandleGuard connection_;
    TransportSettings settings_;
    ServerUrl url_;

    std::mutex currentMutex_;
    HINTERNET current_ = nullptr;
};

}  // namespace

Result<std::unique_ptr<Transport>> createPlatformTransport() {
    return std::unique_ptr<Transport>(new WinHttpTransport());
}

}  // namespace phpvpn
