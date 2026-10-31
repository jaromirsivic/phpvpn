# Platform independent helpers of the platform boundary. The core calls some of
# them itself (action names, snapshot comparison) and both platform IPC servers
# share the IPC executors, so they live in the core library and the core links
# without any platform implementation.
set(PHPVPN_PLATFORM_SHARED_SOURCES
    platform/iface/ipc_executors.cpp
    platform/iface/netconfig.cpp
    platform/iface/transport.cpp
)
# durableReplaceFile is called by core/runtime/tunnel_session.cpp. It has no
# library dependency, so it lives in the core and GNU ld needs no cyclic
# core <-> platform link order.
if(WIN32)
    list(APPEND PHPVPN_PLATFORM_SHARED_SOURCES platform/windows/durable_file_win.cpp)
elseif(PHPVPN_PLATFORM STREQUAL "linux" OR PHPVPN_PLATFORM STREQUAL "macos" OR PHPVPN_PLATFORM STREQUAL "android")
    list(APPEND PHPVPN_PLATFORM_SHARED_SOURCES platform/posix/durable_file_posix.cpp)
endif()

add_library(phpvpn-core STATIC
    core/base/error.cpp
    core/base/hex.cpp
    core/base/logger.cpp
    core/base/secure_buffer.cpp
    core/base/utf8.cpp

    core/config/config_document.cpp
    core/config/contracts_document.cpp
    core/presentation/contracts_model.cpp
    core/presentation/server_info_model.cpp
    core/presentation/password_key_model.cpp
    core/config/config_store.cpp
    core/config/config_defaults.cpp
    core/wizard/wizard_defaults.cpp
    core/wizard/probe_client.cpp
    core/wizard/http_diagnostic.cpp
    core/wizard/wizard_service.cpp
    core/wizard/wizard_model.cpp
    core/config/config.cpp
    core/config/config_parser.cpp
    core/config/config_validator.cpp

    core/crypto/canonical.cpp
    core/crypto/masked_contracts.cpp
    core/protocol/info_extensions.cpp
    core/crypto/crypto_provider.cpp
    core/crypto/hkdf.cpp
    core/crypto/key_derivation.cpp

    core/ipc/ipc_dispatcher.cpp
    core/ipc/ipc_protocol.cpp
    core/ipc/wizard_ipc.cpp

    core/protocol/http_headers.cpp
    core/protocol/base64url.cpp
    core/protocol/data_record.cpp
    core/protocol/ipv4.cpp
    core/protocol/iso8601.cpp
    core/protocol/json.cpp
    core/protocol/json_scanner.cpp
    core/protocol/ndjson_reader.cpp
    core/protocol/payload_batch.cpp
    core/protocol/request_builder.cpp
    core/protocol/response_parser.cpp
    core/protocol/server_version.cpp
    core/protocol/uuid.cpp

    core/queues/flow_key.cpp
    core/queues/packet_queues.cpp
    core/queues/scheduler.cpp

    core/runtime/network_worker.cpp
    core/runtime/status_snapshot.cpp
    core/runtime/time_sync.cpp
    core/runtime/traffic_counters.cpp
    core/runtime/tunnel_session.cpp
    core/runtime/tunnel_workers.cpp
    core/runtime/vpn_engine.cpp

    ${PHPVPN_PLATFORM_SHARED_SOURCES}
)

target_include_directories(phpvpn-core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(phpvpn-core PUBLIC phpvpn-compile-options)

if(WIN32)
    add_library(phpvpn-platform STATIC
        platform/windows/clock_win.cpp
        platform/windows/config_files_win.cpp
        platform/windows/crypto_cng.cpp
        platform/windows/ipc_pipe.cpp
        platform/windows/netconfig_iphlpapi.cpp
        platform/windows/paths_win.cpp
        platform/windows/tray_autostart.cpp
        platform/windows/transport_winhttp.cpp
        platform/windows/tunnel_wintun.cpp
    )

    target_include_directories(phpvpn-platform
        PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
        PRIVATE ${CMAKE_SOURCE_DIR}/third_party/wintun/include
    )

    target_link_libraries(phpvpn-platform
        PUBLIC phpvpn-core phpvpn-compile-options
        PRIVATE bcrypt winhttp iphlpapi ws2_32 advapi32 shell32 ole32
    )
endif()
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_package(OpenSSL 3 REQUIRED COMPONENTS Crypto)
    find_package(CURL 8.5 REQUIRED)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(PHPVPN_SYSTEMD REQUIRED IMPORTED_TARGET libsystemd)
    find_package(Threads REQUIRED)

    # The POSIX files (platform/posix) are shared with macOS: fixed-path
    # configuration files, trusted directories, the Unix-socket IPC with its
    # epoll/kqueue poller and the durable file replacement.
    add_library(phpvpn-platform STATIC
        platform/linux/clock_linux.cpp
        platform/posix/config_files_posix.cpp
        platform/linux/crypto_openssl.cpp
        platform/linux/dns_snapshot.cpp
        platform/linux/interface_identity.cpp
        platform/posix/event_wait.cpp
        platform/posix/fd_tunnel.cpp
        platform/posix/ipc_unix.cpp
        platform/linux/netconfig_netlink.cpp
        platform/linux/netlink_codec.cpp
        platform/linux/netlink_socket.cpp
        platform/posix/paths_posix.cpp
        platform/linux/transport_curl.cpp
        platform/linux/tunnel_tun.cpp
    )

    target_include_directories(phpvpn-platform PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

    target_link_libraries(phpvpn-platform
        PUBLIC phpvpn-core phpvpn-compile-options Threads::Threads
        PRIVATE OpenSSL::Crypto CURL::libcurl PkgConfig::PHPVPN_SYSTEMD
    )
endif()
if(PHPVPN_PLATFORM STREQUAL "macos")
    # macOS (plan-macos-v001): utun tunnel, PF_ROUTE network configuration,
    # SystemConfiguration DNS snapshot, the libcurl transport and the OpenSSL
    # crypto provider shared with Linux (OpenSSL 3 linked statically, libcurl
    # from the macOS SDK), and the shared POSIX files.
    set(OPENSSL_USE_STATIC_LIBS ON)
    find_package(OpenSSL 3 REQUIRED COMPONENTS Crypto)
    find_package(CURL 8.4 REQUIRED)
    find_package(Threads REQUIRED)
    find_library(PHPVPN_SYSTEMCONFIGURATION SystemConfiguration REQUIRED)
    find_library(PHPVPN_COREFOUNDATION CoreFoundation REQUIRED)

    add_library(phpvpn-platform STATIC
        platform/macos/clock_macos.cpp
        platform/posix/config_files_posix.cpp
        platform/linux/crypto_openssl.cpp
        platform/macos/dns_snapshot.cpp
        platform/macos/interface_identity.cpp
        platform/posix/event_wait.cpp
        platform/posix/ipc_unix.cpp
        platform/macos/netconfig_route.cpp
        platform/macos/route_codec.cpp
        platform/macos/route_socket.cpp
        platform/posix/paths_posix.cpp
        platform/linux/transport_curl.cpp
        platform/macos/tunnel_utun.cpp
    )

    target_include_directories(phpvpn-platform PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

    target_link_libraries(phpvpn-platform
        PUBLIC phpvpn-core phpvpn-compile-options Threads::Threads
        PRIVATE OpenSSL::Crypto CURL::libcurl
                ${PHPVPN_SYSTEMCONFIGURATION} ${PHPVPN_COREFOUNDATION}
    )
endif()
if(PHPVPN_PLATFORM STREQUAL "android")
    # Android (plan-android-v001): the shared POSIX configuration files and
    # trusted paths compile unchanged (bionic has renameat2 since API 30; the
    # secure wipe uses memset_explicit, API 34). The Unix-socket IPC is not built: UI and
    # service share one process. The packet I/O of the VpnService descriptor is
    # the FdTunnel shared with Linux. The tunnel driver, the ConnectivityManager
    # NetConfig, the BOOTTIME clock, the TLS trust store and the logcat sink
    # arrive with WP03 in platform/android. The OpenSSL crypto provider and the
    # curl transport shared with Linux need the prebuilt static libraries of
    # WP01 (PHPVPN_ANDROID_DEPS).
    find_package(Threads REQUIRED)

    add_library(phpvpn-platform STATIC
        platform/posix/config_files_posix.cpp
        platform/posix/fd_tunnel.cpp
        platform/posix/paths_posix.cpp
    )

    target_include_directories(phpvpn-platform PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

    target_link_libraries(phpvpn-platform
        PUBLIC phpvpn-core phpvpn-compile-options Threads::Threads
        PRIVATE log
    )

    if(PHPVPN_ANDROID_DEPS)
        set(PHPVPN_ANDROID_DEPS_ABI "${PHPVPN_ANDROID_DEPS}/${CMAKE_ANDROID_ARCH_ABI}")
        if(NOT IS_DIRECTORY "${PHPVPN_ANDROID_DEPS_ABI}/lib")
            message(FATAL_ERROR "PHPVPN_ANDROID_DEPS has no ${CMAKE_ANDROID_ARCH_ABI}/lib: ${PHPVPN_ANDROID_DEPS_ABI}")
        endif()
        set(OPENSSL_ROOT_DIR "${PHPVPN_ANDROID_DEPS_ABI}")
        set(OPENSSL_USE_STATIC_LIBS ON)
        find_package(OpenSSL 3 REQUIRED COMPONENTS Crypto)
        set(CURL_ROOT "${PHPVPN_ANDROID_DEPS_ABI}")
        set(CURL_USE_STATIC_LIBS ON)
        find_package(CURL 8.5 REQUIRED)
        target_sources(phpvpn-platform PRIVATE
            platform/linux/crypto_openssl.cpp
            platform/linux/transport_curl.cpp
        )
        target_compile_definitions(phpvpn-platform PRIVATE CURL_STATICLIB)
        target_link_libraries(phpvpn-platform PRIVATE CURL::libcurl OpenSSL::Crypto OpenSSL::SSL)
    endif()
endif()
