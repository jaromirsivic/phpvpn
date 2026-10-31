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
