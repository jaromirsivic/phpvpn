# T-N04 shell-level spike (plan-android-v001, WP01). Standalone project, not
# part of the client build: builds one executable for adb shell against the
# prebuilt deps from tools/android/build-deps.sh, discovered the same way as
# client/src/CMakeLists.txt does.
#
#   cmake -S client/tests/android/tls_probe -B work/build/tls-probe-x86_64 -G Ninja
#         -DCMAKE_TOOLCHAIN_FILE=<ndk>/build/cmake/android.toolchain.cmake
#         -DANDROID_ABI=x86_64 -DANDROID_PLATFORM=android-34 -DANDROID_STL=c++_static
#         -DPHPVPN_ANDROID_DEPS=<deps root>
#   cmake --build work/build/tls-probe-x86_64
#   adb push work/build/tls-probe-x86_64/tls_probe /data/local/tmp/
#   adb shell /data/local/tmp/tls_probe
cmake_minimum_required(VERSION 3.28)
project(phpvpn_tls_probe LANGUAGES CXX)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Android")
    message(FATAL_ERROR "tls_probe is built with the NDK toolchain only")
endif()
set(PHPVPN_ANDROID_DEPS "" CACHE PATH "Prebuilt OpenSSL and curl, <root>/<abi>/{include,lib}")
set(deps "${PHPVPN_ANDROID_DEPS}/${CMAKE_ANDROID_ARCH_ABI}")
if(NOT IS_DIRECTORY "${deps}/lib")
    message(FATAL_ERROR "PHPVPN_ANDROID_DEPS has no ${CMAKE_ANDROID_ARCH_ABI}/lib: ${deps}")
endif()

list(APPEND CMAKE_FIND_ROOT_PATH "${deps}")
set(OPENSSL_ROOT_DIR "${deps}")
set(OPENSSL_USE_STATIC_LIBS ON)
find_package(OpenSSL 3 REQUIRED COMPONENTS Crypto SSL)
set(CURL_ROOT "${deps}")
set(CURL_USE_STATIC_LIBS ON)
find_package(CURL 8.5 REQUIRED)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_executable(tls_probe tls_probe.cpp)
target_compile_options(tls_probe PRIVATE -Wall -Wextra -Wpedantic -Werror)
target_link_libraries(tls_probe PRIVATE CURL::libcurl OpenSSL::SSL OpenSSL::Crypto)
