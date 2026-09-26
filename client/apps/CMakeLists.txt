if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    # Linux: the systemd service and the CLI (plan-v001 sections 4.10 and 4.11).
    # The GTK tray (PHPVPN_BUILD_TRAY) lives in tray-linux, added further below.
    include(GNUInstallDirs)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(PHPVPN_APP_SYSTEMD REQUIRED IMPORTED_TARGET libsystemd)

    add_executable(phpvpn-service
        service/service_main_linux.cpp
        service/service_runtime.cpp
        service/startup_connect.cpp
    )
    target_link_libraries(phpvpn-service PRIVATE phpvpn-platform phpvpn-core phpvpn-compile-options
        PkgConfig::PHPVPN_APP_SYSTEMD)
    set_target_properties(phpvpn-service PROPERTIES OUTPUT_NAME "phpvpn-service")

    add_executable(phpvpn-cli
        cli/main.cpp
    )
    target_link_libraries(phpvpn-cli PRIVATE phpvpn-platform phpvpn-core phpvpn-compile-options)
    set_target_properties(phpvpn-cli PROPERTIES OUTPUT_NAME "phpvpn")

    # The unit starts /usr/libexec/phpvpn/phpvpn-service, the location of the
    # distribution packages (prefix /usr).
    set(PHPVPN_SYSTEMD_UNIT_DIR "lib/systemd/system" CACHE STRING
        "systemd system unit directory, relative to CMAKE_INSTALL_PREFIX or absolute")
    set(PHPVPN_NM_CONF_DIR "lib/NetworkManager/conf.d" CACHE STRING
        "NetworkManager vendor configuration directory, relative to CMAKE_INSTALL_PREFIX or absolute")
    if(NOT CMAKE_INSTALL_FULL_LIBEXECDIR STREQUAL "/usr/libexec")
        message(WARNING "phpvpn.service expects /usr/libexec/phpvpn/phpvpn-service, "
                        "but the service installs to ${CMAKE_INSTALL_FULL_LIBEXECDIR}/phpvpn")
    endif()
    set(PHPVPN_LINUX_PACKAGING "${CMAKE_SOURCE_DIR}/packaging/linux")
    install(TARGETS phpvpn-service RUNTIME DESTINATION "${CMAKE_INSTALL_LIBEXECDIR}/phpvpn")
    install(TARGETS phpvpn-cli RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
    install(FILES "${PHPVPN_LINUX_PACKAGING}/systemd/phpvpn.service" DESTINATION "${PHPVPN_SYSTEMD_UNIT_DIR}")
    install(FILES "${PHPVPN_LINUX_PACKAGING}/NetworkManager/90-phpvpn.conf" DESTINATION "${PHPVPN_NM_CONF_DIR}")
    install(FILES "${PHPVPN_LINUX_PACKAGING}/man/phpvpn.1" DESTINATION "${CMAKE_INSTALL_MANDIR}/man1")
    install(FILES "${PHPVPN_LINUX_PACKAGING}/man/phpvpn-service.8" DESTINATION "${CMAKE_INSTALL_MANDIR}/man8")

    if(PHPVPN_BUILD_TRAY)
        add_subdirectory(tray-linux)
    endif()
    return()
endif()

# Everything below is the Windows application set: Win32 resources, manifests, the tray,
# the package GUI and the Wintun copy.
if(NOT WIN32)
    return()
endif()

# Executables carry a version block and per-target manifest/icon. The manifest comes from the
# resource script, so the linker must not generate a second one.
get_filename_component(PHPVPN_ICON_DIRECTORY "${CMAKE_SOURCE_DIR}/../assets" ABSOLUTE)
foreach(icon IN ITEMS default connected connecting disconnected)
    if(NOT EXISTS "${PHPVPN_ICON_DIRECTORY}/${icon}.ico")
        message(FATAL_ERROR "Missing application icon: ${icon}.ico")
    endif()
endforeach()
# Shared by the production tray and every native UI test host.
function(phpvpn_add_tray_resources target)
    set_property(SOURCE "${CMAKE_SOURCE_DIR}/apps/tray/tray.rc" APPEND PROPERTY OBJECT_DEPENDS
        "${CMAKE_SOURCE_DIR}/../assets/default.ico;${CMAKE_SOURCE_DIR}/../assets/disconnected.ico;${CMAKE_SOURCE_DIR}/../assets/connecting.ico;${CMAKE_SOURCE_DIR}/../assets/connected.ico")
    target_sources(${target} PRIVATE "${CMAKE_SOURCE_DIR}/apps/tray/tray.rc")
    target_include_directories(${target} PRIVATE "${CMAKE_SOURCE_DIR}/../assets")
endfunction()

function(phpvpn_add_version_resource target description internalName originalName withIcon)
    string(REPLACE "." "," PHPVPN_RC_VERSION_COMMA "${PHPVPN_CLIENT_VERSION}.0")
    set(PHPVPN_RC_VERSION "${PHPVPN_CLIENT_VERSION}")
    set(PHPVPN_RC_MANIFEST "${CMAKE_SOURCE_DIR}/apps/app.manifest")
    set(PHPVPN_RC_ICON "${CMAKE_SOURCE_DIR}/../assets/default.ico")
    cmake_parse_arguments(RESOURCE "" "MANIFEST;ICON" "" ${ARGN})
    if(RESOURCE_MANIFEST)
        set(PHPVPN_RC_MANIFEST "${RESOURCE_MANIFEST}")
    endif()
    if(RESOURCE_ICON)
        set(PHPVPN_RC_ICON "${RESOURCE_ICON}")
    endif()
    set(PHPVPN_RC_DESCRIPTION "${description}")
    set(PHPVPN_RC_INTERNAL_NAME "${internalName}")
    set(PHPVPN_RC_ORIGINAL_NAME "${originalName}")
    if(withIcon)
        set(PHPVPN_RC_HAS_ICON 1)
    else()
        set(PHPVPN_RC_HAS_ICON 0)
    endif()

    set(generated "${CMAKE_CURRENT_BINARY_DIR}/${target}-version.rc")
    configure_file("${CMAKE_SOURCE_DIR}/apps/version.rc.in" "${generated}" @ONLY)
    if(withIcon)
        set_property(SOURCE "${generated}" PROPERTY OBJECT_DEPENDS "${PHPVPN_RC_ICON}")
    endif()
    target_sources(${target} PRIVATE "${generated}")
    if(MSVC)
        target_link_options(${target} PRIVATE /MANIFEST:NO)
    endif()
endfunction()

add_executable(phpvpn-service
    service/main.cpp
    service/service_host_win.cpp
    service/service_runtime.cpp
    service/startup_connect.cpp
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
    tray/save_response.cpp
    tray/wizard_reader.cpp
    tray/wizard_window.cpp
    tray/http_headers_window.cpp
    tray/http_headers_layout.cpp
    tray/http_error_detail_window.cpp
    tray/wizard_layout.cpp
    tray/progress_window.cpp
    tray/statistics_window.cpp
    tray/server_info_window.cpp
    tray/editor_window.cpp
    tray/contracts_save_controller.cpp
    tray/contracts_save_machine.cpp
    tray/password_sha_window.cpp
    tray/contracts_window.cpp
)
phpvpn_add_tray_resources(phpvpn-tray)
target_include_directories(phpvpn-tray PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tray)
target_link_libraries(phpvpn-tray PRIVATE phpvpn-platform phpvpn-core phpvpn-compile-options comctl32)
set_target_properties(phpvpn-tray PROPERTIES OUTPUT_NAME "phpvpn-tray")
# The tray already declares the three state icons, so the shared resource only
# adds the version block and the manifest that enables the visual styles.
phpvpn_add_version_resource(phpvpn-tray "phpvpn tray client" "phpvpn-tray" "phpvpn-tray.exe" FALSE)

add_library(phpvpn-package-runner STATIC package_gui/script_runner.cpp)
target_include_directories(phpvpn-package-runner PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/package_gui")
target_link_libraries(phpvpn-package-runner PUBLIC phpvpn-compile-options)
add_library(phpvpn-package-window STATIC package_gui/package_window.cpp tray/window_utils.cpp tray/layout.cpp
    tray/ui_controls.cpp tray/ui_resources.cpp)
target_link_libraries(phpvpn-package-window PUBLIC phpvpn-package-runner comctl32 shell32 ole32 oleaut32 oleacc user32 gdi32 uxtheme)
foreach(mode IN ITEMS install uninstall)
    add_executable(phpvpn-${mode} WIN32 package_gui/main.cpp)
    target_link_libraries(phpvpn-${mode} PRIVATE phpvpn-package-window)
    set_target_properties(phpvpn-${mode} PROPERTIES OUTPUT_NAME "${mode}-phpvpn")
    if(mode STREQUAL "install")
        set(package_icon connected)
    else()
        set(package_icon disconnected)
        target_compile_definitions(phpvpn-${mode} PRIVATE PHPVPN_PACKAGE_UNINSTALL)
    endif()
    phpvpn_add_version_resource(phpvpn-${mode} "phpvpn ${mode} launcher" "${mode}-phpvpn" "${mode}-phpvpn.exe" TRUE
        MANIFEST "${CMAKE_CURRENT_SOURCE_DIR}/package_gui/admin.manifest"
        ICON "${PHPVPN_ICON_DIRECTORY}/${package_icon}.ico")
endforeach()

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
