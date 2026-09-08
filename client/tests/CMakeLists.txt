add_executable(phpvpn-client-tests
    unit/harness.cpp
    unit/test_base64url.cpp
    unit/test_config.cpp
    unit/test_contracts_document.cpp
    unit/test_contracts_model.cpp
    unit/test_password_key.cpp
    unit/test_config_files_win.cpp
    unit/test_config_store.cpp
    unit/test_wizard_ipc.cpp
    unit/test_wizard_document.cpp
    unit/test_wizard_model.cpp
    unit/test_wizard_layout.cpp
    unit/test_crypto.cpp
    unit/test_default_config.cpp
    unit/test_plan3_fix2.cpp
    unit/test_ipc.cpp
    unit/test_ipc_availability_win.cpp
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
    unit/test_wp03_models.cpp
    unit/test_wp03_save.cpp
    unit/test_wp04_lifecycle.cpp
    unit/test_tray_layout.cpp
    unit/test_tray_geometry.cpp
    unit/test_tray_infrastructure.cpp
    unit/test_tray_application.cpp
    unit/test_status_contract.cpp
    unit/test_winhttp_accounting.cpp
    ../apps/tray/status_model.cpp
    ../apps/tray/progress_model.cpp
    ../apps/tray/progress_details.cpp
    ../apps/tray/layout.cpp
    ../apps/tray/wizard_layout.cpp
    ../apps/tray/window_utils.cpp
    ../apps/tray/ui_resources.cpp
    ../apps/tray/ui_controls.cpp
    ../apps/tray/status_reader.cpp
    ../apps/tray/wizard_reader.cpp
    ../apps/tray/tray_application.cpp
    ../apps/tray/progress_window.cpp
    ../apps/tray/statistics_window.cpp
    ../apps/tray/editor_window.cpp
    ../apps/tray/wizard_window.cpp
    ../apps/tray/contracts_save_controller.cpp
    ../apps/tray/password_sha_window.cpp
    ../apps/tray/contracts_window.cpp
    ../apps/tray/tray.rc
)

target_include_directories(phpvpn-client-tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/unit
    ${CMAKE_SOURCE_DIR}/apps/service
)
target_link_libraries(phpvpn-client-tests PRIVATE phpvpn-platform phpvpn-core phpvpn-compile-options)

add_test(NAME phpvpn-client-tests COMMAND phpvpn-client-tests)

option(PHPVPN_BUILD_RESOURCE_TESTS "Build isolated maximum IPC memory and process-local DNS tests" OFF)
if(PHPVPN_BUILD_RESOURCE_TESTS)
    add_executable(phpvpn-resource-tests unit/harness.cpp unit/test_ipc_availability_win.cpp
        e2e/test_process_dns.cpp)
    target_compile_definitions(phpvpn-resource-tests PRIVATE PHPVPN_RESOURCE_TESTS)
    target_include_directories(phpvpn-resource-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/unit)
    target_link_libraries(phpvpn-resource-tests PRIVATE phpvpn-platform phpvpn-core phpvpn-compile-options psapi dnsapi)
    # Explicit isolated process, never runs in the production service or normal CTest.
endif()

option(PHPVPN_BUILD_CONTRACT_TESTS "Build real local PHP relay contract tests (server required)" OFF)
if(PHPVPN_BUILD_CONTRACT_TESTS)
    add_executable(phpvpn-contract-tests
        unit/harness.cpp
        contract/test_server_contract.cpp
    )
    target_include_directories(phpvpn-contract-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/unit)
    target_link_libraries(phpvpn-contract-tests PRIVATE phpvpn-platform phpvpn-core phpvpn-compile-options)
    add_test(NAME phpvpn-contract-tests COMMAND phpvpn-contract-tests)
    # Explicit opt-in only. An unavailable/misconfigured relay fails the test.
    # Serialize consumers of the destructive local fixture's .2-.15 queues.
    set_tests_properties(phpvpn-contract-tests PROPERTIES
        LABELS "contract;real-php"
        RUN_SERIAL TRUE
        RESOURCE_LOCK phpvpn_default_relay
        TIMEOUT 180)
endif()

option(PHPVPN_BUILD_SERVICE_E2E "Build isolated real SCM/Wintun integration targets (admin required)" OFF)
if(PHPVPN_BUILD_SERVICE_E2E)
    if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8 OR CMAKE_SYSTEM_PROCESSOR MATCHES "ARM64|arm64")
        message(FATAL_ERROR "The isolated service E2E target requires AMD64")
    endif()
    add_executable(phpvpn-e2e-service e2e/service_main.cpp ../apps/service/service_host.cpp)
    target_include_directories(phpvpn-e2e-service PRIVATE ${CMAKE_SOURCE_DIR}/apps/service)
    target_link_libraries(phpvpn-e2e-service PRIVATE phpvpn-platform phpvpn-core phpvpn-compile-options)
    add_custom_command(TARGET phpvpn-e2e-service POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/third_party/wintun/x64/wintun.dll"
            "$<TARGET_FILE_DIR:phpvpn-e2e-service>/wintun.dll")
    add_executable(phpvpn-service-e2e e2e/service_e2e.cpp ../apps/tray/status_model.cpp)
    target_include_directories(phpvpn-service-e2e PRIVATE ${CMAKE_SOURCE_DIR}/apps/tray)
    target_link_libraries(phpvpn-service-e2e PRIVATE phpvpn-platform phpvpn-core phpvpn-compile-options ws2_32 advapi32)
    add_dependencies(phpvpn-service-e2e phpvpn-e2e-service)
    # Explicit administrator opt-in, never part of unattended CTest.
endif()

option(PHPVPN_BUILD_UI_TESTS "Build explicit in-process Win32 tray UI tests" OFF)
if(PHPVPN_BUILD_UI_TESTS)
    add_executable(phpvpn-wp03-ui-tests
        unit/harness.cpp
        unit/test_wp03_windows.cpp
        unit/test_wp03_save_windows.cpp
        unit/test_wp05_windows.cpp
        ../apps/tray/tray_application.cpp
        ../apps/tray/wizard_reader.cpp
        ../apps/tray/wizard_layout.cpp
        ../apps/tray/wizard_window.cpp
        ../apps/tray/contracts_save_controller.cpp
        ../apps/tray/password_sha_window.cpp
        ../apps/tray/contracts_window.cpp
        ../apps/tray/progress_model.cpp
        ../apps/tray/progress_details.cpp
        ../apps/tray/progress_window.cpp
        ../apps/tray/statistics_window.cpp
        ../apps/tray/editor_window.cpp
        ../apps/tray/status_model.cpp
        ../apps/tray/status_reader.cpp
        ../apps/tray/layout.cpp
        ../apps/tray/window_utils.cpp
        ../apps/tray/ui_resources.cpp
        ../apps/tray/ui_controls.cpp
        ../apps/tray/tray.rc)
    target_include_directories(phpvpn-wp03-ui-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/unit)
    target_link_libraries(phpvpn-wp03-ui-tests PRIVATE phpvpn-platform phpvpn-core phpvpn-compile-options comctl32)
    target_link_options(phpvpn-wp03-ui-tests PRIVATE "/MANIFESTINPUT:${CMAKE_SOURCE_DIR}/apps/app.manifest")
    # Explicit native WP03 checks, never registered with unattended CTest.
    add_executable(phpvpn-tray-ui-tests
        ui/tray_ui_tests.cpp
        ui/dev_preview.inc
        ../apps/tray/tray_application.cpp
        ../apps/tray/window_utils.cpp
        ../apps/tray/ui_resources.cpp
        ../apps/tray/ui_controls.cpp
        ../apps/tray/progress_model.cpp
        ../apps/tray/progress_details.cpp
        ../apps/tray/progress_window.cpp
        ../apps/tray/statistics_window.cpp
        ../apps/tray/tray.rc
        ../apps/tray/layout.cpp
        ../apps/tray/status_model.cpp
        ../apps/tray/status_reader.cpp
        ../apps/tray/wizard_reader.cpp
        ../apps/tray/wizard_layout.cpp
        ../apps/tray/wizard_window.cpp
        ../apps/tray/editor_window.cpp
        ../apps/tray/contracts_save_controller.cpp
        ../apps/tray/password_sha_window.cpp
        ../apps/tray/contracts_window.cpp)
    target_include_directories(phpvpn-tray-ui-tests PRIVATE ${CMAKE_SOURCE_DIR}/apps/tray)
    target_link_libraries(phpvpn-tray-ui-tests PRIVATE phpvpn-platform phpvpn-core phpvpn-compile-options comctl32)
    target_link_options(phpvpn-tray-ui-tests PRIVATE "/MANIFESTINPUT:${CMAKE_SOURCE_DIR}/apps/app.manifest")
    # Deliberately not part of unattended CTest: this target opens real windows.
endif()
