find_package(Python3 3.11 REQUIRED COMPONENTS Interpreter)

# The proxy is never linked to or deployed with the application. The test
# copies it under the system DLL name only inside a disposable runtime copy.
add_library(xdstarter_d3d11_failure_proxy SHARED d3d11_failure_proxy.cpp)
target_link_options(xdstarter_d3d11_failure_proxy PRIVATE
    "/EXPORT:D3D11CreateDevice=ProbeD3D11CreateDevice")
set_target_properties(xdstarter_d3d11_failure_proxy PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/test-fixtures")

add_test(NAME app.d3d11_failure_exit
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/verify_d3d11_failure.py"
        --app "$<TARGET_FILE:xdstarter>"
        --qt-bin "$<TARGET_FILE_DIR:Qt6::Core>"
        --proxy "$<TARGET_FILE:xdstarter_d3d11_failure_proxy>"
        --output "${PROJECT_BINARY_DIR}/graphics-failure-evidence/result.json")
set_tests_properties(app.d3d11_failure_exit PROPERTIES
    WORKING_DIRECTORY "${PROJECT_BINARY_DIR}"
    TIMEOUT 120)
