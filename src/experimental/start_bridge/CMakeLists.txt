# src/experimental/start_bridge/CMakeLists.txt

add_library(xdstarter_start_bridge_poc SHARED bridge_dll.cpp win_support.cpp poc_version.rc)
add_executable(xdstarter_start_bridge_fixture fixture_host.cpp win_support.cpp poc_version.rc)
qt_add_executable(xdstarter_start_bridge_controller bridge_controller.cpp session_server.cpp
    win_support.cpp poc_version.rc)

foreach(target IN ITEMS xdstarter_start_bridge_poc xdstarter_start_bridge_fixture
                        xdstarter_start_bridge_controller)
    target_include_directories(${target} PRIVATE "${PROJECT_SOURCE_DIR}/src")
    target_compile_definitions(${target} PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
    target_link_libraries(${target} PRIVATE user32 advapi32 bcrypt version)
    set_target_properties(${target} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
endforeach()
target_link_libraries(xdstarter_start_bridge_fixture PRIVATE shell32)
set_target_properties(xdstarter_start_bridge_poc xdstarter_start_bridge_fixture PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
target_link_libraries(xdstarter_start_bridge_controller PRIVATE
    Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2 shell32)
add_dependencies(xdstarter_start_bridge_controller
    xdstarter_start_bridge_poc xdstarter_start_bridge_fixture)

if(BUILD_TESTING)
    add_executable(xdstarter_start_bridge_tests
        "${PROJECT_SOURCE_DIR}/tests/experimental/start_bridge_tests.cpp"
        session_server.cpp win_support.cpp poc_version.rc)
    target_include_directories(xdstarter_start_bridge_tests PRIVATE "${PROJECT_SOURCE_DIR}/src")
    target_compile_definitions(xdstarter_start_bridge_tests PRIVATE
        NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
    target_link_libraries(xdstarter_start_bridge_tests PRIVATE user32 advapi32 bcrypt version shell32)
    set_target_properties(xdstarter_start_bridge_tests PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
        AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
    add_dependencies(xdstarter_start_bridge_tests
        xdstarter_start_bridge_poc xdstarter_start_bridge_fixture)
    add_test(NAME experimental.start-bridge COMMAND xdstarter_start_bridge_tests
        $<TARGET_FILE:xdstarter_start_bridge_poc> $<TARGET_FILE:xdstarter_start_bridge_fixture>)
    set_tests_properties(experimental.start-bridge PROPERTIES
        WORKING_DIRECTORY "${CMAKE_BINARY_DIR}" TIMEOUT 35)
    add_test(NAME experimental.start-bridge-ui-smoke
        COMMAND xdstarter_start_bridge_controller --smoke-test)
    set_tests_properties(experimental.start-bridge-ui-smoke PROPERTIES
        WORKING_DIRECTORY "${CMAKE_BINARY_DIR}" TIMEOUT 10
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1")
endif()
