#include "core/protocol/http_headers.h"
// Asynchronous WinHTTP. Only post() invokes request APIs or closes the handle.
// Callbacks signal completion and account bytes. Cancellation only signals the
// owner, which waits for HANDLE_CLOSING before releasing callback state/buffers.
#include <windows.h>
#include <winhttp.h>
#include <array>
#include <atomic>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include "core/config/config_document.h"
#include "platform/iface/transport.h"

namespace phpvpn {
namespace {
std::wstring widen(std::string_view text) {
    if (text.size() > INT_MAX) return {};
    const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (size <= 0) return {};
    std::wstring result(static_cast<std::size_t>(size), L'\0');
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), result.data(), size)) return {};
    return result;
}
std::string narrow(std::wstring_view text) {
    if (text.empty() || text.size() > INT_MAX) return {};
    const int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (size <= 0) return {};
    std::string out(static_cast<std::size_t>(size), '\0');
    if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), out.data(), size, nullptr, nullptr)) return {};
    return out;
}
std::optional<std::string> queryHeaderText(HINTERNET request, DWORD flags) {
    DWORD size = 0;
    WinHttpQueryHeaders(request, flags, WINHTTP_HEADER_NAME_BY_INDEX, nullptr, &size, WINHTTP_NO_HEADER_INDEX);
    for (int retry = 0; retry < 3 && GetLastError() == ERROR_INSUFFICIENT_BUFFER && size; ++retry) {
        std::wstring buffer((size + sizeof(wchar_t) - 1) / sizeof(wchar_t), L'\0');
        if (WinHttpQueryHeaders(request, flags, WINHTTP_HEADER_NAME_BY_INDEX, buffer.data(), &size, WINHTTP_NO_HEADER_INDEX)) {
            buffer.resize(size / sizeof(wchar_t));
            while (!buffer.empty() && buffer.back() == L'\0') buffer.pop_back();
            auto text = narrow(buffer);
            if (!buffer.empty() && text.empty()) return {};
            return text;
        }
    }
    return {};
}
void stripHeaderFraming(std::string& block) {
    while (block.size() >= 2 && block.compare(block.size() - 2, 2, "\r\n") == 0) block.resize(block.size() - 2);
}
ErrorCode mapError(DWORD error) {
    switch (error) {
        case ERROR_WINHTTP_NAME_NOT_RESOLVED: return ErrorCode::transport_dns_failed;
        case ERROR_WINHTTP_CANNOT_CONNECT:
        case ERROR_WINHTTP_CONNECTION_ERROR: return ErrorCode::transport_connect_failed;
        case ERROR_WINHTTP_SECURE_FAILURE:
        case ERROR_WINHTTP_CLIENT_AUTH_CERT_NEEDED: return ErrorCode::transport_tls_failed;
        case ERROR_WINHTTP_TIMEOUT: return ErrorCode::transport_timeout;
        case ERROR_WINHTTP_OPERATION_CANCELLED: return ErrorCode::cancelled;
        default: return ErrorCode::transport_receive_failed;
    }
}
class InternetHandle {
public:
    HINTERNET value = nullptr;
    ~InternetHandle() { reset(); }
    void reset(HINTERNET next = nullptr) { if (value) WinHttpCloseHandle(value); value = next; }
};
struct RequestState {
    HANDLE changed = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HANDLE cancel = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HANDLE closed = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    std::mutex mutex;
    DWORD event = 0, error = 0, read = 0, available = 0;
    std::uint64_t bytes = 0;
    bool cancelled = false, resultChosen = false;
    std::array<char, 64 * 1024> buffer{};
    std::optional<HttpDiagnostic> diagnostic;
    BoundedHttpCapture diagnosticBody;
    std::string diagnosticHeaders;
    bool responseProcessingFailed = false;
    std::optional<int> receivedHttpStatus;
    ~RequestState() {
        if (changed) CloseHandle(changed);
        if (cancel) CloseHandle(cancel);
        if (closed) CloseHandle(closed);
    }
};
void CALLBACK statusCallback(HINTERNET, DWORD_PTR pointer, DWORD status, void* info, DWORD size) {
    if (!pointer) return;
    auto& state = *reinterpret_cast<RequestState*>(pointer);
    // This lock also lets the owner wait for the last callback to return after
    // observing the closing event. No callback invokes an API or waits for I/O.
    std::lock_guard lock(state.mutex);
    if (status == WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING) { SetEvent(state.closed); return; }
    if (status == WINHTTP_CALLBACK_STATUS_READ_COMPLETE) {
        state.read = size;
        const auto maximum = (std::numeric_limits<std::uint64_t>::max)();
        state.bytes = size > maximum - state.bytes ? maximum : state.bytes + size;
    } else if (status == WINHTTP_CALLBACK_STATUS_DATA_AVAILABLE) {
        if (size != sizeof(DWORD) || !info) return;
        state.available = *static_cast<DWORD*>(info);
    } else if (status == WINHTTP_CALLBACK_STATUS_REQUEST_ERROR) {
        state.error = static_cast<WINHTTP_ASYNC_RESULT*>(info)->dwError;
    } else if (status != WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE && status != WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE) {
        return;
    }
    state.event = status;
    SetEvent(state.changed);
}
class WinHttpTransport final : public Transport {
public:
    ~WinHttpTransport() override {
        cancelCurrentRequest();
        std::lock_guard lock(serial_);
        connection_.reset();
        session_.reset();
    }
    Status configure(const TransportSettings& settings) override {
        cancelCurrentRequest();
        std::lock_guard lock(serial_);
        connection_.reset();
        session_.reset();
        if (settings.responseTimeoutSeconds < 1 || settings.responseTimeoutSeconds > kMaxHttpTimeoutSeconds || settings.maxResponseBodyBytes < 1)
            return Error(ErrorCode::config_invalid_limit);
        PHPVPN_TRY_ASSIGN(url_, parseServerUrl(settings.serverUrl));
        PHPVPN_TRY_ASSIGN(normalizedHeaders_, normalizeHttpHeaders(settings.httpHeaders));
        if (widen(normalizedHeaders_).empty()) return Error(ErrorCode::config_encoding_error);
        settings_ = settings;
        session_.reset(WinHttpOpen(nullptr, WINHTTP_ACCESS_TYPE_NO_PROXY, WINHTTP_NO_PROXY_NAME,
                                  WINHTTP_NO_PROXY_BYPASS, WINHTTP_FLAG_ASYNC));
        if (!session_.value) return Error(ErrorCode::transport_unavailable);
        const int timeout = static_cast<int>(settings.responseTimeoutSeconds * 1000);
        if (!WinHttpSetTimeouts(session_.value, timeout, timeout, timeout, timeout)) {
            session_.reset();
            return Error(ErrorCode::transport_unavailable, "HTTP timeout settings failed");
        }
        DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
#ifdef WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3
        protocols |= WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
#endif
        if (!WinHttpSetOption(session_.value, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols))) {
            session_.reset();
            return Error(ErrorCode::transport_tls_failed);
        }
        connection_.reset(WinHttpConnect(session_.value, widen(url_.host).c_str(), url_.port, 0));
        if (!connection_.value) return Error(ErrorCode::transport_connect_failed);
        return ok();
    }
    void cancelCurrentRequest() override {
        cancellationEpoch_.fetch_add(1);
        std::shared_ptr<RequestState> state;
        { std::lock_guard lock(currentMutex_); state = current_; }
        if (state) {
            std::lock_guard lock(state->mutex);
            if (!state->resultChosen) { state->cancelled = true; SetEvent(state->cancel); }
        }
    }
    HttpExchangeResult post(HttpAction action, std::string_view body) override {
        return postImpl(action, body, false);
    }
    HttpExchangeResult postDiagnostic(HttpAction action, std::string_view body) override {
        return postImpl(action, body, true);
    }
    HttpExchangeResult postImpl(HttpAction action, std::string_view body, bool capture) {
        const auto cancellationEpoch = cancellationEpoch_.load();
        std::lock_guard lock(serial_);
        const auto start = GetTickCount64();
        if (!connection_.value) return Error(ErrorCode::transport_unavailable);
        if (body.size() > INT_MAX) return Error(ErrorCode::wire_body_too_large);
        const auto budget = static_cast<ULONGLONG>(settings_.responseTimeoutSeconds) * 1000;
        if (start > (std::numeric_limits<ULONGLONG>::max)() - budget) return Error(ErrorCode::config_invalid_limit);
        const auto deadline = start + budget;
        auto state = std::make_shared<RequestState>();
        if (!state->changed || !state->cancel || !state->closed) return Error(ErrorCode::transport_unavailable);
        const auto target = widen(url_.path) + L"?action=" + widen(httpActionName(action));
        HINTERNET request = WinHttpOpenRequest(connection_.value, L"POST", target.c_str(), nullptr,
            WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, url_.secure ? WINHTTP_FLAG_SECURE : 0);
        if (!request) return Error(ErrorCode::transport_send_failed);
        DWORD_PTR pointer = reinterpret_cast<DWORD_PTR>(state.get());
        if (!WinHttpSetOption(request, WINHTTP_OPTION_CONTEXT_VALUE, &pointer, sizeof(pointer)) ||
            WinHttpSetStatusCallback(request, statusCallback, WINHTTP_CALLBACK_FLAG_ALL_COMPLETIONS | WINHTTP_CALLBACK_FLAG_HANDLES, 0)
                == WINHTTP_INVALID_STATUS_CALLBACK) {
            WinHttpCloseHandle(request);
            return Error(ErrorCode::transport_unavailable);
        }
        {
            std::lock_guard currentLock(currentMutex_);
            current_ = state;
            if (cancellationEpoch != cancellationEpoch_.load()) {
                state->cancelled = true;
                SetEvent(state->cancel);
            }
        }
        auto result = exchange(request, *state, deadline, action, body, capture);
        {
            std::lock_guard stateLock(state->mutex);
            if (state->cancelled) result = Error(ErrorCode::cancelled);
            else if (GetTickCount64() >= deadline) result = Error(ErrorCode::transport_timeout);
            state->resultChosen = true;
        }
        // Exactly one closer. Closing is asynchronous internally, so return only
        // after the documented final callback. The post owner still owns buffers.
        WinHttpCloseHandle(request);
        WaitForSingleObject(state->closed, INFINITE);
        std::uint64_t bytes;
        { std::lock_guard stateLock(state->mutex); bytes = state->bytes; }
        { std::lock_guard currentLock(currentMutex_); current_.reset(); }
        HttpExchangeResult outcome{std::move(result), bytes};
        // The existing final cancellation/deadline decision always wins.
        if (state->diagnostic && (outcome.ok() || (state->responseProcessingFailed &&
            outcome.error().code != ErrorCode::cancelled && outcome.error().code != ErrorCode::transport_timeout))) {
            auto message = prependHttpHeaders(state->diagnosticHeaders, state->diagnosticBody.finish());
            if (message) {
                state->diagnostic->response = std::move(*message);
                outcome.diagnostic = std::move(state->diagnostic);
                outcome.responseProcessingFailed = state->responseProcessingFailed;
                outcome.receivedHttpStatus = state->receivedHttpStatus;
            }
        }
        return outcome;
    }
private:
    Status wait(RequestState& state, ULONGLONG deadline, DWORD expected) {
        for (;;) {
            const auto now = GetTickCount64();
            if (now >= deadline) return Error(ErrorCode::transport_timeout);
            const DWORD remaining = static_cast<DWORD>((std::min)(deadline - now, static_cast<ULONGLONG>(INT_MAX)));
            HANDLE handles[] = {state.cancel, state.changed};
            const auto result = WaitForMultipleObjects(2, handles, FALSE, remaining);
            if (result == WAIT_OBJECT_0) return Error(ErrorCode::cancelled);
            if (result == WAIT_TIMEOUT) continue;
            if (result != WAIT_OBJECT_0 + 1) return Error(ErrorCode::transport_receive_failed);
            std::lock_guard lock(state.mutex);
            if (state.cancelled) return Error(ErrorCode::cancelled);
            if (GetTickCount64() >= deadline) return Error(ErrorCode::transport_timeout);
            if (state.error) return Error(mapError(state.error));
            if (state.event != expected) return Error(ErrorCode::transport_receive_failed);
            state.event = 0;
            ResetEvent(state.changed);
            return ok();
        }
    }
    Result<HttpResponse> exchange(HINTERNET request, RequestState& state, ULONGLONG deadline,
                                   HttpAction action, std::string_view body, bool capture) {
        DWORD disabled = WINHTTP_DISABLE_REDIRECTS | WINHTTP_DISABLE_AUTHENTICATION;
        if (!WinHttpSetOption(request, WINHTTP_OPTION_DISABLE_FEATURE, &disabled, sizeof(disabled)))
            return Error(ErrorCode::transport_unavailable);
        DWORD autoLogon = WINHTTP_AUTOLOGON_SECURITY_LEVEL_HIGH;
        if (!WinHttpSetOption(request, WINHTTP_OPTION_AUTOLOGON_POLICY, &autoLogon, sizeof(autoLogon)))
            return Error(ErrorCode::transport_unavailable);
        if (url_.secure && settings_.trustAnyTlsCertificate) {
            DWORD flags = SECURITY_FLAG_IGNORE_UNKNOWN_CA | SECURITY_FLAG_IGNORE_CERT_DATE_INVALID |
                SECURITY_FLAG_IGNORE_CERT_CN_INVALID | SECURITY_FLAG_IGNORE_CERT_WRONG_USAGE;
            if (!WinHttpSetOption(request, WINHTTP_OPTION_SECURITY_FLAGS, &flags, sizeof(flags))) return Error(ErrorCode::transport_tls_failed);
        }
        const auto applicationHeaders = buildApplicationRequestHeaders(httpActionContentType(action), normalizedHeaders_);
        // WinHTTP encodes Unicode header values as UTF-8 on the wire. Decode the
        // configured UTF-8 once, never widen its individual octets or use ACP.
        const auto headers = widen(applicationHeaders);
        if (headers.empty()) return Error(ErrorCode::config_encoding_error);
        {
            std::lock_guard lock(state.mutex);
            if (state.cancelled) return Error(ErrorCode::cancelled);
        }
        if (GetTickCount64() >= deadline) return Error(ErrorCode::transport_timeout);
        if (!WinHttpSendRequest(request, headers.c_str(), static_cast<DWORD>(headers.size()),
            const_cast<char*>(body.data()), static_cast<DWORD>(body.size()), static_cast<DWORD>(body.size()), reinterpret_cast<DWORD_PTR>(&state)))
            return Error(mapError(GetLastError()));
        PHPVPN_TRY(wait(state, deadline, WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE));
        if (!WinHttpReceiveResponse(request, nullptr)) return Error(mapError(GetLastError()));
        PHPVPN_TRY(wait(state, deadline, WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE));
        if ((capture || settings_.captureWizardHttpDiagnostics) && action != HttpAction::data) {
            state.diagnostic.emplace();
            auto& diagnostic = *state.diagnostic;
            diagnostic.action = action;
            auto rawRequest = queryHeaderText(request, WINHTTP_QUERY_RAW_HEADERS_CRLF | WINHTTP_QUERY_FLAG_REQUEST_HEADERS);
            const auto requestHeaders = reconstructDiagnosticRequestHeaders(std::move(rawRequest), applicationHeaders);
            diagnostic.requestHeadersSource = requestHeaders.source;
            const auto schemeEnd = url_.original.find("://") + 3;
            const auto authorityEnd = url_.original.find('/', schemeEnd);
            BoundedHttpCapture requestCapture;
            requestCapture.append("POST ");
            requestCapture.append(std::string_view(url_.original).substr(0, authorityEnd));
            requestCapture.append(url_.path);
            requestCapture.append(std::string("?action=") + httpActionName(action) + " " + requestHeaders.version + "\r\n");
            requestCapture.append(requestHeaders.block);
            requestCapture.append("\r\n\r\n");
            requestCapture.append(body);
            diagnostic.request = requestCapture.finish();
            auto rawResponse = queryHeaderText(request, WINHTTP_QUERY_RAW_HEADERS_CRLF);
            std::optional<unsigned> diagnosticStatus;
            std::vector<std::pair<std::string, std::string>> availableHeaders;
            if (!rawResponse || !rawResponse->starts_with("HTTP/")) {
                DWORD code = 0, codeSize = sizeof(code);
                const bool gotStatus = WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                    WINHTTP_HEADER_NAME_BY_INDEX, &code, &codeSize, WINHTTP_NO_HEADER_INDEX) != FALSE;
                if (gotStatus) diagnosticStatus = code;
                for (const auto& entry : {std::pair<DWORD, const char*>{WINHTTP_QUERY_CONTENT_TYPE, "Content-Type"},
                    {WINHTTP_QUERY_CONTENT_LENGTH, "Content-Length"}, {WINHTTP_QUERY_SERVER, "Server"}, {WINHTTP_QUERY_DATE, "Date"}}) {
                    if (auto value = queryHeaderText(request, entry.first)) {
                        availableHeaders.emplace_back(entry.second, std::move(*value));
                    }
                }
            }
            auto responseHeaders = reconstructDiagnosticResponseHeaders(std::move(rawResponse), diagnosticStatus, availableHeaders);
            diagnostic.responseHeadersSource = responseHeaders.source;
            state.diagnosticHeaders = std::move(responseHeaders.block);
        }
        // Only local processing of received headers/body is diagnostic failure.
        state.responseProcessingFailed = true;
        DWORD status = 0, size = sizeof(status);
        if (!WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX)) return Error(ErrorCode::transport_receive_failed);
        HttpResponse response;
        response.statusCode = static_cast<int>(status);
        state.receivedHttpStatus = response.statusCode;
        wchar_t contentType[256]{};
        size = sizeof(contentType);
        if (WinHttpQueryHeaders(request, WINHTTP_QUERY_CONTENT_TYPE, WINHTTP_HEADER_NAME_BY_INDEX,
            contentType, &size, WINHTTP_NO_HEADER_INDEX)) {
            for (const wchar_t* character = contentType; *character; ++character) {
                if (*character > 127) return Error(ErrorCode::transport_receive_failed);
                response.contentType.push_back(static_cast<char>(*character));
            }
        } else if (GetLastError() != ERROR_WINHTTP_HEADER_NOT_FOUND) return Error(ErrorCode::transport_receive_failed);
        std::optional<std::uint64_t> length;
        wchar_t text[32]{};
        size = sizeof(text);
        if (WinHttpQueryHeaders(request, WINHTTP_QUERY_CONTENT_LENGTH, WINHTTP_HEADER_NAME_BY_INDEX, text, &size, WINHTTP_NO_HEADER_INDEX)) {
            std::uint64_t parsed = 0;
            for (const wchar_t* digit = text; *digit; ++digit) {
                if (*digit < L'0' || *digit > L'9' || parsed > ((std::numeric_limits<std::uint64_t>::max)() - (*digit - L'0')) / 10)
                    return Error(ErrorCode::transport_receive_failed);
                parsed = parsed * 10 + (*digit - L'0');
            }
            length = parsed;
        } else if (GetLastError() != ERROR_WINHTTP_HEADER_NOT_FOUND) return Error(ErrorCode::transport_receive_failed);
        state.responseProcessingFailed = false;
        const auto eof = [&]() -> Result<HttpResponse> {
            if (length && response.body.size() != *length) {
                state.responseProcessingFailed = true;
                return Error(ErrorCode::transport_receive_failed, "the response body is incomplete");
            }
            if (state.diagnostic) {
                state.diagnostic->responseComplete = true;
#ifdef WINHTTP_QUERY_FLAG_TRAILERS
                if (auto trailers = queryHeaderText(request, WINHTTP_QUERY_RAW_HEADERS_CRLF | WINHTTP_QUERY_FLAG_TRAILERS); trailers && !trailers->empty()) {
                    stripHeaderFraming(*trailers);
                    if (!trailers->empty()) {
                        stripHeaderFraming(state.diagnosticHeaders);
                        state.diagnosticHeaders += "\r\n[Response trailers]\r\n" + *trailers + "\r\n\r\n";
                    }
                }
#endif
            }
            return std::move(response);
        };
        for (;;) {
            if (!WinHttpQueryDataAvailable(request, nullptr)) return Error(mapError(GetLastError()));
            PHPVPN_TRY(wait(state, deadline, WINHTTP_CALLBACK_STATUS_DATA_AVAILABLE));
            DWORD available;
            { std::lock_guard lock(state.mutex); available = state.available; }
            if (!available) {
                return eof();
            }
            const auto requested = (std::min)(available, static_cast<DWORD>(state.buffer.size()));
            if (!WinHttpReadData(request, state.buffer.data(), requested, nullptr))
                return Error(mapError(GetLastError()));
            PHPVPN_TRY(wait(state, deadline, WINHTTP_CALLBACK_STATUS_READ_COMPLETE));
            DWORD read;
            { std::lock_guard lock(state.mutex); read = state.read; }
            if (!read) {
                return eof();
            }
            if (state.diagnostic) state.diagnosticBody.append(std::string_view(state.buffer.data(), read));
            if (read > static_cast<std::uint64_t>(settings_.maxResponseBodyBytes) - response.body.size()) {
                state.responseProcessingFailed = true;
                return Error(ErrorCode::transport_response_too_large);
            }
            response.body.append(state.buffer.data(), read);
        }
    }
    std::mutex serial_, currentMutex_;
    InternetHandle session_, connection_;
    std::shared_ptr<RequestState> current_;
    std::atomic<std::uint64_t> cancellationEpoch_{0};
    ServerUrl url_;
    TransportSettings settings_;
    std::string normalizedHeaders_;
};
}
Result<std::unique_ptr<Transport>> createPlatformTransport() { return std::unique_ptr<Transport>(new WinHttpTransport); }
}
