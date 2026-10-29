add_library(xdstarter_platform STATIC platform_integration.cpp platform_integration.h
    motion_preference.cpp motion_preference.h
    contrast_preference.cpp contrast_preference.h
    installation_guard.cpp installation_guard.h
    graphics_failure_notice.h
    application_platform.cpp application_platform.h
    desktop_platform.cpp desktop_platform.h
    desktop_width_snap.cpp desktop_width_snap.h
    desktop_tray.cpp desktop_tray.h
    win_key_capture.cpp win_key_capture.h
    win_key_capture_state.cpp win_key_capture_state.h
    start_button_capture.cpp start_button_capture.h
    start_button_capture_state.cpp start_button_capture_state.h)
if(WIN32)
    target_sources(xdstarter_platform PRIVATE
        windows/windows_installation_guard.cpp
        windows/windows_graphics_failure_notice.cpp
        windows/windows_graphics_failure_notice.h
        windows/windows_platform_integration.cpp
        windows/windows_platform_integration.h
        windows/windows_motion_preference.cpp
        windows/windows_motion_preference.h
        windows/windows_contrast_preference.cpp
        windows/windows_contrast_preference.h
        windows/windows_application_platform.cpp
        windows/windows_application_platform.h
        windows/windows_desktop_platform.cpp
        windows/windows_desktop_platform.h
        windows/windows_desktop_tray.cpp
        windows/windows_desktop_tray.h
        windows/windows_win_key_capture.cpp
        windows/windows_win_key_capture.h
        windows/windows_start_button_capture.cpp
        windows/windows_start_button_events.cpp
        windows/windows_start_button_events.h
    )
else()
    target_sources(xdstarter_platform PRIVATE
        stub/stub_graphics_failure_notice.cpp
        stub/stub_platform_integration.cpp
        stub/stub_platform_integration.h
        stub/stub_application_platform.cpp
        stub/stub_application_platform.h
        stub/stub_desktop_platform.cpp
        stub/stub_desktop_platform.h
        stub/stub_desktop_tray.cpp
        stub/stub_desktop_tray.h
        stub/stub_win_key_capture.cpp
        stub/stub_win_key_capture.h
    )
endif()
target_include_directories(xdstarter_platform PUBLIC "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(xdstarter_platform PUBLIC Qt6::Core Qt6::Gui)
if(WIN32)
    target_compile_definitions(xdstarter_platform PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(xdstarter_platform PRIVATE shell32 ole32 oleaut32 user32 gdi32 propsys uuid
        uiautomationcore dwmapi comctl32 advapi32 wtsapi32)
endif()
