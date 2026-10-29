add_executable(xdstarter_application_platform_tests application_platform_tests.cpp)
target_link_libraries(xdstarter_application_platform_tests PRIVATE xdstarter_platform)
if(WIN32)
    add_executable(xdstarter_motion_preference_tests motion_preference_tests.cpp)
    target_compile_definitions(xdstarter_motion_preference_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(xdstarter_motion_preference_tests PRIVATE xdstarter_platform)
    add_test(NAME platform.motion-preference COMMAND xdstarter_motion_preference_tests)
    set_tests_properties(platform.motion-preference PROPERTIES TIMEOUT 15
        ENVIRONMENT "QT_QPA_PLATFORM=windows")
    add_executable(xdstarter_graphics_failure_notice_tests graphics_failure_notice_tests.cpp)
    target_compile_definitions(xdstarter_graphics_failure_notice_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(xdstarter_graphics_failure_notice_tests PRIVATE xdstarter_platform)
    add_test(NAME platform.graphics-failure-notice COMMAND xdstarter_graphics_failure_notice_tests)
    set_tests_properties(platform.graphics-failure-notice PROPERTIES TIMEOUT 15)
    add_executable(xdstarter_installation_guard_tests installation_guard_tests.cpp)
    target_link_libraries(xdstarter_installation_guard_tests PRIVATE xdstarter_platform)
    set_target_properties(xdstarter_installation_guard_tests PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin")
    add_test(NAME platform.installation-guard COMMAND xdstarter_installation_guard_tests)
    set_tests_properties(platform.installation-guard PROPERTIES TIMEOUT 20)
    target_compile_definitions(xdstarter_application_platform_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(xdstarter_application_platform_tests PRIVATE ole32 shell32 shlwapi uuid advapi32)
    add_executable(xdstarter_launch_fixture launch_fixture.cpp)
    target_sources(xdstarter_launch_fixture PRIVATE application_catalog_fixture.rc)
    target_link_libraries(xdstarter_launch_fixture PRIVATE Qt6::Core)
    if("$ENV{GITHUB_ACTIONS}" STREQUAL "true" AND
       "$ENV{RUNNER_ENVIRONMENT}" STREQUAL "github-hosted")
        # The workspace volume can have different Shell metadata policy from
        # the user profile. Exercise both without changing Windows policy.
        add_test(NAME platform.applications COMMAND xdstarter_application_platform_tests
            $<TARGET_FILE:xdstarter_launch_fixture> --compare-shell-display)
        if("$ENV{LOCALAPPDATA}" STREQUAL "" OR "$ENV{GITHUB_RUN_ID}" STREQUAL "")
            message(FATAL_ERROR "Hosted Shell localization fixture requires a user profile and run ID")
        endif()
        file(TO_CMAKE_PATH
            "$ENV{LOCALAPPDATA}/Temp/xdstarter-shell-$ENV{GITHUB_RUN_ID}-$ENV{GITHUB_RUN_ATTEMPT}/build/fixture"
            shell_profile_fixture)
        file(MAKE_DIRECTORY "${shell_profile_fixture}")
        add_test(NAME platform.applications.localized COMMAND xdstarter_application_platform_tests
            $<TARGET_FILE:xdstarter_launch_fixture>)
        set(shell_evidence "${PROJECT_BINARY_DIR}/shell-localization-evidence")
        file(MAKE_DIRECTORY "${shell_evidence}/workspace" "${shell_evidence}/profile")
        set_property(TEST platform.applications APPEND PROPERTY ENVIRONMENT
            "XDSTARTER_CATALOG_VALIDATION_DIR=${shell_evidence}/workspace")
        set_tests_properties(platform.applications.localized PROPERTIES
            ENVIRONMENT "XDSTARTER_CATALOG_VALIDATION_DIR=${shell_evidence}/profile"
            WORKING_DIRECTORY "${shell_profile_fixture}" TIMEOUT 120)
    else()
        add_test(NAME platform.applications COMMAND xdstarter_application_platform_tests
            $<TARGET_FILE:xdstarter_launch_fixture>)
    endif()
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
