# Every executable carries the same version block, the asInvoker manifest and
# the product icon, plan_client.md chapter 5. The manifest comes from the
# resource script, so the linker must not generate a second one.
string(REPLACE "." "," PHPVPN_RC_VERSION_COMMA "${PHPVPN_CLIENT_VERSION}.0")
set(PHPVPN_RC_VERSION "${PHPVPN_CLIENT_VERSION}")
set(PHPVPN_RC_MANIFEST "${CMAKE_CURRENT_SOURCE_DIR}/app.manifest")
set(PHPVPN_RC_ICON "${CMAKE_SOURCE_DIR}/assets/icons/shield_lock_green.ico")

function(phpvpn_add_version_resource target description internalName originalName withIcon)
    set(PHPVPN_RC_DESCRIPTION "${description}")
    set(PHPVPN_RC_INTERNAL_NAME "${internalName}")
    set(PHPVPN_RC_ORIGINAL_NAME "${originalName}")
    if(withIcon)
        set(PHPVPN_RC_HAS_ICON 1)
    else()
        set(PHPVPN_RC_HAS_ICON 0)
    endif()

    set(generated "${CMAKE_CURRENT_BINARY_DIR}/${target}-version.rc")
    configure_file("${CMAKE_CURRENT_SOURCE_DIR}/version.rc.in" "${generated}" @ONLY)
    target_sources(${target} PRIVATE "${generated}")
    if(MSVC)
        target_link_options(${target} PRIVATE /MANIFEST:NO)
    endif()
endfunction()

add_executable(phpvpn-service
    service/main.cpp
    service/service_host.cpp
)
target_link_libraries(phpvpn-service PRIVATE phpvpn-platform phpvpn-core phpvpn-compile-options)
set_target_properties(phpvpn-service PROPERTIES OUTPUT_NAME "phpvpn-service")
phpvpn_add_version_resource(phpvpn-service "phpvpn client service" "phpvpn-service"
                            "phpvpn-service.exe" TRUE)

add_executable(phpvpn-cli
    cli/main.cpp
)
target_link_libraries(phpvpn-cli PRIVATE phpvpn-platform phpvpn-core phpvpn-compile-options)
set_target_properties(phpvpn-cli PROPERTIES OUTPUT_NAME "phpvpn")
phpvpn_add_version_resource(phpvpn-cli "phpvpn console client" "phpvpn" "phpvpn.exe" TRUE)

add_executable(phpvpn-tray WIN32
    tray/main.cpp
    tray/tray_application.cpp
    tray/system_dialog.cpp
    tray/window_utils.cpp
    tray/ui_resources.cpp
    tray/ui_controls.cpp
    tray/status_model.cpp
    tray/progress_model.cpp
    tray/progress_details.cpp
    tray/layout.cpp
    tray/status_reader.cpp
    tray/wizard_reader.cpp
    tray/wizard_window.cpp
    tray/http_error_detail_window.cpp
    tray/wizard_layout.cpp
    tray/progress_window.cpp
    tray/statistics_window.cpp
    tray/server_info_window.cpp
    tray/editor_window.cpp
    tray/contracts_save_controller.cpp
    tray/password_sha_window.cpp
    tray/contracts_window.cpp
    tray/tray.rc
)
target_include_directories(phpvpn-tray PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tray)
target_link_libraries(phpvpn-tray PRIVATE phpvpn-platform phpvpn-core phpvpn-compile-options comctl32)
set_target_properties(phpvpn-tray PROPERTIES OUTPUT_NAME "phpvpn-tray")
# The tray already declares the three state icons, so the shared resource only
# adds the version block and the manifest that enables the visual styles.
phpvpn_add_version_resource(phpvpn-tray "phpvpn tray client" "phpvpn-tray" "phpvpn-tray.exe" FALSE)

# Wintun ships as an architecture specific binary that must keep its own name.
if(CMAKE_SIZEOF_VOID_P EQUAL 8 AND CMAKE_SYSTEM_PROCESSOR MATCHES "ARM64|arm64")
    set(PHPVPN_WINTUN_DIR "arm64")
elseif(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(PHPVPN_WINTUN_DIR "x64")
else()
    set(PHPVPN_WINTUN_DIR "x86")
endif()

add_custom_command(TARGET phpvpn-service POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/third_party/wintun/${PHPVPN_WINTUN_DIR}/wintun.dll"
        "$<TARGET_FILE_DIR:phpvpn-service>/wintun.dll"
    COMMENT "Copying the tunnel driver library next to the service"
)
