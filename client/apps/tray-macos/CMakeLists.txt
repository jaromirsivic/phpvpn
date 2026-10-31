# AppKit menu bar tray of the macOS client (plan-macos-v001 step 9), the third tray
# next to the Win32 tray (apps/tray) and the GTK tray (apps/tray-linux). Built by
# apps/CMakeLists.txt when PHPVPN_BUILD_TRAY is ON; the macos-headless preset
# switches it off.
#
# Layout of the sources:
#   * GUI-free C++ (.cpp): the asynchronous IPC channels, the status item model, the
#     launch options and the Configuration Wizard controller. Together with the
#     portable models of apps/tray and the GTK-free presenters of apps/tray-linux
#     they form phpvpn-tray-macos-model, which the unit tests link without AppKit.
#   * Objective-C++ with ARC (.mm): the application, the status item and every window.
#
# Include order matters: this directory first, then apps/tray, then apps/tray-linux,
# so the macOS ipc_async.h and wizard_controller.h win over the Linux files of the
# same name. Only GTK-free files are taken from apps/tray-linux.

set(PHPVPN_TRAY_SHARED_DIR "${CMAKE_SOURCE_DIR}/apps/tray")
set(PHPVPN_TRAY_LINUX_DIR "${CMAKE_SOURCE_DIR}/apps/tray-linux")
set(PHPVPN_TRAY_ICON_DIR "${CMAKE_SOURCE_DIR}/assets/linux/hicolor")

add_library(phpvpn-tray-macos-model STATIC
    ipc_async.cpp
    status_item_model.cpp
    wizard_controller.cpp
    ${PHPVPN_TRAY_SHARED_DIR}/status_model.cpp
    ${PHPVPN_TRAY_SHARED_DIR}/progress_model.cpp
    ${PHPVPN_TRAY_SHARED_DIR}/progress_details.cpp
    ${PHPVPN_TRAY_SHARED_DIR}/save_response.cpp
    ${PHPVPN_TRAY_SHARED_DIR}/contracts_save_machine.cpp
    ${PHPVPN_TRAY_LINUX_DIR}/editor_model.cpp
    ${PHPVPN_TRAY_LINUX_DIR}/server_info_presenter.cpp
    ${PHPVPN_TRAY_LINUX_DIR}/contracts_view.cpp
    ${PHPVPN_TRAY_LINUX_DIR}/wizard_dialog_text.cpp
)
target_include_directories(phpvpn-tray-macos-model PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PHPVPN_TRAY_SHARED_DIR}
    ${PHPVPN_TRAY_LINUX_DIR})
target_link_libraries(phpvpn-tray-macos-model PUBLIC phpvpn-platform phpvpn-core phpvpn-compile-options)

set(PHPVPN_TRAY_MACOS_OBJC_SOURCES
    main.mm
    mac_ui.mm
    mac_status_item.mm
    mac_control_window.mm
    mac_notifications.mm
    mac_tray_app.mm
    mac_progress_window.mm
    mac_statistics_window.mm
    mac_server_info_window.mm
    mac_editor_window.mm
    mac_password_sha_window.mm
    mac_contracts_window.mm
    mac_http_headers_window.mm
    mac_http_error_detail_window.mm
    mac_wizard_window.mm
)
# The project enables only CXX: Apple clang compiles .mm as Objective-C++ by its
# extension. ARC is required by every Objective-C++ file of the tray.
set_source_files_properties(${PHPVPN_TRAY_MACOS_OBJC_SOURCES} PROPERTIES COMPILE_OPTIONS "-fobjc-arc")

# --- bundle resources ------------------------------------------------------------
# Status item images: the generated hicolor PNGs, 16 px and 32 px as @2x, loaded
# with -[NSBundle imageForResource:]. The application icon phpvpn.icns is generated
# by iconutil from an iconset of the same PNGs (16, 32, 64 and 128 px).
set(PHPVPN_TRAY_RESOURCE_DIR "${CMAKE_CURRENT_BINARY_DIR}/resources")
set(PHPVPN_TRAY_ICONSET_DIR "${CMAKE_CURRENT_BINARY_DIR}/phpvpn.iconset")
file(MAKE_DIRECTORY "${PHPVPN_TRAY_RESOURCE_DIR}" "${PHPVPN_TRAY_ICONSET_DIR}")
set(PHPVPN_TRAY_RESOURCES)
foreach(name IN ITEMS phpvpn phpvpn-disconnected phpvpn-connecting phpvpn-connected)
    foreach(pair IN ITEMS "16x16|${name}.png" "32x32|${name}@2x.png")
        string(REPLACE "|" ";" parts "${pair}")
        list(GET parts 0 size)
        list(GET parts 1 target)
        set(source "${PHPVPN_TRAY_ICON_DIR}/${size}/apps/${name}.png")
        if(NOT EXISTS "${source}")
            message(FATAL_ERROR "Missing tray icon ${source}; run client/tools/make_linux_icons.py")
        endif()
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${source}")
        file(COPY_FILE "${source}" "${PHPVPN_TRAY_RESOURCE_DIR}/${target}" ONLY_IF_DIFFERENT)
        list(APPEND PHPVPN_TRAY_RESOURCES "${PHPVPN_TRAY_RESOURCE_DIR}/${target}")
    endforeach()
endforeach()

set(PHPVPN_TRAY_ICONSET_FILES)
foreach(pair IN ITEMS "16x16|icon_16x16.png" "32x32|icon_16x16@2x.png" "32x32|icon_32x32.png"
                      "64x64|icon_32x32@2x.png" "128x128|icon_128x128.png")
    string(REPLACE "|" ";" parts "${pair}")
    list(GET parts 0 size)
    list(GET parts 1 target)
    set(source "${PHPVPN_TRAY_ICON_DIR}/${size}/apps/phpvpn.png")
    if(NOT EXISTS "${source}")
        message(FATAL_ERROR "Missing application icon ${source}; run client/tools/make_linux_icons.py")
    endif()
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${source}")
    file(COPY_FILE "${source}" "${PHPVPN_TRAY_ICONSET_DIR}/${target}" ONLY_IF_DIFFERENT)
    list(APPEND PHPVPN_TRAY_ICONSET_FILES "${PHPVPN_TRAY_ICONSET_DIR}/${target}")
endforeach()
find_program(PHPVPN_ICONUTIL iconutil REQUIRED)
set(PHPVPN_TRAY_ICNS "${CMAKE_CURRENT_BINARY_DIR}/phpvpn.icns")
add_custom_command(OUTPUT "${PHPVPN_TRAY_ICNS}"
    COMMAND "${PHPVPN_ICONUTIL}" -c icns -o "${PHPVPN_TRAY_ICNS}" "${PHPVPN_TRAY_ICONSET_DIR}"
    DEPENDS ${PHPVPN_TRAY_ICONSET_FILES}
    COMMENT "Generating the tray application icon phpvpn.icns"
    VERBATIM)
set_source_files_properties("${PHPVPN_TRAY_ICNS}" PROPERTIES GENERATED TRUE MACOSX_PACKAGE_LOCATION Resources)
set_source_files_properties(${PHPVPN_TRAY_RESOURCES} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)

# Info.plist: version from the project, then handed to the bundle generator (which
# finds no further ${} or @@ references in it).
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in" "${CMAKE_CURRENT_BINARY_DIR}/Info.plist" @ONLY)

add_executable(phpvpn-tray MACOSX_BUNDLE
    ${PHPVPN_TRAY_MACOS_OBJC_SOURCES}
    "${PHPVPN_TRAY_ICNS}"
    ${PHPVPN_TRAY_RESOURCES}
)
target_link_libraries(phpvpn-tray PRIVATE
    phpvpn-tray-macos-model phpvpn-platform phpvpn-core phpvpn-compile-options
    "-framework AppKit" "-framework Foundation" "-framework QuartzCore" "-framework UserNotifications")
# AppKit pulls in AssertMacros.h, whose legacy lowercase check()/verify()/require()
# macros would rewrite ordinary C++ calls such as ContractsSaveMachine::check.
target_compile_definitions(phpvpn-tray PRIVATE __ASSERT_MACROS_DEFINE_VERSIONS_WITHOUT_UNDERSCORES=0)
set_target_properties(phpvpn-tray PROPERTIES
    OUTPUT_NAME "phpvpn-tray"
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_BINARY_DIR}/Info.plist")

# The build tree bundle is phpvpn-tray.app (CMake names a bundle after its
# executable). The installed bundle is "/Applications/phpvpn Tray.app" with the
# executable Contents/MacOS/phpvpn-tray, the path of the LaunchAgent, so the bundle
# contents are installed under that name instead of install(TARGETS ... BUNDLE).
install(DIRECTORY "$<TARGET_BUNDLE_DIR:phpvpn-tray>/" DESTINATION "Applications/phpvpn Tray.app"
    USE_SOURCE_PERMISSIONS)
install(FILES "${CMAKE_SOURCE_DIR}/packaging/macos/launchd/com.pilandrome.phpvpn-tray.plist"
    DESTINATION "Library/LaunchAgents")

# GUI-free unit tests of the macOS helpers (status item mapping, launch arguments,
# test endpoint options, IPC channel preconditions). This directory is configured
# before the top-level include(CTest), so the test is registered through a test
# include file of the top-level directory instead of add_test().
if(BUILD_TESTING)
    add_executable(phpvpn-tray-macos-tests
        "${CMAKE_SOURCE_DIR}/tests/unit/harness.cpp"
        tests/test_tray_macos.cpp)
    target_include_directories(phpvpn-tray-macos-tests PRIVATE "${CMAKE_SOURCE_DIR}/tests/unit")
    target_link_libraries(phpvpn-tray-macos-tests PRIVATE phpvpn-tray-macos-model phpvpn-compile-options)
    # The Wizard parity test compares the macOS and Linux controller sources.
    target_compile_definitions(phpvpn-tray-macos-tests PRIVATE PHPVPN_CLIENT_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
    set(PHPVPN_TRAY_TEST_INCLUDE "${CMAKE_CURRENT_BINARY_DIR}/phpvpn-tray-macos-tests.cmake")
    file(GENERATE OUTPUT "${PHPVPN_TRAY_TEST_INCLUDE}" CONTENT
"add_test([=[phpvpn-tray-macos-tests]=] \"$<TARGET_FILE:phpvpn-tray-macos-tests>\")
set_tests_properties([=[phpvpn-tray-macos-tests]=] PROPERTIES LABELS \"macos\" TIMEOUT 120)
")
    set_property(DIRECTORY "${CMAKE_SOURCE_DIR}" APPEND PROPERTY TEST_INCLUDE_FILES "${PHPVPN_TRAY_TEST_INCLUDE}")
endif()
