# libphpvpn-android.so (plan-android-v001): the JNI entry of the Android client
# over the shared ServiceRuntime. WP00 registers the version hello and WP03 the
# VPN hosts (jni_vpn_bridge); the runtime start, the in-process IPC dispatch
# and the portable models follow in WP04. Linking the core and the platform
# library already proves that both compile with the NDK toolchain.
add_library(phpvpn-android SHARED
    jni_entry.cpp
    jni_vpn_bridge.cpp
)

target_link_libraries(phpvpn-android PRIVATE phpvpn-platform phpvpn-core phpvpn-compile-options log)

# Only JNI_OnLoad is exported; every native method is registered from it.
# The static OpenSSL, curl and libc++ archives are built with default
# visibility, so --exclude-libs keeps their symbols out of .dynsym as well
# (WP01 recommendation): a smaller symbol table, direct calls and no clash
# with another copy of OpenSSL in the process.
set_target_properties(phpvpn-android PROPERTIES
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON)
target_link_options(phpvpn-android PRIVATE -Wl,--exclude-libs,ALL)

# libphpvpn-android-tests.so (plan-android-v001 T-N03, T-N02, T-A01): the POSIX
# layer and the Android platform tests behind JNI methods, so the instrumented
# tests of the bridge module run them inside the application sandbox, and the
# native half of the VpnService scenarios (vpn_tests_jni.cpp) over the same JNI
# hosts as production. The bridge passes the option for its debug variant only;
# libphpvpn-android.so above never links test code.
option(PHPVPN_ANDROID_NATIVE_TESTS "Build libphpvpn-android-tests.so for the instrumented tests" OFF)
if(PHPVPN_ANDROID_NATIVE_TESTS)
    include(${CMAKE_SOURCE_DIR}/tests/android/posix_tests.cmake)
    include(${CMAKE_SOURCE_DIR}/tests/android/platform_tests.cmake)
    add_library(phpvpn-android-tests SHARED
        native_tests_jni.cpp
        jni_vpn_bridge.cpp
        vpn_tests_jni.cpp
        ${PHPVPN_ANDROID_TEST_HARNESS_DIR}/harness.cpp
        ${PHPVPN_ANDROID_POSIX_TEST_SOURCES}
        ${PHPVPN_ANDROID_PLATFORM_TEST_SOURCES}
        ${PHPVPN_ANDROID_PLATFORM_POSIX_TEST_SOURCES}
        ${PHPVPN_ANDROID_PLATFORM_DEVICE_TEST_SOURCES}
    )
    target_include_directories(phpvpn-android-tests PRIVATE
        ${PHPVPN_ANDROID_TEST_HARNESS_DIR} ${CMAKE_SOURCE_DIR}/tests/android ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_definitions(phpvpn-android-tests PRIVATE PHPVPN_TEST_HARNESS_NO_MAIN)
    if(PHPVPN_ANDROID_DEPS)
        # The curl transport and the CA store exist only with the WP01 libraries;
        # the TLS diagnosis of vpn_tests_jni.cpp calls curl directly.
        target_compile_definitions(phpvpn-android-tests PRIVATE PHPVPN_ANDROID_TLS_TESTS CURL_STATICLIB)
        target_include_directories(phpvpn-android-tests SYSTEM PRIVATE
            "${PHPVPN_ANDROID_DEPS}/${CMAKE_ANDROID_ARCH_ABI}/include")
    endif()
    target_link_libraries(phpvpn-android-tests PRIVATE phpvpn-platform phpvpn-core phpvpn-compile-options log)
    set_target_properties(phpvpn-android-tests PROPERTIES
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN ON)
    target_link_options(phpvpn-android-tests PRIVATE -Wl,--exclude-libs,ALL)
endif()
