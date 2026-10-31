# Android platform layer tests T-N02 (plan-android-v001 WP03).
#
# The context, the snapshot parser, AndroidNetConfig and AndroidTunnelDriver are
# pure C++ over the abstract hosts, so they also run on the build host:
# phpvpn-android-host-tests (tests/CMakeLists.txt) compiles the Android sources
# below directly against phpvpn-core on Windows and Linux, without the
# platform library of the host. On Android the same cases run inside the
# application sandbox (libphpvpn-android-tests.so, apps/android-jni) and as the
# shell user (phpvpn-android-platform-tests), where phpvpn-platform already
# contains the sources.
set(PHPVPN_ANDROID_PLATFORM_DIR ${CMAKE_CURRENT_LIST_DIR}/../../src/platform/android)

# Sources compiled into the host test executable.
set(PHPVPN_ANDROID_HOST_PLATFORM_SOURCES
    ${PHPVPN_ANDROID_PLATFORM_DIR}/netconfig_android.cpp
    ${PHPVPN_ANDROID_PLATFORM_DIR}/tunnel_android.cpp
    ${PHPVPN_ANDROID_PLATFORM_DIR}/vpn_context.cpp
)
# Linux only: CLOCK_BOOTTIME and the eventfd of FdTunnel.
set(PHPVPN_ANDROID_HOST_LINUX_PLATFORM_SOURCES
    ${PHPVPN_ANDROID_PLATFORM_DIR}/clock_android.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../../src/platform/posix/fd_tunnel.cpp
)

# Portable cases (Windows, Linux, Android).
set(PHPVPN_ANDROID_PLATFORM_TEST_SOURCES
    ${CMAKE_CURRENT_LIST_DIR}/test_netconfig_android.cpp
    ${CMAKE_CURRENT_LIST_DIR}/test_tunnel_android.cpp
    ${CMAKE_CURRENT_LIST_DIR}/test_vpn_context.cpp
)
# Linux and Android cases.
set(PHPVPN_ANDROID_PLATFORM_POSIX_TEST_SOURCES
    ${CMAKE_CURRENT_LIST_DIR}/test_clock_android.cpp
    ${CMAKE_CURRENT_LIST_DIR}/test_tunnel_android_fd.cpp
)
# Android only: liblog, and the CA store with the OpenSSL of WP01.
set(PHPVPN_ANDROID_PLATFORM_DEVICE_TEST_SOURCES
    ${CMAKE_CURRENT_LIST_DIR}/test_log_sink_android.cpp
)
if(PHPVPN_ANDROID_DEPS)
    list(APPEND PHPVPN_ANDROID_PLATFORM_DEVICE_TEST_SOURCES ${CMAKE_CURRENT_LIST_DIR}/test_tls_trust_android.cpp)
endif()
