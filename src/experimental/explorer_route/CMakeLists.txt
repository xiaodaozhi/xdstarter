# src/experimental/explorer_route/CMakeLists.txt

add_library(xdstarter_explorer_route SHARED route_dll.cpp)
add_executable(xdstarter_route_fixture route_fixture.cpp ../start_bridge/poc_version.rc)
add_executable(xdstarter_route_probe route_controller.cpp ../start_bridge/win_support.cpp
    ../start_bridge/poc_version.rc)

foreach(target IN ITEMS xdstarter_explorer_route xdstarter_route_fixture xdstarter_route_probe)
    target_include_directories(${target} PRIVATE "${PROJECT_SOURCE_DIR}/src")
    target_compile_definitions(${target} PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
    target_link_libraries(${target} PRIVATE user32)
    set_target_properties(${target} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
        AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
endforeach()
target_link_libraries(xdstarter_route_probe PRIVATE xdstarter_platform Qt6::Core
    bcrypt version advapi32 wintrust crypt32)
add_dependencies(xdstarter_route_probe xdstarter_explorer_route xdstarter_route_fixture)

if(BUILD_TESTING)
    add_test(NAME experimental.explorer-route-self-test COMMAND xdstarter_route_probe --self-test)
    set_tests_properties(experimental.explorer-route-self-test PROPERTIES
        WORKING_DIRECTORY "${CMAKE_BINARY_DIR}" TIMEOUT 15)
endif()
