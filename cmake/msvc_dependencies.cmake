# Some Windows installations lack the English MSVC language pack. Preserve the
# actual /showIncludes prefix bytes instead of relying on CMake's code-page guess.
set(dependency_probe_dir "${CMAKE_BINARY_DIR}/CMakeFiles/xdstarter-dependencies")
file(MAKE_DIRECTORY "${dependency_probe_dir}")
file(WRITE "${dependency_probe_dir}/xdstarter_dependency_probe.h" "#pragma once\n")
file(WRITE "${dependency_probe_dir}/probe.cpp"
    "#include \"xdstarter_dependency_probe.h\"\nint dependencyProbe() { return 0; }\n")
execute_process(
    COMMAND "${CMAKE_CXX_COMPILER}" /nologo /showIncludes /c
        "${dependency_probe_dir}/probe.cpp" "/Fo${dependency_probe_dir}/probe.obj"
    WORKING_DIRECTORY "${dependency_probe_dir}"
    ENCODING NONE
    RESULT_VARIABLE dependency_probe_result
    OUTPUT_VARIABLE dependency_probe_output
    ERROR_VARIABLE dependency_probe_error
)
if(NOT dependency_probe_result EQUAL 0)
    message(FATAL_ERROR "MSVC header dependency probe failed: ${dependency_probe_error}")
endif()
string(REGEX MATCH "[^\r\n]*xdstarter_dependency_probe\\.h"
    dependency_probe_line "${dependency_probe_output}")
string(REGEX MATCH "[A-Za-z]:[\\\\/]" dependency_probe_drive "${dependency_probe_line}")
if(NOT dependency_probe_drive)
    message(FATAL_ERROR "Cannot identify MSVC's /showIncludes prefix; refusing an unsafe incremental build.")
endif()
string(FIND "${dependency_probe_line}" "${dependency_probe_drive}" dependency_prefix_length)
string(SUBSTRING "${dependency_probe_line}" 0 ${dependency_prefix_length} CMAKE_CL_SHOWINCLUDES_PREFIX)
if(NOT CMAKE_CL_SHOWINCLUDES_PREFIX)
    message(FATAL_ERROR "MSVC's /showIncludes prefix is empty.")
endif()
