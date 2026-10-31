# Portable unit tests (plan 6.1). They compile against the core, the shared
# platform interface sources and the portable tray models only, and run on every
# platform. The Windows suite phpvpn-client-tests below keeps its own explicit list.
set(PHPVPN_PORTABLE_TEST_SOURCES
    unit/test_base64url.cpp
    unit/test_config.cpp
    unit/test_contracts_save_machine.cpp
    unit/test_http_diagnostic.cpp
    unit/test_ipc.cpp
    unit/test_iso8601.cpp
    unit/test_json.cpp
    unit/test_payload_batch.cpp
    unit/test_queue_drr.cpp
    unit/test_queue_expiry.cpp
    unit/test_queues.cpp
    unit/test_server_info_model.cpp
    unit/test_server_info_layout.cpp
    unit/test_server_version.cpp
    unit/test_status_contract.cpp
    unit/test_time_sync.cpp
    unit/test_traffic_counters.cpp
    unit/test_tunnel_queues.cpp
    unit/test_tray_geometry.cpp
    unit/test_tray_layout.cpp
    unit/test_tray_arguments.cpp
    unit/test_tray_progress.cpp
    unit/test_tray_status.cpp
    unit/test_utf8.cpp
    unit/test_wizard_ipc.cpp
    unit/test_wizard_layout.cpp
    unit/test_wizard_model.cpp
    ../apps/tray/status_model.cpp
    ../apps/tray/progress_model.cpp
    ../apps/tray/progress_details.cpp
    ../apps/tray/layout.cpp
    ../apps/tray/wizard_layout.cpp
    ../apps/tray/save_response.cpp
    ../apps/tray/contracts_save_machine.cpp
)

# Portable tests that need createPlatformCryptoProvider. They join the portable
# suite wherever a platform library exists; Linux gets it with WP02.
set(PHPVPN_PORTABLE_CRYPTO_TEST_SOURCES
    unit/test_config_store.cpp
    unit/test_contracts_document.cpp
    unit/test_contracts_model.cpp
    unit/test_crypto.cpp
    unit/test_flow_key.cpp
    unit/test_http_headers.cpp
    unit/test_info_extensions.cpp
    unit/test_password_key.cpp
    unit/test_plan3_fix2.cpp
    unit/test_wizard_document.cpp
    unit/test_wp03_models.cpp
)

add_executable(phpvpn-core-tests unit/harness.cpp ${PHPVPN_PORTABLE_TEST_SOURCES})
target_include_directories(phpvpn-core-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/unit)
target_link_libraries(phpvpn-core-tests PRIVATE phpvpn-core phpvpn-compile-options)
# PHPVPN_LINUX_CRYPTO_READY was switched on by WP02 with the OpenSSL provider;
# macOS shares that provider.
set(PHPVPN_LINUX_CRYPTO_READY ON)
if(TARGET phpvpn-platform AND (WIN32 OR PHPVPN_LINUX_CRYPTO_READY))
    target_sources(phpvpn-core-tests PRIVATE ${PHPVPN_PORTABLE_CRYPTO_TEST_SOURCES})
    target_link_libraries(phpvpn-core-tests PRIVATE phpvpn-platform)
endif()
add_test(NAME phpvpn-core-tests COMMAND phpvpn-core-tests)
set_tests_properties(phpvpn-core-tests PROPERTIES LABELS "portable")

# Isolate the singleton logger and its temporary filesystem fixture from other suites.
add_executable(phpvpn-logger-tests unit/harness.cpp unit/test_logger.cpp)
target_include_directories(phpvpn-logger-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/unit)
target_link_libraries(phpvpn-logger-tests PRIVATE phpvpn-core phpvpn-compile-options)
add_test(NAME phpvpn-logger-tests COMMAND phpvpn-logger-tests)
set_tests_properties(phpvpn-logger-tests PROPERTIES TIMEOUT 30)

# Real relay contract tests on every platform (Windows: WinHTTP/CNG, Linux:
# libcurl/OpenSSL), see tools/run-server-contract.ps1 and
# tools/linux/run-server-contract.sh.
option(PHPVPN_BUILD_CONTRACT_TESTS "Build real local relay contract tests (server required)" OFF)
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

if(PHPVPN_PLATFORM STREQUAL "macos")
    # macOS platform layer tests that need no privileges (plan-macos-v001
    # step 7). The clock, configuration file, crypto, transport and IPC tests
    # are the portable POSIX tests of the Linux suite.
    add_executable(phpvpn-macos-tests
        unit/harness.cpp
        linux/unit/test_clock_linux.cpp
        linux/unit/test_config_files_linux.cpp
        linux/unit/test_crypto_openssl.cpp
        linux/unit/test_transport_curl.cpp
        linux/unit/test_ipc_unix.cpp
        macos/unit/test_paths_macos.cpp
        macos/unit/test_route_codec.cpp
        macos/unit/test_interface_identity_macos.cpp
        macos/unit/test_dns_snapshot_macos.cpp
    )
    target_include_directories(phpvpn-macos-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/unit)
    # The transport test hosts its own TLS server, so it needs libssl of the
    # same static OpenSSL.
    find_package(OpenSSL 3 REQUIRED COMPONENTS SSL Crypto)
    target_link_libraries(phpvpn-macos-tests PRIVATE phpvpn-platform phpvpn-core phpvpn-compile-options
        OpenSSL::SSL OpenSSL::Crypto)
    add_test(NAME phpvpn-macos-tests COMMAND phpvpn-macos-tests)
    set_tests_properties(phpvpn-macos-tests PROPERTIES LABELS "macos" TIMEOUT 300)

    # Root only: creates a utun interface, assigns the address and the route,
    # exchanges packets and verifies the cleanup. CI runs it with sudo.
    option(PHPVPN_BUILD_PRIVILEGED_TESTS "Build macOS utun tests (root required)" ON)
    if(PHPVPN_BUILD_PRIVILEGED_TESTS)
        add_executable(phpvpn-utun-tests
            unit/harness.cpp
            macos/privileged/test_tunnel_utun.cpp
        )
        target_include_directories(phpvpn-utun-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/unit)
        target_link_libraries(phpvpn-utun-tests PRIVATE phpvpn-platform phpvpn-core phpvpn-compile-options)
        add_test(NAME phpvpn-utun-tests COMMAND phpvpn-utun-tests)
        set_tests_properties(phpvpn-utun-tests PROPERTIES LABELS "privileged" TIMEOUT 300)
    endif()

    # Helper of the root-only service end-to-end script tests/e2e/macos, shared
    # with Linux through the forwarding IPC header.
    option(PHPVPN_BUILD_MACOS_E2E "Build the macOS service end-to-end test helpers" ON)
    if(PHPVPN_BUILD_MACOS_E2E)
        add_executable(phpvpn-wizard-ipc-probe e2e/linux/wizard_ipc_probe.cpp)
        target_link_libraries(phpvpn-wizard-ipc-probe PRIVATE phpvpn-platform phpvpn-core phpvpn-compile-options)
    endif()
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    # Linux platform layer tests that need no privileges. Each work package owns
    # its own test file (plan-v001 section 9.4).
    add_executable(phpvpn-linux-tests
        unit/harness.cpp
        linux/unit/test_clock_linux.cpp
        linux/unit/test_config_files_linux.cpp
        linux/unit/test_crypto_openssl.cpp
        linux/unit/test_transport_curl.cpp
        linux/unit/test_netlink_codec.cpp
        linux/unit/test_ipc_unix.cpp
        linux/unit/test_fd_tunnel.cpp
        linux/unit/test_paths_trust_root.cpp
        # GTK-free models of the Linux tray (WP09); no GUI library is linked.
        linux/unit/test_tray_linux_models.cpp
        ../apps/tray-linux/indicator_model.cpp
        ../apps/tray-linux/ipc_async.cpp
        ../apps/tray/status_model.cpp
        ../apps/tray/save_response.cpp
        # Server information and Statistics logic (WP10c).
        linux/unit/test_tray_linux_info.cpp
        ../apps/tray-linux/server_info_presenter.cpp
        # Edit Config, Communication contracts and Password SHA logic (WP10b).
        linux/unit/test_tray_linux_config.cpp
        ../apps/tray-linux/editor_model.cpp
        ../apps/tray-linux/contracts_view.cpp
        ../apps/tray/contracts_save_machine.cpp
        # GTK-free Wizard controller and dialog texts (WP10a).
        linux/unit/test_tray_linux_wizard.cpp
        ../apps/tray-linux/wizard_controller.cpp
        ../apps/tray-linux/wizard_dialog_text.cpp
    )
    target_include_directories(phpvpn-linux-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/unit
        ${CMAKE_SOURCE_DIR}/apps/tray ${CMAKE_SOURCE_DIR}/apps/tray-linux)
    # The transport tests run an in-process HTTPS server with a generated certificate.
    find_package(OpenSSL 3 REQUIRED COMPONENTS SSL Crypto)
    target_link_libraries(phpvpn-linux-tests PRIVATE phpvpn-platform phpvpn-core phpvpn-compile-options
        OpenSSL::SSL OpenSSL::Crypto)
    add_test(NAME phpvpn-linux-tests COMMAND phpvpn-linux-tests)
    set_tests_properties(phpvpn-linux-tests PROPERTIES LABELS "linux" TIMEOUT 300)

    # Tests that create TUN interfaces, addresses and routes. They need root and
    # run inside a private network namespace, see tests/linux/netns/README.md.
    option(PHPVPN_BUILD_PRIVILEGED_TESTS "Build Linux network namespace tests (root required)" ON)
    if(PHPVPN_BUILD_PRIVILEGED_TESTS)
        add_executable(phpvpn-netns-tests
            unit/harness.cpp
            linux/netns/test_tunnel_netns.cpp
        )
        target_include_directories(phpvpn-netns-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/unit)
        target_link_libraries(phpvpn-netns-tests PRIVATE phpvpn-platform phpvpn-core phpvpn-compile-options)
        # Not registered with ctest: the runner script provides root and the namespace.
    endif()

    # Helpers of the root-only service end-to-end scripts in tests/e2e/linux.
    # Built by default, never registered with ctest.
    option(PHPVPN_BUILD_LINUX_E2E "Build the Linux service end-to-end test helpers" ON)
    if(PHPVPN_BUILD_LINUX_E2E)
        add_executable(phpvpn-wizard-ipc-probe e2e/linux/wizard_ipc_probe.cpp)
        target_link_libraries(phpvpn-wizard-ipc-probe PRIVATE phpvpn-platform phpvpn-core phpvpn-compile-options)
    endif()
    return()
endif()

if(NOT WIN32)
    return()
endif()

# Windows only from here on: the full Win32 client suite, resources, package and
# opt-in integration targets.
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
    unit/test_http_headers.cpp
    unit/test_http_diagnostic.cpp
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
    unit/test_flow_key.cpp
    unit/test_queue_expiry.cpp
    unit/test_queue_drr.cpp
    unit/test_queue_concurrency.cpp
    unit/test_tunnel_queues.cpp
    unit/test_queues.cpp
    unit/test_service_registration.cpp
    unit/test_tray_autostart.cpp
    ../apps/service/startup_connect.cpp
    unit/test_time_sync.cpp
    unit/test_wire.cpp
    unit/test_info_extensions.cpp
    unit/test_server_info_model.cpp
    unit/test_server_info_layout.cpp
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
    unit/test_tray_palette.cpp
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
    ../apps/tray/save_response.cpp
    ../apps/tray/wizard_reader.cpp
    ../apps/tray/tray_application.cpp
    ../apps/tray/system_dialog.cpp
    ../apps/tray/progress_window.cpp
    ../apps/tray/statistics_window.cpp
    ../apps/tray/server_info_window.cpp
    ../apps/tray/editor_window.cpp
    ../apps/tray/wizard_window.cpp
    ../apps/tray/http_headers_window.cpp
    ../apps/tray/http_headers_layout.cpp
    ../apps/tray/http_error_detail_window.cpp
    ../apps/tray/contracts_save_controller.cpp
    ../apps/tray/contracts_save_machine.cpp
    ../apps/tray/password_sha_window.cpp
    ../apps/tray/contracts_window.cpp

)

phpvpn_add_tray_resources(phpvpn-client-tests)
target_include_directories(phpvpn-client-tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/unit
    ${CMAKE_SOURCE_DIR}/apps/service
)
target_link_libraries(phpvpn-client-tests PRIVATE phpvpn-platform phpvpn-core phpvpn-compile-options comctl32)
target_link_options(phpvpn-client-tests PRIVATE "/MANIFESTINPUT:${CMAKE_SOURCE_DIR}/apps/app.manifest")

add_test(NAME phpvpn-client-tests COMMAND phpvpn-client-tests)

add_executable(phpvpn-package-gui-tests unit/harness.cpp unit/test_package_gui.cpp)
target_link_libraries(phpvpn-package-gui-tests PRIVATE phpvpn-package-runner)
add_test(NAME phpvpn-package-gui-tests COMMAND phpvpn-package-gui-tests)
set_tests_properties(phpvpn-package-gui-tests PROPERTIES TIMEOUT 60 LABELS "package;isolated")

add_executable(phpvpn-tray-registry-tests resources/tray_registry.cpp)
target_link_libraries(phpvpn-tray-registry-tests PRIVATE phpvpn-platform phpvpn-compile-options)
add_test(NAME phpvpn-tray-registry-tests COMMAND phpvpn-tray-registry-tests)
set_tests_properties(phpvpn-tray-registry-tests PROPERTIES LABELS "registry;isolated")
add_executable(phpvpn-icon-resource-tests resources/icon_resources.cpp)
target_link_libraries(phpvpn-icon-resource-tests PRIVATE phpvpn-compile-options user32 version)
add_dependencies(phpvpn-icon-resource-tests phpvpn-service phpvpn-tray phpvpn-cli phpvpn-install phpvpn-uninstall)
add_test(NAME phpvpn-icon-resource-tests COMMAND phpvpn-icon-resource-tests
    "$<TARGET_FILE_DIR:phpvpn-service>" "${CMAKE_SOURCE_DIR}/../assets")
set_tests_properties(phpvpn-icon-resource-tests PROPERTIES LABELS "icons;resources")

if(WIN32)
    find_program(PHPVPN_POWERSHELL powershell.exe REQUIRED)
    foreach(package_test IN ITEMS upgrade feedback gui distribution)
        add_test(NAME phpvpn-package-${package_test}
            COMMAND ${PHPVPN_POWERSHELL} -NoProfile -ExecutionPolicy Bypass
                -File ${CMAKE_CURRENT_SOURCE_DIR}/test_package_${package_test}.ps1)
        set_tests_properties(phpvpn-package-${package_test} PROPERTIES LABELS "package" TIMEOUT 60)
    endforeach()
endif()

option(PHPVPN_BUILD_RESOURCE_TESTS "Build isolated maximum IPC memory and process-local DNS tests" OFF)
if(PHPVPN_BUILD_RESOURCE_TESTS)
    add_executable(phpvpn-resource-tests unit/harness.cpp unit/test_ipc_availability_win.cpp
        e2e/test_process_dns.cpp)
    target_compile_definitions(phpvpn-resource-tests PRIVATE PHPVPN_RESOURCE_TESTS)
    target_include_directories(phpvpn-resource-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/unit)
    target_link_libraries(phpvpn-resource-tests PRIVATE phpvpn-platform phpvpn-core phpvpn-compile-options psapi dnsapi)
    # Explicit isolated process, never runs in the production service or normal CTest.
endif()

option(PHPVPN_BUILD_SERVICE_E2E "Build isolated real SCM/Wintun integration targets (admin required)" OFF)
if(PHPVPN_BUILD_SERVICE_E2E)
    if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8 OR CMAKE_SYSTEM_PROCESSOR MATCHES "ARM64|arm64")
        message(FATAL_ERROR "The isolated service E2E target requires AMD64")
    endif()
    add_executable(phpvpn-e2e-service e2e/service_main.cpp ../apps/service/service_host_win.cpp
        ../apps/service/service_runtime.cpp ../apps/service/startup_connect.cpp)
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
    add_executable(phpvpn-installation-notice-probe ui/installation_notice_probe.cpp)
    target_include_directories(phpvpn-installation-notice-probe PRIVATE ${CMAKE_SOURCE_DIR}/apps/tray)
    target_link_libraries(phpvpn-installation-notice-probe PRIVATE phpvpn-core phpvpn-compile-options shell32 user32)
    phpvpn_add_tray_resources(phpvpn-installation-notice-probe)
    # Opt-in native Shell display check, excluded from unattended CTest.
    add_executable(phpvpn-package-gui-ui-tests ui/package_gui_tests.cpp)
    target_link_libraries(phpvpn-package-gui-ui-tests PRIVATE phpvpn-package-window)
    phpvpn_add_version_resource(phpvpn-package-gui-ui-tests "phpvpn package GUI test host"
        "phpvpn-package-gui-ui-tests" "phpvpn-package-gui-ui-tests.exe" TRUE
        ICON "${CMAKE_SOURCE_DIR}/../assets/connected.ico")
    # Explicit interactive test only. This host remains asInvoker.
    add_executable(phpvpn-wp03-ui-tests
        unit/harness.cpp
        unit/test_wp03_windows.cpp
        unit/test_wp03_save_windows.cpp
        unit/test_wp05_windows.cpp
        ../apps/tray/tray_application.cpp
        ../apps/tray/system_dialog.cpp
        ../apps/tray/wizard_reader.cpp
        ../apps/tray/wizard_layout.cpp
        ../apps/tray/wizard_window.cpp
        ../apps/tray/http_headers_window.cpp
        ../apps/tray/http_headers_layout.cpp
        ../apps/tray/http_error_detail_window.cpp
        ../apps/tray/contracts_save_controller.cpp
        ../apps/tray/contracts_save_machine.cpp
        ../apps/tray/password_sha_window.cpp
        ../apps/tray/contracts_window.cpp
        ../apps/tray/progress_model.cpp
        ../apps/tray/progress_details.cpp
        ../apps/tray/progress_window.cpp
        ../apps/tray/statistics_window.cpp
        ../apps/tray/server_info_window.cpp
        ../apps/tray/editor_window.cpp
        ../apps/tray/status_model.cpp
        ../apps/tray/status_reader.cpp
        ../apps/tray/save_response.cpp
        ../apps/tray/layout.cpp
        ../apps/tray/window_utils.cpp
        ../apps/tray/ui_resources.cpp
        ../apps/tray/ui_controls.cpp
    )
    phpvpn_add_tray_resources(phpvpn-wp03-ui-tests)
    target_include_directories(phpvpn-wp03-ui-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/unit)
    target_link_libraries(phpvpn-wp03-ui-tests PRIVATE phpvpn-platform phpvpn-core phpvpn-compile-options comctl32)
    target_link_options(phpvpn-wp03-ui-tests PRIVATE "/MANIFESTINPUT:${CMAKE_SOURCE_DIR}/apps/app.manifest")
    # Explicit native WP03 checks, never registered with unattended CTest.
    add_executable(phpvpn-tray-ui-tests
        ui/tray_ui_tests.cpp
        ui/dev_preview.inc
        ../apps/tray/tray_application.cpp
        ../apps/tray/system_dialog.cpp
        ../apps/tray/window_utils.cpp
        ../apps/tray/ui_resources.cpp
        ../apps/tray/ui_controls.cpp
        ../apps/tray/progress_model.cpp
        ../apps/tray/progress_details.cpp
        ../apps/tray/progress_window.cpp
        ../apps/tray/statistics_window.cpp

        ../apps/tray/layout.cpp
        ../apps/tray/status_model.cpp
        ../apps/tray/status_reader.cpp
        ../apps/tray/save_response.cpp
        ../apps/tray/wizard_reader.cpp
        ../apps/tray/wizard_layout.cpp
        ../apps/tray/wizard_window.cpp
        ../apps/tray/http_headers_window.cpp
        ../apps/tray/http_headers_layout.cpp
        ../apps/tray/http_error_detail_window.cpp
        ../apps/tray/server_info_window.cpp
    ../apps/tray/editor_window.cpp
        ../apps/tray/contracts_save_controller.cpp
        ../apps/tray/contracts_save_machine.cpp
        ../apps/tray/password_sha_window.cpp
        ../apps/tray/contracts_window.cpp)
    phpvpn_add_tray_resources(phpvpn-tray-ui-tests)
    target_include_directories(phpvpn-tray-ui-tests PRIVATE ${CMAKE_SOURCE_DIR}/apps/tray)
    target_link_libraries(phpvpn-tray-ui-tests PRIVATE phpvpn-platform phpvpn-core phpvpn-compile-options comctl32)
    target_link_options(phpvpn-tray-ui-tests PRIVATE "/MANIFESTINPUT:${CMAKE_SOURCE_DIR}/apps/app.manifest")
    # Deliberately not part of unattended CTest: this target opens real windows.
endif()
