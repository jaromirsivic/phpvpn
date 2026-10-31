cmake_minimum_required(VERSION 3.28)

project(phpvpn-client
    VERSION 1.0.3
    DESCRIPTION "phpvpn v1 native client"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(WIN32)
    set(PHPVPN_PLATFORM "windows")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Android")
    # Android: VpnService descriptor tunnel, in-process IPC, JNI entry in
    # apps/android-jni, OpenSSL and curl prebuilt by tools/android
    # (plan-android-v001). Configured by Gradle through the NDK toolchain.
    set(PHPVPN_PLATFORM "android")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(PHPVPN_PLATFORM "linux")
elseif(APPLE AND CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    # macOS: utun, PF_ROUTE, launchd, system libcurl and a static OpenSSL 3
    # (plan-macos-v001). iOS has no platform layer yet.
    set(PHPVPN_PLATFORM "macos")
else()
    message(FATAL_ERROR "phpvpn client v1 supports Windows, Linux, macOS and Android only; ${CMAKE_SYSTEM_NAME} has no platform layer.")
endif()

option(PHPVPN_ENABLE_ASAN "Build with AddressSanitizer (and UndefinedBehaviorSanitizer on GCC/Clang)" OFF)
option(PHPVPN_WARNINGS_AS_ERRORS "Treat compiler warnings as errors on GCC and Clang" ON)
option(PHPVPN_BUILD_TRAY "Build the tray application (Linux: needs GTK 3 and ayatana-appindicator; macOS: AppKit menu bar app)" ON)
# Android only: root of the prebuilt static OpenSSL and curl
# (tools/android/build-deps.sh), laid out as <root>/<abi>/{include,lib}. Empty
# skips the crypto provider and the curl transport (WP00 skeleton builds).
set(PHPVPN_ANDROID_DEPS "" CACHE PATH "Prebuilt OpenSSL and curl for Android, <root>/<abi>/{include,lib}")

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

set(PHPVPN_CLIENT_VERSION "${PROJECT_VERSION}")
set(PHPVPN_IPC_VERSION 1)

add_library(phpvpn-compile-options INTERFACE)
target_compile_features(phpvpn-compile-options INTERFACE cxx_std_20)
target_compile_definitions(phpvpn-compile-options INTERFACE
    PHPVPN_CLIENT_VERSION="${PHPVPN_CLIENT_VERSION}"
    PHPVPN_IPC_VERSION=${PHPVPN_IPC_VERSION})
if(WIN32)
    target_compile_definitions(phpvpn-compile-options INTERFACE
        UNICODE
        _UNICODE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        NTDDI_VERSION=0x0A000000
        _WIN32_WINNT=0x0A00)
endif()

if(MSVC)
    target_compile_options(phpvpn-compile-options INTERFACE
        /permissive-
        /W4
        /WX
        /utf-8
        /EHsc
        /GS
        /guard:cf
        /Zi
        /Zc:__cplusplus
        /Zc:preprocessor)

    # AddressSanitizer needs the linker to keep the default checks that /GUARD:CF and
    # incremental linking would otherwise disturb, so the security flags are applied
    # per configuration rather than globally.
    if(PHPVPN_ENABLE_ASAN)
        target_compile_options(phpvpn-compile-options INTERFACE /fsanitize=address)
        target_link_options(phpvpn-compile-options INTERFACE /DEBUG /INCREMENTAL:NO)
    else()
        target_link_options(phpvpn-compile-options INTERFACE
            /DEBUG
            /OPT:REF
            /OPT:ICF
            /DYNAMICBASE
            /HIGHENTROPYVA
            /NXCOMPAT
            /GUARD:CF
            /INCREMENTAL:NO)
    endif()
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(phpvpn-compile-options INTERFACE
        -Wall
        -Wextra
        -Wpedantic
        $<$<BOOL:${PHPVPN_WARNINGS_AS_ERRORS}>:-Werror>
        -fstack-protector-strong
        # The Android output is a shared library, so it is PIC, not PIE.
        $<IF:$<STREQUAL:${PHPVPN_PLATFORM},android>,-fPIC,-fPIE>)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|amd64")
        target_compile_options(phpvpn-compile-options INTERFACE -fcf-protection)
    endif()
    if(PHPVPN_PLATFORM STREQUAL "android")
        # NDK clang with bionic: _FORTIFY_SOURCE level 2 is the supported level,
        # the GNU ld hardening flags apply to the shared library and -pie does
        # not (plan-android-v001 point 15). 16 KB page alignment is the NDK r28
        # default.
        target_compile_options(phpvpn-compile-options INTERFACE
            $<$<CONFIG:Release,RelWithDebInfo,MinSizeRel>:-U_FORTIFY_SOURCE>
            $<$<CONFIG:Release,RelWithDebInfo,MinSizeRel>:-D_FORTIFY_SOURCE=2>)
        target_link_options(phpvpn-compile-options INTERFACE
            -Wl,-z,relro,-z,now,-z,noexecstack)
    elseif(APPLE)
        # Apple clang: level 2 is the supported _FORTIFY_SOURCE, PIE is the
        # default of the 64-bit Darwin linker and the GNU ld hardening flags do
        # not exist. The deployment target is set by the presets (14.0).
        target_compile_options(phpvpn-compile-options INTERFACE
            $<$<CONFIG:Release,RelWithDebInfo,MinSizeRel>:-U_FORTIFY_SOURCE>
            $<$<CONFIG:Release,RelWithDebInfo,MinSizeRel>:-D_FORTIFY_SOURCE=2>)
        # The Homebrew OpenSSL bottle is built for the host macOS release, so
        # every link against it would print one "built for newer macOS" warning
        # per object file; linker errors are unaffected.
        target_link_options(phpvpn-compile-options INTERFACE -Wl,-w)
    else()
        # _FORTIFY_SOURCE needs the optimizer and warns without it, so Debug stays
        # plain. Some distributions predefine a level, which is replaced explicitly.
        target_compile_options(phpvpn-compile-options INTERFACE
            $<$<CONFIG:Release,RelWithDebInfo,MinSizeRel>:-U_FORTIFY_SOURCE>
            $<$<CONFIG:Release,RelWithDebInfo,MinSizeRel>:-D_FORTIFY_SOURCE=3>)
        target_link_options(phpvpn-compile-options INTERFACE
            -pie
            -Wl,-z,relro,-z,now,-z,noexecstack)
    endif()
    if(PHPVPN_ENABLE_ASAN)
        target_compile_options(phpvpn-compile-options INTERFACE
            -fsanitize=address,undefined
            -fno-omit-frame-pointer
            -fno-sanitize-recover=undefined)
        target_link_options(phpvpn-compile-options INTERFACE -fsanitize=address,undefined)
    endif()
else()
    message(FATAL_ERROR "Unsupported compiler ${CMAKE_CXX_COMPILER_ID}; use MSVC, GCC or Clang.")
endif()

add_subdirectory(src)
add_subdirectory(apps)

include(CTest)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
