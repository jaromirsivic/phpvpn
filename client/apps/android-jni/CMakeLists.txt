# libphpvpn-android.so (plan-android-v001): the JNI entry of the Android client
# over the shared ServiceRuntime. WP00 registers only the version hello; the
# runtime start, the in-process IPC dispatch and the portable models follow in
# WP04. Linking the core and the platform library already proves that both
# compile with the NDK toolchain.
add_library(phpvpn-android SHARED
    jni_entry.cpp
)

target_link_libraries(phpvpn-android PRIVATE phpvpn-platform phpvpn-core phpvpn-compile-options log)

# Only JNI_OnLoad is exported; every native method is registered from it.
set_target_properties(phpvpn-android PROPERTIES
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON)

# libphpvpn-android-tests.so (plan-android-v001 T-N03): the POSIX layer tests
# behind one JNI method, so the instrumented test of the bridge module runs them
# inside the application sandbox. The bridge passes the option for its debug
# variant only; libphpvpn-android.so above never links test code.
option(PHPVPN_ANDROID_NATIVE_TESTS "Build libphpvpn-android-tests.so for the instrumented tests" OFF)
if(PHPVPN_ANDROID_NATIVE_TESTS)
    include(${CMAKE_SOURCE_DIR}/tests/android/posix_tests.cmake)
    add_library(phpvpn-android-tests SHARED
        native_tests_jni.cpp
        ${PHPVPN_ANDROID_TEST_HARNESS_DIR}/harness.cpp
        ${PHPVPN_ANDROID_POSIX_TEST_SOURCES}
    )
    target_include_directories(phpvpn-android-tests PRIVATE ${PHPVPN_ANDROID_TEST_HARNESS_DIR})
    target_compile_definitions(phpvpn-android-tests PRIVATE PHPVPN_TEST_HARNESS_NO_MAIN)
    target_link_libraries(phpvpn-android-tests PRIVATE phpvpn-platform phpvpn-core phpvpn-compile-options log)
    set_target_properties(phpvpn-android-tests PROPERTIES
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN ON)
endif()
