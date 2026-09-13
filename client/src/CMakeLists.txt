add_library(phpvpn-core STATIC
    core/base/error.cpp
    core/base/hex.cpp
    core/base/logger.cpp
    core/base/secure_buffer.cpp

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

    core/queues/packet_queues.cpp
    core/queues/scheduler.cpp

    core/runtime/network_worker.cpp
    core/runtime/status_snapshot.cpp
    core/runtime/time_sync.cpp
    core/runtime/traffic_counters.cpp
    core/runtime/tunnel_session.cpp
    core/runtime/tunnel_workers.cpp
    core/runtime/vpn_engine.cpp
)

target_include_directories(phpvpn-core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(phpvpn-core PUBLIC phpvpn-compile-options)

add_library(phpvpn-platform STATIC
    platform/iface/transport.cpp

    platform/windows/clock_win.cpp
    platform/windows/config_files_win.cpp
    platform/windows/crypto_cng.cpp
    platform/windows/ipc_pipe.cpp
    platform/windows/netconfig_iphlpapi.cpp
    platform/windows/paths_win.cpp
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
