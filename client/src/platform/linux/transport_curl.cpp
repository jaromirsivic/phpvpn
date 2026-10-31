// libcurl transport, plan-v001 section 4.5. The behaviour mirrors
// platform/windows/transport_winhttp.cpp: one absolute monotonic deadline for
// the whole exchange, the response body limit, read accounting that survives
// every failure, the optional Wizard header capture and prompt cancellation.
//
// Only post() drives curl. It runs one transfer at a time on the persistent easy
// handle inside the transport's private multi handle; the multi handle owns the
// connection pool, so a kept-alive connection is reused by the next request.
// cancelCurrentRequest() only flags the request and wakes curl_multi_poll. The
// owner then removes the easy handle, which aborts DNS resolution, the TCP
// connect, the TLS handshake or a slow read at once. Every request starts from
// curl_easy_reset, so a cancelled or timed out transfer leaves no state behind.
#include <curl/curl.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <climits>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "core/base/utf8.h"
#include "core/config/config.h"
#include "core/config/config_document.h"
#include "core/protocol/http_headers.h"
#include "platform/iface/transport.h"
#if defined(__ANDROID__)
#include "platform/android/tls_trust_android.h"
#endif

namespace phpvpn {
namespace {

using SteadyClock = std::chrono::steady_clock;

// System CA trust of a verifying request. Linux and macOS keep the default CA
// locations of their curl and OpenSSL builds, so the hook is empty there.
// Android has no CA bundle OpenSSL can read and installs the system store
// (plan-android-v001 point 10). Never called with trustAnyTlsCertificate.
#if defined(__ANDROID__)
Status configureTlsTrust(CURL* easy) { return installAndroidSystemTrust(easy); }
#else
Status configureTlsTrust(CURL*) { return ok(); }
#endif

bool initializeCurl() {
    static std::once_flag once;
    static CURLcode code = CURLE_FAILED_INIT;
    std::call_once(once, [] { code = curl_global_init(CURL_GLOBAL_DEFAULT); });
    return code == CURLE_OK;
}

struct EasyDeleter {
    void operator()(CURL* handle) const noexcept { curl_easy_cleanup(handle); }
};
struct MultiDeleter {
    void operator()(CURLM* handle) const noexcept { curl_multi_cleanup(handle); }
};
struct ListDeleter {
    void operator()(curl_slist* list) const noexcept { curl_slist_free_all(list); }
};
using EasyHandle = std::unique_ptr<CURL, EasyDeleter>;
using MultiHandle = std::unique_ptr<CURLM, MultiDeleter>;
using HeaderList = std::unique_ptr<curl_slist, ListDeleter>;

// Same ErrorCode set as the WinHTTP mapping. A reset or terminated connection is
// ERROR_WINHTTP_CONNECTION_ERROR there, hence connect_failed for send/recv.
ErrorCode mapError(CURLcode code) {
    switch (code) {
        case CURLE_COULDNT_RESOLVE_HOST: return ErrorCode::transport_dns_failed;
        case CURLE_COULDNT_CONNECT:
        case CURLE_SEND_ERROR:
        case CURLE_RECV_ERROR: return ErrorCode::transport_connect_failed;
        case CURLE_SSL_CONNECT_ERROR:
        case CURLE_PEER_FAILED_VERIFICATION:
        case CURLE_SSL_CERTPROBLEM:
        case CURLE_SSL_CIPHER:
        case CURLE_SSL_CACERT_BADFILE:
        case CURLE_SSL_CRL_BADFILE:
        case CURLE_SSL_ISSUER_ERROR:
        case CURLE_SSL_INVALIDCERTSTATUS:
        case CURLE_SSL_PINNEDPUBKEYNOTMATCH:
        case CURLE_SSL_CLIENTCERT:
        case CURLE_SSL_ENGINE_NOTFOUND:
        case CURLE_SSL_ENGINE_SETFAILED:
        case CURLE_SSL_ENGINE_INITFAILED:
        case CURLE_USE_SSL_FAILED: return ErrorCode::transport_tls_failed;
        case CURLE_OPERATION_TIMEDOUT: return ErrorCode::transport_timeout;
        case CURLE_ABORTED_BY_CALLBACK: return ErrorCode::cancelled;
        default: return ErrorCode::transport_receive_failed;
    }
}

bool equalsIgnoreCase(std::string_view left, std::string_view right) {
    if (left.size() != right.size()) return false;
    for (std::size_t i = 0; i < left.size(); ++i) {
        const auto lower = [](char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c + ('a' - 'A')) : c; };
        if (lower(left[i]) != lower(right[i])) return false;
    }
    return true;
}

std::string_view trimWhitespace(std::string_view text) {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) text.remove_prefix(1);
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r' || text.back() == '\n'))
        text.remove_suffix(1);
    return text;
}

// First value of a response header in a raw block (status line included), like
// WinHttpQueryHeaders with the default index.
std::optional<std::string_view> firstHeaderValue(std::string_view block, std::string_view name) {
    for (std::size_t start = 0; start < block.size();) {
        auto end = block.find('\n', start);
        if (end == std::string_view::npos) end = block.size();
        const auto line = block.substr(start, end - start);
        const auto colon = line.find(':');
        if (colon != std::string_view::npos && equalsIgnoreCase(line.substr(0, colon), name))
            return trimWhitespace(line.substr(colon + 1));
        start = end + 1;
    }
    return std::nullopt;
}

void stripHeaderFraming(std::string& block) {
    while (block.size() >= 2 && block.compare(block.size() - 2, 2, "\r\n") == 0) block.resize(block.size() - 2);
}

// curl request headers from the application block. A field with an empty value
// needs the `Name;` spelling, `Name:` would remove it. WinHTTP sends
// `Connection: Keep-Alive`, no Accept, no Expect and a User-Agent only when the
// block contains one; libcurl adds Host and Content-Length itself.
std::optional<HeaderList> buildHeaderList(std::string_view applicationHeaders) {
    HeaderList list;
    const auto append = [&list](const std::string& line) {
        curl_slist* next = curl_slist_append(list.get(), line.c_str());
        if (!next) return false;
        static_cast<void>(list.release());
        list.reset(next);
        return true;
    };
    bool accept = false, expect = false;
    for (std::size_t start = 0; start < applicationHeaders.size();) {
        auto end = applicationHeaders.find("\r\n", start);
        if (end == std::string_view::npos) end = applicationHeaders.size();
        const auto line = applicationHeaders.substr(start, end - start);
        start = end + 2;
        const auto colon = line.find(':');
        if (colon == std::string_view::npos) continue;
        const auto name = line.substr(0, colon);
        accept = accept || equalsIgnoreCase(name, "Accept");
        expect = expect || equalsIgnoreCase(name, "Expect");
        const auto value = line.substr(colon + 1);
        const bool empty = value.find_first_not_of(" \t") == std::string_view::npos;
        if (!append(empty ? std::string(name) + ";" : std::string(line))) return std::nullopt;
    }
    if (!append("Connection: Keep-Alive")) return std::nullopt;
    if (!accept && !append("Accept:")) return std::nullopt;
    if (!expect && !append("Expect:")) return std::nullopt;
    return list;
}

struct RequestState {
    CURL* easy = nullptr;
    CURLM* multi = nullptr;
    // The only field shared with cancelCurrentRequest(), under currentMutex_.
    bool cancelled = false;

    // Everything below is touched by the post() thread only, curl callbacks
    // included, because curl_multi_perform runs them on that thread.
    HttpAction action = HttpAction::health;
    std::string_view body;
    const ServerUrl* url = nullptr;
    std::string_view applicationHeaders;
    bool capture = false;
    std::uint64_t limit = 0;

    std::uint64_t bytes = 0;
    HttpResponse response;
    std::optional<std::uint64_t> length;
    std::optional<Error> callbackError;
    bool headersComplete = false;
    std::string sentHeaders, receivedHeaders, trailers;
    std::optional<HttpDiagnostic> diagnostic;
    BoundedHttpCapture diagnosticBody;
    std::string diagnosticHeaders;
    bool responseProcessingFailed = false;
    std::optional<int> receivedHttpStatus;
};

// The part of the WinHTTP exchange between HEADERS_AVAILABLE and the first read.
Status processHeaders(RequestState& state) {
    if (state.capture) {
        state.diagnostic.emplace();
        auto& diagnostic = *state.diagnostic;
        diagnostic.action = state.action;
        std::optional<std::string> rawRequest;
        if (!state.sentHeaders.empty()) rawRequest = state.sentHeaders;
        const auto requestHeaders = reconstructDiagnosticRequestHeaders(std::move(rawRequest), state.applicationHeaders);
        diagnostic.requestHeadersSource = requestHeaders.source;
        const auto& original = state.url->original;
        const auto schemeEnd = original.find("://") + 3;
        const auto authorityEnd = original.find('/', schemeEnd);
        BoundedHttpCapture requestCapture;
        requestCapture.append("POST ");
        requestCapture.append(std::string_view(original).substr(0, authorityEnd));
        requestCapture.append(state.url->path);
        requestCapture.append(std::string("?action=") + httpActionName(state.action) + " " + requestHeaders.version + "\r\n");
        requestCapture.append(requestHeaders.block);
        requestCapture.append("\r\n\r\n");
        requestCapture.append(state.body);
        diagnostic.request = requestCapture.finish();
        auto responseHeaders = reconstructDiagnosticResponseHeaders(state.receivedHeaders, std::nullopt, {});
        diagnostic.responseHeadersSource = responseHeaders.source;
        state.diagnosticHeaders = std::move(responseHeaders.block);
    }
    // Only local processing of received headers/body is diagnostic failure.
    state.responseProcessingFailed = true;
    long status = 0;
    if (curl_easy_getinfo(state.easy, CURLINFO_RESPONSE_CODE, &status) != CURLE_OK || status <= 0 || status > INT_MAX)
        return Error(ErrorCode::transport_receive_failed);
    state.response.statusCode = static_cast<int>(status);
    state.receivedHttpStatus = state.response.statusCode;
    if (const auto contentType = firstHeaderValue(state.receivedHeaders, "Content-Type")) {
        // WinHTTP queries it into a 256 character buffer and accepts ASCII only.
        if (contentType->size() > 255) return Error(ErrorCode::transport_receive_failed);
        for (const char character : *contentType) {
            if (static_cast<unsigned char>(character) > 127) return Error(ErrorCode::transport_receive_failed);
        }
        state.response.contentType.assign(*contentType);
    }
    if (const auto text = firstHeaderValue(state.receivedHeaders, "Content-Length")) {
        if (text->size() > 31) return Error(ErrorCode::transport_receive_failed);
        std::uint64_t parsed = 0;
        for (const char digit : *text) {
            if (digit < '0' || digit > '9' ||
                parsed > ((std::numeric_limits<std::uint64_t>::max)() - static_cast<unsigned>(digit - '0')) / 10)
                return Error(ErrorCode::transport_receive_failed);
            parsed = parsed * 10 + static_cast<unsigned>(digit - '0');
        }
        state.length = parsed;
    }
    state.responseProcessingFailed = false;
    return ok();
}

std::size_t onHeader(char* data, std::size_t size, std::size_t count, void* pointer) {
    auto& state = *static_cast<RequestState*>(pointer);
    const std::size_t total = size * count;
    const std::string_view line(data, total);
    const bool blank = line == "\r\n" || line == "\n";
    if (state.headersComplete) {
        // Everything after the final header block are chunked trailers.
        if (!blank) state.trailers.append(line);
        return total;
    }
    if (line.starts_with("HTTP/")) {
        state.receivedHeaders.assign(line);
        return total;
    }
    if (!blank) {
        state.receivedHeaders.append(line);
        return total;
    }
    state.receivedHeaders.append(line);
    // Interim 1xx responses are consumed like WinHTTP does, the final block counts.
    long status = 0;
    if (curl_easy_getinfo(state.easy, CURLINFO_RESPONSE_CODE, &status) == CURLE_OK && status >= 100 && status < 200)
        return total;
    state.headersComplete = true;
    if (auto processed = processHeaders(state); !processed.ok()) {
        state.callbackError = processed.error();
        return 0;
    }
    return total;
}

std::size_t onBody(char* data, std::size_t size, std::size_t count, void* pointer) {
    auto& state = *static_cast<RequestState*>(pointer);
    const std::size_t total = size * count;
    const auto maximum = (std::numeric_limits<std::uint64_t>::max)();
    state.bytes = total > maximum - state.bytes ? maximum : state.bytes + total;
    if (state.diagnostic) state.diagnosticBody.append(std::string_view(data, total));
    if (total > state.limit - state.response.body.size()) {
        state.responseProcessingFailed = true;
        state.callbackError = Error(ErrorCode::transport_response_too_large);
        return 0;
    }
    state.response.body.append(data, total);
    return total;
}

int onDebug(CURL*, curl_infotype type, char* data, std::size_t size, void* pointer) {
    if (type != CURLINFO_HEADER_OUT) return 0;
    auto& state = *static_cast<RequestState*>(pointer);
    const std::string_view text(data, size);
    // A retry on a stale pooled connection sends the request line again.
    if (text.starts_with("POST ")) state.sentHeaders.assign(text);
    else state.sentHeaders.append(text);
    return 0;
}

class CurlTransport final : public Transport {
public:
    ~CurlTransport() override {
        cancelCurrentRequest();
        std::lock_guard lock(serial_);
        easy_.reset();
        multi_.reset();
    }
    Status configure(const TransportSettings& settings) override {
        cancelCurrentRequest();
        std::lock_guard lock(serial_);
        // Like closing the WinHTTP session: pooled connections go with it.
        easy_.reset();
        multi_.reset();
        if (settings.responseTimeoutSeconds < 1 || settings.responseTimeoutSeconds > kMaxHttpTimeoutSeconds || settings.maxResponseBodyBytes < 1)
            return Error(ErrorCode::config_invalid_limit);
        PHPVPN_TRY_ASSIGN(url_, parseServerUrl(settings.serverUrl));
        PHPVPN_TRY_ASSIGN(normalizedHeaders_, normalizeHttpHeaders(settings.httpHeaders));
        // The header values go to the wire as configured UTF-8, which must be valid.
        if (!utf8ToUtf32(normalizedHeaders_).ok()) return Error(ErrorCode::config_encoding_error);
        settings_ = settings;
        // The host is canonical lower case, path and query go verbatim (PATH_AS_IS).
        requestUrl_ = std::string(url_.secure ? "https://" : "http://") + url_.host + ":" + std::to_string(url_.port) + url_.path;
        multi_.reset(curl_multi_init());
        easy_.reset(curl_easy_init());
        if (!multi_ || !easy_) {
            easy_.reset();
            multi_.reset();
            return Error(ErrorCode::transport_unavailable);
        }
        return ok();
    }
    void cancelCurrentRequest() override {
        cancellationEpoch_.fetch_add(1);
        std::lock_guard lock(currentMutex_);
        if (current_ && !current_->cancelled) {
            current_->cancelled = true;
            curl_multi_wakeup(current_->multi);
        }
    }
    HttpExchangeResult post(HttpAction action, std::string_view body) override {
        return postImpl(action, body, false);
    }
    HttpExchangeResult postDiagnostic(HttpAction action, std::string_view body) override {
        return postImpl(action, body, true);
    }

private:
    HttpExchangeResult postImpl(HttpAction action, std::string_view body, bool capture) {
        const auto cancellationEpoch = cancellationEpoch_.load();
        std::lock_guard lock(serial_);
        const auto start = SteadyClock::now();
        if (!easy_ || !multi_) return Error(ErrorCode::transport_unavailable);
        if (body.size() > INT_MAX) return Error(ErrorCode::wire_body_too_large);
        const auto deadline = start + std::chrono::seconds(settings_.responseTimeoutSeconds);
        RequestState state;
        state.easy = easy_.get();
        state.multi = multi_.get();
        state.action = action;
        state.body = body;
        state.url = &url_;
        state.capture = (capture || settings_.captureWizardHttpDiagnostics) && action != HttpAction::data;
        state.limit = static_cast<std::uint64_t>(settings_.maxResponseBodyBytes);
        const auto applicationHeaders = buildApplicationRequestHeaders(httpActionContentType(action), normalizedHeaders_);
        state.applicationHeaders = applicationHeaders;
        auto headers = buildHeaderList(applicationHeaders);
        if (!headers) return Error(ErrorCode::transport_unavailable);
        const auto target = requestUrl_ + "?action=" + httpActionName(action);
        if (auto prepared = prepare(state, target, headers->get(), deadline - start); !prepared.ok()) return prepared.error();
        {
            std::lock_guard currentLock(currentMutex_);
            current_ = &state;
            if (cancellationEpoch != cancellationEpoch_.load()) state.cancelled = true;
        }
        auto result = exchange(state, deadline);
        {
            // The final cancellation/deadline decision always wins, a late
            // success after the deadline is never returned.
            std::lock_guard currentLock(currentMutex_);
            if (state.cancelled) result = Error(ErrorCode::cancelled);
            else if (SteadyClock::now() >= deadline) result = Error(ErrorCode::transport_timeout);
            current_ = nullptr;
        }
        HttpExchangeResult outcome{std::move(result), state.bytes};
        if (state.diagnostic && (outcome.ok() || (state.responseProcessingFailed &&
            outcome.error().code != ErrorCode::cancelled && outcome.error().code != ErrorCode::transport_timeout))) {
            auto message = prependHttpHeaders(state.diagnosticHeaders, state.diagnosticBody.finish());
            if (message) {
                state.diagnostic->response = std::move(*message);
                outcome.diagnostic = std::move(state.diagnostic);
                outcome.responseProcessingFailed = state.responseProcessingFailed;
                outcome.receivedHttpStatus = state.receivedHttpStatus;
            }
        }
        return outcome;
    }

    Status prepare(RequestState& state, const std::string& target, curl_slist* headers, SteadyClock::duration budget) {
        CURL* easy = state.easy;
        curl_easy_reset(easy);
        const auto budgetMs = std::clamp<long long>(std::chrono::ceil<std::chrono::milliseconds>(budget).count(), 1, INT_MAX);
        // An empty body still needs a valid pointer, otherwise curl reads from stdin.
        const char* body = state.body.empty() ? "" : state.body.data();
        const CURLcode codes[] = {
            curl_easy_setopt(easy, CURLOPT_URL, target.c_str()),
            curl_easy_setopt(easy, CURLOPT_PROTOCOLS_STR, "http,https"),
            curl_easy_setopt(easy, CURLOPT_PATH_AS_IS, 1L),
            curl_easy_setopt(easy, CURLOPT_HTTP_VERSION, static_cast<long>(CURL_HTTP_VERSION_1_1)),
            curl_easy_setopt(easy, CURLOPT_FOLLOWLOCATION, 0L),
            // An explicit empty proxy also ignores http_proxy and friends.
            curl_easy_setopt(easy, CURLOPT_PROXY, ""),
            curl_easy_setopt(easy, CURLOPT_NOPROXY, "*"),
            curl_easy_setopt(easy, CURLOPT_NETRC, static_cast<long>(CURL_NETRC_IGNORED)),
            curl_easy_setopt(easy, CURLOPT_HTTPAUTH, static_cast<long>(CURLAUTH_NONE)),
            curl_easy_setopt(easy, CURLOPT_NOSIGNAL, 1L),
            // Removing the handle must not wait for a blocked resolver thread.
            curl_easy_setopt(easy, CURLOPT_QUICK_EXIT, 1L),
            curl_easy_setopt(easy, CURLOPT_TIMEOUT_MS, static_cast<long>(budgetMs)),
            curl_easy_setopt(easy, CURLOPT_CONNECTTIMEOUT_MS, static_cast<long>(budgetMs)),
            curl_easy_setopt(easy, CURLOPT_SSL_VERIFYPEER, settings_.trustAnyTlsCertificate ? 0L : 1L),
            curl_easy_setopt(easy, CURLOPT_SSL_VERIFYHOST, settings_.trustAnyTlsCertificate ? 0L : 2L),
            curl_easy_setopt(easy, CURLOPT_HTTPHEADER, headers),
            curl_easy_setopt(easy, CURLOPT_POSTFIELDSIZE_LARGE, static_cast<curl_off_t>(state.body.size())),
            curl_easy_setopt(easy, CURLOPT_POSTFIELDS, body),
            curl_easy_setopt(easy, CURLOPT_HEADERFUNCTION, onHeader),
            curl_easy_setopt(easy, CURLOPT_HEADERDATA, &state),
            curl_easy_setopt(easy, CURLOPT_WRITEFUNCTION, onBody),
            curl_easy_setopt(easy, CURLOPT_WRITEDATA, &state),
        };
        for (const auto code : codes) {
            if (code != CURLE_OK) return Error(ErrorCode::transport_unavailable, "HTTP client option failed");
        }
        if (curl_easy_setopt(easy, CURLOPT_SSLVERSION, static_cast<long>(CURL_SSLVERSION_TLSv1_2)) != CURLE_OK)
            return Error(ErrorCode::transport_tls_failed);
        if (!settings_.trustAnyTlsCertificate) {
            if (auto trusted = configureTlsTrust(easy); !trusted.ok()) return trusted;
        }
        if (state.capture) {
            // The debug callback replaces curl's own verbose output entirely.
            if (curl_easy_setopt(easy, CURLOPT_DEBUGFUNCTION, onDebug) != CURLE_OK ||
                curl_easy_setopt(easy, CURLOPT_DEBUGDATA, &state) != CURLE_OK ||
                curl_easy_setopt(easy, CURLOPT_VERBOSE, 1L) != CURLE_OK)
                return Error(ErrorCode::transport_unavailable, "HTTP client option failed");
        }
        return ok();
    }

    bool cancelled(RequestState& state) {
        std::lock_guard lock(currentMutex_);
        return state.cancelled;
    }

    Result<HttpResponse> exchange(RequestState& state, SteadyClock::time_point deadline) {
        if (curl_multi_add_handle(state.multi, state.easy) != CURLM_OK) return Error(ErrorCode::transport_unavailable);
        Result<HttpResponse> result = Error(ErrorCode::transport_receive_failed);
        for (;;) {
            if (cancelled(state)) { result = Error(ErrorCode::cancelled); break; }
            auto now = SteadyClock::now();
            if (now >= deadline) { result = Error(ErrorCode::transport_timeout); break; }
            int running = 0;
            if (curl_multi_perform(state.multi, &running) != CURLM_OK) break;
            std::optional<CURLcode> done;
            int queued = 0;
            while (CURLMsg* message = curl_multi_info_read(state.multi, &queued)) {
                if (message->msg == CURLMSG_DONE && message->easy_handle == state.easy) done = message->data.result;
            }
            if (done) { result = finish(state, *done); break; }
            now = SteadyClock::now();
            if (now >= deadline) continue;
            const auto remaining = std::clamp<long long>(
                std::chrono::ceil<std::chrono::milliseconds>(deadline - now).count(), 1, INT_MAX);
            // Returns on socket activity, curl's own timers, the deadline or a wakeup.
            if (curl_multi_poll(state.multi, nullptr, 0, static_cast<int>(remaining), nullptr) != CURLM_OK) break;
        }
        // Aborts whatever phase is still running and closes an unfinished connection.
        curl_multi_remove_handle(state.multi, state.easy);
        return result;
    }

    Result<HttpResponse> finish(RequestState& state, CURLcode code) {
        if (code == CURLE_WRITE_ERROR && state.callbackError) return *state.callbackError;
        if (code == CURLE_PARTIAL_FILE) {
            // The connection ended before Content-Length, WinHTTP's eof() case.
            state.responseProcessingFailed = true;
            return Error(ErrorCode::transport_receive_failed, "the response body is incomplete");
        }
        if (code != CURLE_OK) return Error(mapError(code));
        if (!state.headersComplete) return Error(ErrorCode::transport_receive_failed);
        if (state.length && state.response.body.size() != *state.length) {
            state.responseProcessingFailed = true;
            return Error(ErrorCode::transport_receive_failed, "the response body is incomplete");
        }
        if (state.diagnostic) {
            state.diagnostic->responseComplete = true;
            std::string trailers = std::move(state.trailers);
            stripHeaderFraming(trailers);
            if (!trailers.empty()) {
                stripHeaderFraming(state.diagnosticHeaders);
                state.diagnosticHeaders += "\r\n[Response trailers]\r\n" + trailers + "\r\n\r\n";
            }
        }
        return std::move(state.response);
    }

    std::mutex serial_, currentMutex_;
    // Declared before the easy handle, so the easy handle is cleaned up first.
    MultiHandle multi_;
    EasyHandle easy_;
    RequestState* current_ = nullptr;
    std::atomic<std::uint64_t> cancellationEpoch_{0};
    ServerUrl url_;
    TransportSettings settings_;
    std::string normalizedHeaders_;
    std::string requestUrl_;
};

}  // namespace

Result<std::unique_ptr<Transport>> createPlatformTransport() {
    if (!initializeCurl()) return Error(ErrorCode::transport_unavailable, "libcurl initialization failed");
    return std::unique_ptr<Transport>(new CurlTransport);
}

}  // namespace phpvpn
