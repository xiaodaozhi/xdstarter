add_executable(xdstarter_application_platform_tests application_platform_tests.cpp)
target_link_libraries(xdstarter_application_platform_tests PRIVATE xdstarter_platform)
if(WIN32)
    add_executable(xdstarter_installation_guard_tests installation_guard_tests.cpp)
    target_link_libraries(xdstarter_installation_guard_tests PRIVATE xdstarter_platform)
    add_test(NAME platform.installation-guard COMMAND xdstarter_installation_guard_tests)
    set_tests_properties(platform.installation-guard PROPERTIES TIMEOUT 20)
    target_compile_definitions(xdstarter_application_platform_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(xdstarter_application_platform_tests PRIVATE ole32 shell32 shlwapi uuid advapi32)
    add_executable(xdstarter_launch_fixture launch_fixture.cpp)
    target_sources(xdstarter_launch_fixture PRIVATE application_catalog_fixture.rc)
    target_link_libraries(xdstarter_launch_fixture PRIVATE Qt6::Core)
    add_test(NAME platform.applications COMMAND xdstarter_application_platform_tests
        $<TARGET_FILE:xdstarter_launch_fixture>)
else()
    add_test(NAME platform.applications COMMAND xdstarter_application_platform_tests)
endif()
set_tests_properties(platform.applications PROPERTIES
    # The merge-budget fixture creates 4100 shortcuts and removes them before
    # returning. Allow bounded cleanup time on slower Windows filesystems.
    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}" TIMEOUT 120)

add_executable(xdstarter_desktop_platform_tests desktop_platform_tests.cpp)
add_executable(xdstarter_desktop_width_snap_tests desktop_width_snap_tests.cpp)
target_link_libraries(xdstarter_desktop_width_snap_tests PRIVATE xdstarter_platform)
add_test(NAME platform.desktop-width-snap COMMAND xdstarter_desktop_width_snap_tests)
set_tests_properties(platform.desktop-width-snap PROPERTIES
    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}" TIMEOUT 15)
target_link_libraries(xdstarter_desktop_platform_tests PRIVATE xdstarter_platform)
if(WIN32)
    target_compile_definitions(xdstarter_desktop_platform_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(xdstarter_desktop_platform_tests PRIVATE user32 dwmapi)
    add_executable(xdstarter_start_button_window_event_tests start_button_window_event_tests.cpp)
    target_compile_definitions(xdstarter_start_button_window_event_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(xdstarter_start_button_window_event_tests PRIVATE xdstarter_platform user32)
    add_test(NAME platform.start-button-window-events COMMAND xdstarter_start_button_window_event_tests)
    set_tests_properties(platform.start-button-window-events PROPERTIES
        WORKING_DIRECTORY "${CMAKE_BINARY_DIR}" TIMEOUT 15)
    add_executable(xdstarter_native_taskbar_probe native_taskbar_probe.cpp)
    target_compile_definitions(xdstarter_native_taskbar_probe PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(xdstarter_native_taskbar_probe PRIVATE xdstarter_platform user32 ole32 oleaut32 uuid)
    add_test(NAME platform.native-menu-resize
        COMMAND xdstarter_native_taskbar_probe --run --resize-filter-test)
    set_tests_properties(platform.native-menu-resize PROPERTIES
        WORKING_DIRECTORY "${CMAKE_BINARY_DIR}" TIMEOUT 15
        ENVIRONMENT "QT_QPA_PLATFORM=windows")
endif()
add_test(NAME platform.desktop COMMAND xdstarter_desktop_platform_tests)
set_tests_properties(platform.desktop PROPERTIES
    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}" TIMEOUT 15)

add_executable(xdstarter_win_key_capture_tests win_key_capture_tests.cpp)
target_link_libraries(xdstarter_win_key_capture_tests PRIVATE xdstarter_platform)
add_test(NAME platform.win-key-capture COMMAND xdstarter_win_key_capture_tests)
set_tests_properties(platform.win-key-capture PROPERTIES
    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}" TIMEOUT 15)

add_executable(xdstarter_start_button_capture_tests start_button_capture_tests.cpp)
target_link_libraries(xdstarter_start_button_capture_tests PRIVATE xdstarter_platform)
add_test(NAME platform.start-button-capture COMMAND xdstarter_start_button_capture_tests)
set_tests_properties(platform.start-button-capture PROPERTIES
    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}" TIMEOUT 15)
