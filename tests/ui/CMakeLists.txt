find_package(Qt6 REQUIRED COMPONENTS Test)
qt_add_executable(xdstarter_ui_tests
    tst_prototype_window.cpp
    native_chrome_probe.h
    native_chrome_probe.cpp
)
xdstarter_add_ui(xdstarter_ui_tests "${PROJECT_BINARY_DIR}/test-qml")
target_link_libraries(xdstarter_ui_tests PRIVATE Qt6::Test)
set_target_properties(xdstarter_ui_tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin"
)
add_test(NAME ui.prototype COMMAND xdstarter_ui_tests)
set_tests_properties(ui.prototype PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1"
    WORKING_DIRECTORY "${PROJECT_BINARY_DIR}"
    TIMEOUT 90
)
if(WIN32 AND DEFINED ENV{WINDIR})
    set_property(TEST ui.prototype APPEND PROPERTY ENVIRONMENT "QT_QPA_FONTDIR=$ENV{WINDIR}/Fonts")
endif()

qt_add_executable(xdstarter_desktop_window_host_tests
    tst_desktop_window_host.cpp
    "${PROJECT_SOURCE_DIR}/src/app/desktop_window_host.h"
    "${PROJECT_SOURCE_DIR}/src/app/desktop_window_host.cpp"
)
target_link_libraries(xdstarter_desktop_window_host_tests PRIVATE
    xdstarter_services xdstarter_platform Qt6::Gui Qt6::Test
)
set_target_properties(xdstarter_desktop_window_host_tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin"
)
add_test(NAME ui.desktop_window_host COMMAND xdstarter_desktop_window_host_tests)
set_tests_properties(ui.desktop_window_host PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_FORCE_STDERR_LOGGING=1"
    WORKING_DIRECTORY "${PROJECT_BINARY_DIR}"
    TIMEOUT 30
)
