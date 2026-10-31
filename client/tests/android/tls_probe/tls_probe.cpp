// T-N04 shell-level spike (plan-android-v001, WP01): can the static OpenSSL
// and curl from tools/android/build-deps.sh verify public HTTPS servers with
// the Android system CA store? Run from adb shell:
//
//   tls_probe [url...]      default: https://www.google.com/ https://curl.se/
//
// The CA directories name files by the OLD OpenSSL subject hash
// (subject_hash_old), so X509_STORE_load_locations with a CApath never
// matches. Every file is read with PEM_read_X509 into one X509_STORE instead,
// which curl gets through CURLOPT_SSL_CTX_FUNCTION, the same way the Android
// platform layer will (WP03). Not part of the client build.
#include <curl/curl.h>
#include <openssl/crypto.h>
#include <openssl/err.h>
#include <openssl/pem.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>

#include <dirent.h>
#include <sys/stat.h>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

struct DirResult {
    bool opened = false;
    int openErrno = 0;
    int files = 0;
    int certs = 0;
    int unreadable = 0;
    int notPem = 0;
};

DirResult loadDirectory(const char* path, X509_STORE* store) {
    DirResult result;
    DIR* dir = opendir(path);
    if (dir == nullptr) {
        result.openErrno = errno;
        return result;
    }
    result.opened = true;
    while (dirent* entry = readdir(dir)) {
        if (entry->d_name[0] == '.') {
            continue;
        }
        const std::string file = std::string(path) + "/" + entry->d_name;
        struct stat st {};
        if (stat(file.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) {
            continue;
        }
        ++result.files;
        FILE* fp = std::fopen(file.c_str(), "r");
        if (fp == nullptr) {
            ++result.unreadable;
            continue;
        }
        // Each file holds one PEM certificate followed by a text dump.
        X509* cert = PEM_read_X509(fp, nullptr, nullptr, nullptr);
        std::fclose(fp);
        if (cert == nullptr) {
            ++result.notPem;
            ERR_clear_error();
            continue;
        }
        // Duplicates (same certificate in both sources) are not an error.
        if (X509_STORE_add_cert(store, cert) == 1) {
            ++result.certs;
        } else {
            ERR_clear_error();
        }
        X509_free(cert);
    }
    closedir(dir);
    return result;
}

void printDirectory(const char* path, const DirResult& r) {
    if (!r.opened) {
        std::printf("ca-dir %s: opendir failed errno=%d (%s)\n", path, r.openErrno, std::strerror(r.openErrno));
        return;
    }
    std::printf("ca-dir %s: files=%d certs=%d unreadable=%d not-pem=%d\n", path, r.files, r.certs, r.unreadable,
                r.notPem);
}

struct Transfer {
    std::string tlsVersion;
    std::string cipher;
    size_t bodyBytes = 0;
};

size_t onBody(char*, size_t size, size_t count, void* user) {
    static_cast<Transfer*>(user)->bodyBytes += size * count;
    return size * count;
}

void captureTls(CURL* easy, Transfer& t) {
    if (!t.tlsVersion.empty()) {
        return;
    }
    curl_tlssessioninfo* info = nullptr;
    if (curl_easy_getinfo(easy, CURLINFO_TLS_SSL_PTR, &info) == CURLE_OK && info != nullptr &&
        info->backend == CURLSSLBACKEND_OPENSSL && info->internals != nullptr) {
        auto* ssl = static_cast<SSL*>(info->internals);
        t.tlsVersion = SSL_get_version(ssl);
        t.cipher = SSL_get_cipher_name(ssl);
    }
}

// The SSL pointer is only guaranteed during the transfer, so the negotiated
// version is read from the first header callback.
struct HeaderCtx {
    CURL* easy;
    Transfer* t;
};

size_t onHeaderCapture(char*, size_t size, size_t count, void* user) {
    auto* ctx = static_cast<HeaderCtx*>(user);
    captureTls(ctx->easy, *ctx->t);
    return size * count;
}

CURLcode onSslCtx(CURL*, void* sslCtx, void* user) {
    auto* store = static_cast<X509_STORE*>(user);
    // set1 takes its own reference, so one store serves every handle.
    SSL_CTX_set1_cert_store(static_cast<SSL_CTX*>(sslCtx), store);
    return CURLE_OK;
}

enum class Mode { SystemStore, EmptyStore, TrustAny };

const char* modeName(Mode m) {
    switch (m) {
    case Mode::SystemStore: return "verify=system-store";
    case Mode::EmptyStore: return "verify=empty-store";
    case Mode::TrustAny: return "trustAny";
    }
    return "?";
}

bool request(const char* url, Mode mode, X509_STORE* systemStore) {
    CURL* easy = curl_easy_init();
    if (easy == nullptr) {
        std::printf("GET %s [%s]: curl_easy_init failed\n", url, modeName(mode));
        return false;
    }
    X509_STORE* empty = X509_STORE_new();
    Transfer t;
    HeaderCtx hctx{easy, &t};
    char err[CURL_ERROR_SIZE] = {};
    curl_easy_setopt(easy, CURLOPT_URL, url);
    curl_easy_setopt(easy, CURLOPT_PROTOCOLS_STR, "http,https");
    curl_easy_setopt(easy, CURLOPT_HTTP_VERSION, static_cast<long>(CURL_HTTP_VERSION_1_1));
    curl_easy_setopt(easy, CURLOPT_SSLVERSION, static_cast<long>(CURL_SSLVERSION_TLSv1_2));
    curl_easy_setopt(easy, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(easy, CURLOPT_TIMEOUT_MS, 20000L);
    curl_easy_setopt(easy, CURLOPT_ERRORBUFFER, err);
    curl_easy_setopt(easy, CURLOPT_HEADERFUNCTION, onHeaderCapture);
    curl_easy_setopt(easy, CURLOPT_HEADERDATA, &hctx);
    curl_easy_setopt(easy, CURLOPT_WRITEFUNCTION, onBody);
    curl_easy_setopt(easy, CURLOPT_WRITEDATA, &t);
    if (mode == Mode::TrustAny) {
        curl_easy_setopt(easy, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(easy, CURLOPT_SSL_VERIFYHOST, 0L);
    } else {
        curl_easy_setopt(easy, CURLOPT_SSL_VERIFYPEER, 1L);
        curl_easy_setopt(easy, CURLOPT_SSL_VERIFYHOST, 2L);
        curl_easy_setopt(easy, CURLOPT_SSL_CTX_FUNCTION, onSslCtx);
        curl_easy_setopt(easy, CURLOPT_SSL_CTX_DATA, mode == Mode::SystemStore ? systemStore : empty);
    }
    const CURLcode rc = curl_easy_perform(easy);
    long status = 0;
    long verify = 0;
    curl_easy_getinfo(easy, CURLINFO_RESPONSE_CODE, &status);
    curl_easy_getinfo(easy, CURLINFO_SSL_VERIFYRESULT, &verify);
    std::printf("GET %s [%s]: curl=%d (%s) http=%ld tls=%s cipher=%s verify-result=%ld body=%zu%s%s\n", url,
                modeName(mode), static_cast<int>(rc), curl_easy_strerror(rc), status,
                t.tlsVersion.empty() ? "-" : t.tlsVersion.c_str(), t.cipher.empty() ? "-" : t.cipher.c_str(), verify,
                t.bodyBytes, err[0] != '\0' ? " error=" : "", err);
    curl_easy_cleanup(easy);
    X509_STORE_free(empty);
    return rc == CURLE_OK;
}

}  // namespace

int main(int argc, char** argv) {
    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
        std::printf("curl_global_init failed\n");
        return 2;
    }
    const curl_version_info_data* v = curl_version_info(CURLVERSION_NOW);
    std::printf("libcurl %s ssl=%s openssl-runtime=%s\n", v->version, v->ssl_version ? v->ssl_version : "-",
                OpenSSL_version(OPENSSL_VERSION));
    std::printf("features: ipv6=%d ssl=%d asynchdns=%d threadsafe=%d libz=%d http2=%d idn=%d psl=%d\n",
                (v->features & CURL_VERSION_IPV6) != 0, (v->features & CURL_VERSION_SSL) != 0,
                (v->features & CURL_VERSION_ASYNCHDNS) != 0, (v->features & CURL_VERSION_THREADSAFE) != 0,
                (v->features & CURL_VERSION_LIBZ) != 0, (v->features & CURL_VERSION_HTTP2) != 0,
                (v->features & CURL_VERSION_IDN) != 0, (v->features & CURL_VERSION_PSL) != 0);
    std::printf("protocols:");
    for (const char* const* p = v->protocols; *p != nullptr; ++p) {
        std::printf(" %s", *p);
    }
    std::printf("\n");

    // Preferred source first, as the platform layer will do: APEX on
    // Android 14+, /system as the fallback. Both are reported.
    const char* const apex = "/apex/com.android.conscrypt/cacerts";
    const char* const system = "/system/etc/security/cacerts";
    X509_STORE* apexStore = X509_STORE_new();
    X509_STORE* systemStore = X509_STORE_new();
    const DirResult apexResult = loadDirectory(apex, apexStore);
    const DirResult systemResult = loadDirectory(system, systemStore);
    printDirectory(apex, apexResult);
    printDirectory(system, systemResult);
    X509_STORE* chosen = apexResult.certs > 0 ? apexStore : systemStore;
    std::printf("store used for verification: %s\n", chosen == apexStore ? apex : system);

    std::vector<const char*> urls;
    for (int i = 1; i < argc; ++i) {
        urls.push_back(argv[i]);
    }
    if (urls.empty()) {
        urls = {"https://www.google.com/", "https://curl.se/"};
    }

    int failures = 0;
    for (const char* url : urls) {
        if (!request(url, Mode::SystemStore, chosen)) {
            ++failures;
        }
        if (!request(url, Mode::TrustAny, chosen)) {
            ++failures;
        }
        // Negative control: an empty store must fail verification, which
        // proves the system store is what made the first request pass.
        if (request(url, Mode::EmptyStore, chosen)) {
            ++failures;
        }
    }
    // Same check with the fallback store when the APEX one was chosen.
    if (chosen == apexStore && systemResult.certs > 0) {
        if (!request(urls.front(), Mode::SystemStore, systemStore)) {
            ++failures;
        }
        std::printf("(previous line: fallback store %s)\n", system);
    }

    X509_STORE_free(apexStore);
    X509_STORE_free(systemStore);
    curl_global_cleanup();
    std::printf("result: %s (%d unexpected)\n", failures == 0 ? "PASS" : "FAIL", failures);
    return failures == 0 ? 0 : 1;
}
