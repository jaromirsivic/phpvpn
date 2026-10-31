// T-N04 in the application (plan-android-v001 point 10, WP03): the system CA
// store loads from the APEX module, falls back to /system and refuses an
// empty set. Inside libphpvpn-android-tests.so this runs as untrusted_app,
// which the WP01 shell spike could not cover. Android only, needs the OpenSSL
// of WP01 (PHPVPN_ANDROID_DEPS). The HTTPS handshake itself runs in the
// instrumented AndroidTlsTrustTest, because it needs the network.
#include "harness.h"

#include <string>
#include <vector>

#include "platform/android/tls_trust_android.h"

namespace phpvpn {

PHPVPN_TEST(AndroidTlsTrust, ApexDirectoryComesFirst) {
    AndroidTrustStoreInfo info;
    const auto store = loadAndroidTrustStore({kAndroidApexCaDirectory, kAndroidSystemCaDirectory}, info);
    PHPVPN_REQUIRE_OK(store);
    PHPVPN_CHECK(store.value() != nullptr);
    PHPVPN_CHECK_EQ(info.directoryIndex, std::size_t{0});
    // Android 14 ships about 130 to 150 roots.
    PHPVPN_CHECK(info.certificates >= 100);
    PHPVPN_CHECK_EQ(info.skippedFiles, 0);
}

PHPVPN_TEST(AndroidTlsTrust, SystemPartitionIsTheFallback) {
    AndroidTrustStoreInfo info;
    const auto store =
        loadAndroidTrustStore({"/nonexistent/phpvpn-cacerts", "/proc/self/fdinfo", kAndroidSystemCaDirectory}, info);
    PHPVPN_REQUIRE_OK(store);
    PHPVPN_CHECK_EQ(info.directoryIndex, std::size_t{2});
    PHPVPN_CHECK(info.certificates >= 100);
}

PHPVPN_TEST(AndroidTlsTrust, NoCertificateIsAnError) {
    AndroidTrustStoreInfo info;
    PHPVPN_REQUIRE_ERROR(loadAndroidTrustStore({"/nonexistent/phpvpn-cacerts"}, info), ErrorCode::transport_tls_failed);
    PHPVPN_REQUIRE_ERROR(loadAndroidTrustStore({}, info), ErrorCode::transport_tls_failed);
}

PHPVPN_TEST(AndroidTlsTrust, ProcessStoreIsLoadedOnce) {
    const auto first = androidSystemTrustStoreInfo();
    PHPVPN_REQUIRE_OK(first);
    const auto second = androidSystemTrustStoreInfo();
    PHPVPN_REQUIRE_OK(second);
    PHPVPN_CHECK_EQ(first->certificates, second->certificates);
    PHPVPN_CHECK_EQ(first->directoryIndex, std::size_t{0});
}

}  // namespace phpvpn
