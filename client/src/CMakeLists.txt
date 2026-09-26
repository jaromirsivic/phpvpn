# Platform independent helpers of the platform boundary. The core calls some of
# them itself (action names, snapshot comparison) and both platform IPC servers
# share the IPC executors, so they live in the core library and the core links
# without any platform implementation.
set(PHPVPN_PLATFORM_SHARED_SOURCES
    platform/iface/ipc_executors.cpp
    platform/iface/netconfig.cpp
    platform/iface/transport.cpp
)

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
        platform/windows/durable_file_win.cpp
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

    add_library(phpvpn-platform STATIC
        platform/linux/clock_linux.cpp
        platform/linux/config_files_linux.cpp
        platform/linux/crypto_openssl.cpp
        platform/linux/dns_snapshot.cpp
        platform/linux/durable_file_linux.cpp
        platform/linux/ipc_unix.cpp
        platform/linux/netconfig_netlink.cpp
        platform/linux/netlink_codec.cpp
        platform/linux/paths_linux.cpp
        platform/linux/transport_curl.cpp
        platform/linux/tunnel_tun.cpp
    )

    target_include_directories(phpvpn-platform PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

    target_link_libraries(phpvpn-platform
        PUBLIC phpvpn-core phpvpn-compile-options Threads::Threads
        PRIVATE OpenSSL::Crypto CURL::libcurl PkgConfig::PHPVPN_SYSTEMD
    )
endif()
