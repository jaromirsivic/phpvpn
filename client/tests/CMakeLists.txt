add_executable(phpvpn-client-tests
    unit/harness.cpp
    unit/test_base64url.cpp
    unit/test_config.cpp
    unit/test_crypto.cpp
    unit/test_default_config.cpp
    unit/test_ipc.cpp
    unit/test_iso8601.cpp
    unit/test_json.cpp
    unit/test_payload_batch.cpp
    unit/test_queues.cpp
    unit/test_service_registration.cpp
    unit/test_time_sync.cpp
    unit/test_wire.cpp
    unit/test_traffic_counters.cpp
    unit/test_runtime_status.cpp
    unit/test_tray_status.cpp
    unit/test_tray_progress.cpp
    unit/test_tray_layout.cpp
    unit/test_status_contract.cpp
    unit/test_winhttp_accounting.cpp
    ../apps/tray/status_model.cpp
    ../apps/tray/progress_model.cpp
    ../apps/tray/layout.cpp
)

target_include_directories(phpvpn-client-tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/unit
    ${CMAKE_SOURCE_DIR}/apps/service
)
target_link_libraries(phpvpn-client-tests PRIVATE phpvpn-platform phpvpn-core phpvpn-compile-options)

add_test(NAME phpvpn-client-tests COMMAND phpvpn-client-tests)

if(PHPVPN_BUILD_CONTRACT_TESTS)
    add_executable(phpvpn-contract-tests
        unit/harness.cpp
        contract/test_server_contract.cpp
    )
    target_include_directories(phpvpn-contract-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/unit)
    target_link_libraries(phpvpn-contract-tests PRIVATE phpvpn-platform phpvpn-core phpvpn-compile-options)
    add_test(NAME phpvpn-contract-tests COMMAND phpvpn-contract-tests)
endif()
