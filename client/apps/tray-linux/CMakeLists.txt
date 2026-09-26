# GTK 3 + libayatana-appindicator3 tray (plan-v001 section 4.12). Built on Linux when
# PHPVPN_BUILD_TRAY is ON; the linux-headless preset switches it off so a headless
# build needs no GUI headers.
include(GNUInstallDirs)
find_package(PkgConfig REQUIRED)
pkg_check_modules(PHPVPN_TRAY_GTK REQUIRED IMPORTED_TARGET gtk+-3.0 ayatana-appindicator3-0.1)

set(PHPVPN_TRAY_SHARED_DIR "${CMAKE_SOURCE_DIR}/apps/tray")
set(PHPVPN_LINUX_ICON_DIR "${CMAKE_SOURCE_DIR}/assets/linux/hicolor")
set(PHPVPN_LINUX_DESKTOP_DIR "${CMAKE_SOURCE_DIR}/packaging/linux/desktop")
set(PHPVPN_LINUX_ICON_SIZES 16 22 24 32 48 64 128)
set(PHPVPN_LINUX_ICON_NAMES phpvpn phpvpn-disconnected phpvpn-connecting phpvpn-connected)

add_executable(phpvpn-tray
    main.cpp
    tray_app.cpp
    indicator.cpp
    indicator_model.cpp
    ipc_async.cpp
    ipc_async_glib.cpp
    progress_window.cpp
    control_window.cpp
    notifications.cpp
    dialogs.cpp
    style.cpp
    server_info_window.cpp
    server_info_presenter.cpp
    statistics_window.cpp
    editor_model.cpp
    editor_window.cpp
    contracts_view.cpp
    contracts_window.cpp
    password_sha_window.cpp
    wizard_window.cpp
    wizard_controller.cpp
    wizard_dialog_text.cpp
    wizard_style.cpp
    http_headers_window.cpp
    http_error_detail_window.cpp
    ${PHPVPN_TRAY_SHARED_DIR}/status_model.cpp
    ${PHPVPN_TRAY_SHARED_DIR}/progress_model.cpp
    ${PHPVPN_TRAY_SHARED_DIR}/progress_details.cpp
    ${PHPVPN_TRAY_SHARED_DIR}/save_response.cpp
    ${PHPVPN_TRAY_SHARED_DIR}/contracts_save_machine.cpp
)
target_include_directories(phpvpn-tray PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PHPVPN_TRAY_SHARED_DIR})
target_link_libraries(phpvpn-tray PRIVATE
    phpvpn-platform phpvpn-core phpvpn-compile-options PkgConfig::PHPVPN_TRAY_GTK)

# A non-installed run finds the generated icons in <build>/icons (tray_app.cpp).
add_custom_command(TARGET phpvpn-tray POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${PHPVPN_LINUX_ICON_DIR}"
        "$<TARGET_FILE_DIR:phpvpn-tray>/icons/hicolor"
    COMMENT "Copying the tray icon theme next to the development tray")

install(TARGETS phpvpn-tray RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
foreach(size IN LISTS PHPVPN_LINUX_ICON_SIZES)
    foreach(name IN LISTS PHPVPN_LINUX_ICON_NAMES)
        set(icon "${PHPVPN_LINUX_ICON_DIR}/${size}x${size}/apps/${name}.png")
        if(NOT EXISTS "${icon}")
            message(FATAL_ERROR "Missing generated tray icon ${icon}; run client/tools/make_linux_icons.py")
        endif()
        install(FILES "${icon}" DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/${size}x${size}/apps")
    endforeach()
endforeach()
install(FILES "${PHPVPN_LINUX_DESKTOP_DIR}/phpvpn-tray.desktop" DESTINATION "${CMAKE_INSTALL_DATADIR}/applications")
# XDG autostart for every user, the counterpart of the Windows HKLM Run value.
install(FILES "${PHPVPN_LINUX_DESKTOP_DIR}/phpvpn-tray-autostart.desktop"
    DESTINATION "${CMAKE_INSTALL_FULL_SYSCONFDIR}/xdg/autostart" RENAME "phpvpn-tray.desktop")
install(FILES "${CMAKE_SOURCE_DIR}/packaging/linux/man/phpvpn-tray.1" DESTINATION "${CMAKE_INSTALL_MANDIR}/man1")
# The state icons derive from Material Symbols (Apache-2.0): licence and notice
# travel with them (RPM %license; the Debian package uses debian/copyright).
install(FILES "${CMAKE_SOURCE_DIR}/assets/icons/LICENSE-material-symbols.txt"
              "${CMAKE_SOURCE_DIR}/assets/icons/NOTICE-material-symbols.txt"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/licenses/phpvpn-tray")

# Development harness only: scripted phpvpn service on a private socket for the
# tray (tests/linux/tray/README.md). Never installed or packaged.
if(BUILD_TESTING)
    add_executable(phpvpn-tray-mock-service "${CMAKE_SOURCE_DIR}/tests/linux/tray/mock_service.cpp"
        "${CMAKE_SOURCE_DIR}/tests/linux/tray/mock_server_info.cpp"
        "${CMAKE_SOURCE_DIR}/tests/linux/tray/mock_config.cpp")
    target_link_libraries(phpvpn-tray-mock-service PRIVATE phpvpn-platform phpvpn-core phpvpn-compile-options)
endif()
