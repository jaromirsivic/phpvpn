# POSIX layer tests for Android (plan-android-v001 WP02, T-N01 and T-N03).
# Included by tests/CMakeLists.txt (phpvpn-android-posix-tests, run as the adb
# shell user by tools/android/run-native-tests.ps1 -Tests phpvpn-android-posix-tests)
# and by
# apps/android-jni (libphpvpn-android-tests.so, run by the instrumented test
# inside the application sandbox), so both run the same cases. The test crypto
# provider stands in while the OpenSSL provider of WP01 (PHPVPN_ANDROID_DEPS)
# is missing; with the prebuilt libraries phpvpn-platform brings the real one.
set(PHPVPN_ANDROID_POSIX_TEST_SOURCES
    ${CMAKE_CURRENT_LIST_DIR}/../linux/unit/test_config_files_linux.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../linux/unit/test_paths_trust_root.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../linux/unit/test_fd_tunnel.cpp
    ${CMAKE_CURRENT_LIST_DIR}/test_posix_syscalls.cpp
)
if(NOT PHPVPN_ANDROID_DEPS)
    list(APPEND PHPVPN_ANDROID_POSIX_TEST_SOURCES ${CMAKE_CURRENT_LIST_DIR}/test_crypto_provider.cpp)
endif()
set(PHPVPN_ANDROID_TEST_HARNESS_DIR ${CMAKE_CURRENT_LIST_DIR}/../unit)
