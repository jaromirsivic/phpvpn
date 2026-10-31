// System CA trust for the curl transport on Android (plan-android-v001
// point 10, spike T-N04 in WP01).
//
// Android ships no CA bundle that OpenSSL can read through CApath: the files in
// /apex/com.android.conscrypt/cacerts (Android 14+) and
// /system/etc/security/cacerts are named by the old subject_hash_old, so the
// OpenSSL lookup by the current hash never finds them. Every file is therefore
// read with PEM_read_X509 into one X509_STORE, once per process: the APEX
// directory first (it receives CA updates through mainline), /system only when
// the APEX directory yields nothing. User-added CAs are not loaded, the default
// policy of Android for apps. The store is shared by every request through
// CURLOPT_SSL_CTX_FUNCTION (SSL_CTX_set1_cert_store takes its own reference).
// Nothing is loaded for trustAnyTlsCertificate=true; transport_curl.cpp calls
// installAndroidSystemTrust only for verifying requests. Failures surface only
// as error codes, never with a path or an OpenSSL error text.
#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "core/base/result.h"

struct x509_store_st;

namespace phpvpn {

inline constexpr const char* kAndroidApexCaDirectory = "/apex/com.android.conscrypt/cacerts";
inline constexpr const char* kAndroidSystemCaDirectory = "/system/etc/security/cacerts";

struct AndroidTrustStoreInfo {
    // Index into the directory list of the directory that was used.
    std::size_t directoryIndex = 0;
    int certificates = 0;
    // Files that were no regular file, too large, unreadable or not PEM.
    int skippedFiles = 0;
};

struct X509StoreDeleter {
    void operator()(x509_store_st* store) const noexcept;
};
using X509StoreHandle = std::unique_ptr<x509_store_st, X509StoreDeleter>;

// Loads the first directory of `directories` that yields at least one
// certificate; transport_tls_failed when none does. Not cached.
Result<X509StoreHandle> loadAndroidTrustStore(const std::vector<std::string>& directories,
                                              AndroidTrustStoreInfo& info);

// The process-wide store of the system directories, loaded on the first call.
// A failed load is retried by the next call.
Result<AndroidTrustStoreInfo> androidSystemTrustStoreInfo();

// Installs the process-wide store into a verifying curl request. `curlEasy` is
// the CURL* (curl.h declares CURL as void), so this header needs no curl
// include path.
Status installAndroidSystemTrust(void* curlEasy);

}  // namespace phpvpn
