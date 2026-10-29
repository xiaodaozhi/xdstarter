find_package(Qt6 REQUIRED COMPONENTS Test)
qt_add_executable(xdstarter_scene_graph_failure_host_fixture
    scene_graph_failure_host_fixture.cpp
    "${PROJECT_SOURCE_DIR}/src/app/scene_graph_failure_guard.cpp"
    "${PROJECT_SOURCE_DIR}/src/app/desktop_window_host.h"
    "${PROJECT_SOURCE_DIR}/src/app/desktop_window_host.cpp"
)
target_include_directories(xdstarter_scene_graph_failure_host_fixture PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(xdstarter_scene_graph_failure_host_fixture PRIVATE
    xdstarter_services xdstarter_platform Qt6::Gui Qt6::Quick
)
add_test(NAME ui.scene_graph_failure_host COMMAND xdstarter_scene_graph_failure_host_fixture)
set_tests_properties(ui.scene_graph_failure_host PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_FORCE_STDERR_LOGGING=1"
    WORKING_DIRECTORY "${PROJECT_BINARY_DIR}"
    TIMEOUT 15
)

add_executable(xdstarter_runtime_support_tests runtime_support_tests.cpp
    "${PROJECT_SOURCE_DIR}/src/app/runtime_support.cpp")
target_include_directories(xdstarter_runtime_support_tests PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(xdstarter_runtime_support_tests PRIVATE Qt6::Core)
set_target_properties(xdstarter_runtime_support_tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin")
add_test(NAME app.qt-translations COMMAND xdstarter_runtime_support_tests
    "$<TARGET_FILE_DIR:Qt6::Core>/../translations")
qt_add_executable(xdstarter_ui_tests
    tst_prototype_window.cpp
    native_chrome_probe.h
    native_chrome_probe.cpp
    visual_acceptance_probe.h
    visual_acceptance_probe.cpp
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
    TIMEOUT 180
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
