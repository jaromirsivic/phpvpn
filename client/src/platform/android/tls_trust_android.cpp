// Android system CA store for libcurl, see tls_trust_android.h. The loading
// follows client/tests/android/tls_probe (T-N04), which verified both
// directories on API 34 and 36.
#include "platform/android/tls_trust_android.h"

#include <dirent.h>
#include <sys/stat.h>

#include <openssl/err.h>
#include <openssl/pem.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>
#include <openssl/x509_vfy.h>

#include <curl/curl.h>

#include <cstdio>
#include <mutex>

#include "core/base/logger.h"

namespace phpvpn {
namespace {

constexpr const char* kComponent = "android-tls";
// Android 14 ships about 150 roots; the bounds only stop a broken directory.
constexpr int kMaxDirectoryEntries = 4096;
constexpr off_t kMaxCertificateFileBytes = 256 * 1024;

struct DirectoryCloser {
    void operator()(DIR* directory) const noexcept { closedir(directory); }
};
struct FileCloser {
    void operator()(FILE* file) const noexcept { std::fclose(file); }
};

// Each file holds one PEM certificate followed by a text dump of it.
X509* readCertificate(const std::string& path) {
    std::unique_ptr<FILE, FileCloser> file(std::fopen(path.c_str(), "re"));
    if (!file) return nullptr;
    struct stat status{};
    if (::fstat(fileno(file.get()), &status) != 0 || !S_ISREG(status.st_mode) ||
        status.st_size > kMaxCertificateFileBytes) {
        return nullptr;
    }
    return PEM_read_X509(file.get(), nullptr, nullptr, nullptr);
}

// Number of certificates added, -1 when the directory cannot be opened.
int loadDirectory(const std::string& path, X509_STORE* store, int& skipped) {
    std::unique_ptr<DIR, DirectoryCloser> directory(opendir(path.c_str()));
    if (!directory) return -1;
    int added = 0;
    int entries = 0;
    while (const dirent* entry = readdir(directory.get())) {
        if (entry->d_name[0] == '.') continue;
        if (++entries > kMaxDirectoryEntries) break;
        X509* certificate = readCertificate(path + "/" + entry->d_name);
        if (certificate == nullptr) {
            ++skipped;
            ERR_clear_error();
            continue;
        }
        // The same root twice is not an error.
        if (X509_STORE_add_cert(store, certificate) == 1) {
            ++added;
        } else {
            ERR_clear_error();
        }
        X509_free(certificate);
    }
    return added;
}

struct SystemStore {
    std::mutex mutex;
    X509_STORE* store = nullptr;
    AndroidTrustStoreInfo info;
};

// Never destroyed: a curl handle of a late thread may still reference it.
SystemStore& systemStore() {
    static SystemStore* instance = new SystemStore;
    return *instance;
}

Result<X509_STORE*> loadedSystemStore(AndroidTrustStoreInfo* infoOut) {
    SystemStore& cache = systemStore();
    std::lock_guard<std::mutex> lock(cache.mutex);
    if (cache.store == nullptr) {
        AndroidTrustStoreInfo info;
        X509StoreHandle loaded;
        PHPVPN_TRY_ASSIGN(loaded, loadAndroidTrustStore({kAndroidApexCaDirectory, kAndroidSystemCaDirectory}, info));
        cache.store = loaded.release();
        cache.info = info;
        PHPVPN_LOG_INFO(kComponent, "the system CA store was loaded with " + std::to_string(info.certificates) +
                                        (info.directoryIndex == 0 ? " certificates from the APEX module"
                                                                  : " certificates from the system partition"));
    }
    if (infoOut != nullptr) *infoOut = cache.info;
    return cache.store;
}

CURLcode onSslContext(CURL* easy, void* sslContext, void* store) {
    (void)easy;
    // set1 takes its own reference, one store serves every connection.
    SSL_CTX_set1_cert_store(static_cast<SSL_CTX*>(sslContext), static_cast<X509_STORE*>(store));
    return CURLE_OK;
}

}  // namespace

void X509StoreDeleter::operator()(x509_store_st* store) const noexcept { X509_STORE_free(store); }

Result<X509StoreHandle> loadAndroidTrustStore(const std::vector<std::string>& directories,
                                              AndroidTrustStoreInfo& info) {
    info = AndroidTrustStoreInfo{};
    for (std::size_t index = 0; index < directories.size(); ++index) {
        X509StoreHandle store(X509_STORE_new());
        if (!store) return Error(ErrorCode::transport_tls_failed, "the CA store could not be created");
        // curl sets this on the store it would have built itself, so the
        // verification matches Linux.
        X509_STORE_set_flags(store.get(), X509_V_FLAG_PARTIAL_CHAIN);
        int skipped = 0;
        const int added = loadDirectory(directories[index], store.get(), skipped);
        if (added > 0) {
            info.directoryIndex = index;
            info.certificates = added;
            info.skippedFiles = skipped;
            return store;
        }
    }
    return Error(ErrorCode::transport_tls_failed, "no system CA certificate could be loaded");
}

Result<AndroidTrustStoreInfo> androidSystemTrustStoreInfo() {
    AndroidTrustStoreInfo info;
    X509_STORE* store = nullptr;
    PHPVPN_TRY_ASSIGN(store, loadedSystemStore(&info));
    (void)store;
    return info;
}

Status installAndroidSystemTrust(void* curlEasy) {
    CURL* easy = curlEasy;
    X509_STORE* store = nullptr;
    PHPVPN_TRY_ASSIGN(store, loadedSystemStore(nullptr));
    if (curl_easy_setopt(easy, CURLOPT_SSL_CTX_FUNCTION, onSslContext) != CURLE_OK ||
        curl_easy_setopt(easy, CURLOPT_SSL_CTX_DATA, store) != CURLE_OK) {
        return Error(ErrorCode::transport_unavailable, "HTTP client option failed");
    }
    return Status::success();
}

}  // namespace phpvpn
