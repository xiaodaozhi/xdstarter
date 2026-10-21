add_executable(xdstarter_application_platform_tests application_platform_tests.cpp)
target_link_libraries(xdstarter_application_platform_tests PRIVATE xdstarter_platform)
if(WIN32)
    target_compile_definitions(xdstarter_application_platform_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(xdstarter_application_platform_tests PRIVATE ole32 shell32 uuid)
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
endif()
add_test(NAME platform.desktop COMMAND xdstarter_desktop_platform_tests)
set_tests_properties(platform.desktop PROPERTIES
    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}" TIMEOUT 15)
