add_executable(xdstarter_application_platform_tests application_platform_tests.cpp)
target_link_libraries(xdstarter_application_platform_tests PRIVATE xdstarter_platform)
if(WIN32)
    target_compile_definitions(xdstarter_application_platform_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(xdstarter_application_platform_tests PRIVATE ole32 shell32 uuid advapi32)
    add_executable(xdstarter_launch_fixture launch_fixture.cpp)
    target_link_libraries(xdstarter_launch_fixture PRIVATE Qt6::Core)
    add_test(NAME platform.applications COMMAND xdstarter_application_platform_tests
        $<TARGET_FILE:xdstarter_launch_fixture>)
else()
    add_test(NAME platform.applications COMMAND xdstarter_application_platform_tests)
endif()
set_tests_properties(platform.applications PROPERTIES
    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}" TIMEOUT 30)

add_executable(xdstarter_desktop_platform_tests desktop_platform_tests.cpp)
target_link_libraries(xdstarter_desktop_platform_tests PRIVATE xdstarter_platform)
if(WIN32)
    target_compile_definitions(xdstarter_desktop_platform_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(xdstarter_desktop_platform_tests PRIVATE user32 dwmapi)
    add_executable(xdstarter_native_taskbar_probe native_taskbar_probe.cpp)
    target_compile_definitions(xdstarter_native_taskbar_probe PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(xdstarter_native_taskbar_probe PRIVATE xdstarter_platform user32)
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
