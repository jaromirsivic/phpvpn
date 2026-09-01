cmake_minimum_required(VERSION 3.28)

project(phpvpn-client
    VERSION 1.0.0
    DESCRIPTION "phpvpn v1 native client"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT WIN32)
    message(FATAL_ERROR "phpvpn client v1 ships for Windows only; the core stays portable but no other platform layer exists yet.")
endif()

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

set(PHPVPN_CLIENT_VERSION "${PROJECT_VERSION}")
set(PHPVPN_IPC_VERSION 1)

add_library(phpvpn-compile-options INTERFACE)
target_compile_features(phpvpn-compile-options INTERFACE cxx_std_20)
target_compile_definitions(phpvpn-compile-options INTERFACE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    NTDDI_VERSION=0x0A000000
    _WIN32_WINNT=0x0A00
    PHPVPN_CLIENT_VERSION="${PHPVPN_CLIENT_VERSION}"
    PHPVPN_IPC_VERSION=${PHPVPN_IPC_VERSION})

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

add_subdirectory(src)
add_subdirectory(apps)

include(CTest)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
