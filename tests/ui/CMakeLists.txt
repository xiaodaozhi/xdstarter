find_package(Qt6 REQUIRED COMPONENTS Test)
qt_add_executable(xdstarter_delayed_asset_tests delayed_asset_tests.cpp)
target_link_libraries(xdstarter_delayed_asset_tests PRIVATE Qt6::Gui Qt6::Qml Qt6::Quick Qt6::Test)
add_test(NAME ui.delayed-asset-offscreen COMMAND xdstarter_delayed_asset_tests)
set_tests_properties(ui.delayed-asset-offscreen PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1;XDSTARTER_DELAYED_ASSET_REQUIRE_SOFTWARE=1"
    WORKING_DIRECTORY "${PROJECT_BINARY_DIR}" TIMEOUT 20)
if(WIN32 AND "$ENV{GITHUB_ACTIONS}" STREQUAL "true" AND
   "$ENV{RUNNER_ENVIRONMENT}" STREQUAL "github-hosted")
    add_test(NAME ui.delayed-asset-native COMMAND xdstarter_delayed_asset_tests)
    set_tests_properties(ui.delayed-asset-native PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=windows;QSG_RHI_BACKEND=d3d11;QT_FORCE_STDERR_LOGGING=1;XDSTARTER_DELAYED_ASSET_REQUIRE_D3D11=1"
        WORKING_DIRECTORY "${PROJECT_BINARY_DIR}" TIMEOUT 20)
endif()
qt_add_executable(xdstarter_scene_graph_failure_host_fixture
    scene_graph_failure_host_fixture.cpp
    "${PROJECT_SOURCE_DIR}/src/app/scene_graph_failure_guard.cpp"
    "${PROJECT_SOURCE_DIR}/src/app/scene_graph_failure_notice.cpp"
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
    ui_search_benchmark.h
    ui_search_benchmark.cpp
    contrast_frame_trace.h
    contrast_frame_trace.cpp
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
if(WIN32)
    add_executable(xdstarter_accessibility_uia_driver native_accessibility_driver.cpp)
    target_link_libraries(xdstarter_accessibility_uia_driver PRIVATE ole32 oleaut32 user32 uiautomationcore uuid)
    set_target_properties(xdstarter_accessibility_uia_driver PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin")
endif()
option(XDSTARTER_BUILD_LOCAL_NATIVE_PICKER_TESTS "Build the explicitly opted-in local picker driver" OFF)
if(WIN32 AND (XDSTARTER_BUILD_LOCAL_NATIVE_PICKER_TESTS OR
   ("$ENV{GITHUB_ACTIONS}" STREQUAL "true" AND "$ENV{RUNNER_ENVIRONMENT}" STREQUAL "github-hosted")))
    add_executable(xdstarter_native_picker_driver native_picker_driver.cpp)
    target_link_libraries(xdstarter_native_picker_driver PRIVATE ole32 oleaut32 user32 uiautomationcore uuid)
    set_target_properties(xdstarter_native_picker_driver PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin")
endif()
if(WIN32 AND "$ENV{GITHUB_ACTIONS}" STREQUAL "true" AND
   "$ENV{RUNNER_ENVIRONMENT}" STREQUAL "github-hosted")
    set(accessibility_evidence "${PROJECT_BINARY_DIR}/accessibility-uia-native-evidence")
    file(MAKE_DIRECTORY "${accessibility_evidence}")
    add_test(NAME ui.accessibility-uia-native COMMAND xdstarter_ui_tests
        nativeAccessibilityDescriptions -o "${accessibility_evidence}/test.log,txt")
    set_tests_properties(ui.accessibility-uia-native PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=windows;QSG_RHI_BACKEND=d3d11;QSG_INFO=1;QT_FORCE_STDERR_LOGGING=1;XDSTARTER_NATIVE_UIA_DRIVER=${PROJECT_BINARY_DIR}/bin/xdstarter_accessibility_uia_driver.exe"
        WORKING_DIRECTORY "${PROJECT_BINARY_DIR}"
        TIMEOUT 60)
    set(import_evidence "${PROJECT_BINARY_DIR}/layout-import-native-evidence")
    file(MAKE_DIRECTORY "${import_evidence}")
    add_test(NAME ui.layout-import-native COMMAND xdstarter_ui_tests
        layoutImportDialogThemeAndKeyboard -o "${import_evidence}/native-d3d11.log,txt")
    set_tests_properties(ui.layout-import-native PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=windows;QSG_RHI_BACKEND=d3d11;QSG_INFO=1;QT_FORCE_STDERR_LOGGING=1;XDSTARTER_TEST_REQUIRE_D3D11=1"
        WORKING_DIRECTORY "${PROJECT_BINARY_DIR}"
        TIMEOUT 60)
    set(picker_evidence "${PROJECT_BINARY_DIR}/native-picker-evidence")
    file(MAKE_DIRECTORY "${picker_evidence}")
    foreach(picker_mode IN ITEMS rapid immediate shown)
        add_test(NAME ui.native-picker-${picker_mode} COMMAND xdstarter_ui_tests
            layoutImportFilePickerOwnership -o "${picker_evidence}/${picker_mode}.log,txt")
        set_tests_properties(ui.native-picker-${picker_mode} PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=windows;QSG_RHI_BACKEND=d3d11;QSG_INFO=1;QT_FORCE_STDERR_LOGGING=1;QT_LOGGING_RULES=qt.qpa.dialogs.debug=true;QT_MESSAGE_PATTERN=thread:%{threadid} %{category} %{message};XDSTARTER_NATIVE_PICKER_DIAGNOSTIC=${picker_mode}"
            WORKING_DIRECTORY "${PROJECT_BINARY_DIR}"
            TIMEOUT 60)
    endforeach()
    add_test(NAME ui.native-picker-selection COMMAND xdstarter_ui_tests
        layoutImportNativeSelection layoutImportNativeLaunchFailure -o "${picker_evidence}/selection.log,txt")
    set_tests_properties(ui.native-picker-selection PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=windows;QSG_RHI_BACKEND=d3d11;QSG_INFO=1;QT_FORCE_STDERR_LOGGING=1;XDSTARTER_NATIVE_PICKER_SELECTION=1"
        WORKING_DIRECTORY "${PROJECT_BINARY_DIR}"
        TIMEOUT 60)
    add_test(NAME ui.native-picker-shutdown COMMAND xdstarter_ui_tests
        layoutImportNativeShutdown -o "${picker_evidence}/shutdown.log,txt")
    set_tests_properties(ui.native-picker-shutdown PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=windows;QSG_RHI_BACKEND=d3d11;QSG_INFO=1;QT_FORCE_STDERR_LOGGING=1;XDSTARTER_NATIVE_PICKER_SHUTDOWN=1"
        WORKING_DIRECTORY "${PROJECT_BINARY_DIR}"
        TIMEOUT 60)
    set(contrast_evidence "${PROJECT_BINARY_DIR}/high-contrast-native-evidence")
    file(MAKE_DIRECTORY "${contrast_evidence}")
    add_test(NAME ui.high-contrast-native COMMAND xdstarter_ui_tests
        highContrastTracksSystemPalette -o "${contrast_evidence}/native-d3d11.log,txt")
    set_tests_properties(ui.high-contrast-native PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=windows;QSG_RHI_BACKEND=d3d11;QSG_INFO=1;QT_FORCE_STDERR_LOGGING=1;XDSTARTER_TEST_REQUIRE_D3D11=1;XDSTARTER_CONTRAST_CAPTURE_DIR=${contrast_evidence}"
        WORKING_DIRECTORY "${PROJECT_BINARY_DIR}"
        TIMEOUT 60)
    set(frame_evidence "${PROJECT_BINARY_DIR}/high-contrast-frame-evidence")
    foreach(mode IN ITEMS offscreen native)
        file(MAKE_DIRECTORY "${frame_evidence}/${mode}")
        add_test(NAME ui.high-contrast-frame-${mode} COMMAND xdstarter_ui_tests
            highContrastTracksSystemPalette -o "${frame_evidence}/${mode}/test.log,txt")
        if(mode STREQUAL "native")
            set(frame_backend "QT_QPA_PLATFORM=windows;QSG_RHI_BACKEND=d3d11;XDSTARTER_TEST_REQUIRE_D3D11=1")
        else()
            set(frame_backend "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
        endif()
        set_tests_properties(ui.high-contrast-frame-${mode} PROPERTIES
            ENVIRONMENT "${frame_backend};QT_FORCE_STDERR_LOGGING=1;QSG_INFO=1;XDSTARTER_FRAME_TRACE_DIR=${frame_evidence}/${mode};XDSTARTER_CONTRAST_CAPTURE_DIR=${frame_evidence}/${mode};QT_QPA_FONTDIR=$ENV{WINDIR}/Fonts"
            WORKING_DIRECTORY "${PROJECT_BINARY_DIR}"
            TIMEOUT 60)
    endforeach()
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
