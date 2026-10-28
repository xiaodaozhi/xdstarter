find_package(Git QUIET)
set(XDSTARTER_BUILD_NUMBER "0" CACHE STRING "Numeric development/package build number")
option(XDSTARTER_DISTRIBUTION_BUILD "Build a Windows MSVC 2022 distribution candidate" OFF)
if(NOT XDSTARTER_BUILD_NUMBER MATCHES "^[0-9]+$" OR XDSTARTER_BUILD_NUMBER GREATER 65535)
    message(FATAL_ERROR "Build number must be an integer in 0..65535")
endif()
set(XDSTARTER_REVISION "unknown")
set(XDSTARTER_DIRTY true)
if(GIT_FOUND)
    execute_process(COMMAND "${GIT_EXECUTABLE}" rev-parse HEAD
        WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}" OUTPUT_VARIABLE XDSTARTER_REVISION
        OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    execute_process(COMMAND "${GIT_EXECUTABLE}" status --porcelain --untracked-files=normal
        WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}" OUTPUT_VARIABLE working_changes
        RESULT_VARIABLE status_result ERROR_QUIET)
    if(status_result EQUAL 0 AND working_changes STREQUAL "")
        set(XDSTARTER_DIRTY false)
    endif()
endif()
set(XDSTARTER_DISTRIBUTION false)
if(XDSTARTER_DISTRIBUTION_BUILD)
    if(NOT WIN32 OR NOT MSVC OR MSVC_VERSION LESS 1939 OR
       NOT CMAKE_BUILD_TYPE STREQUAL "Release" OR XDSTARTER_DIRTY OR
       NOT XDSTARTER_REVISION MATCHES "^[0-9a-f]+$")
        message(FATAL_ERROR "Distribution requires clean Git source, Windows x64, MSVC 2022 >=19.39 and Release")
    endif()
    set(XDSTARTER_DISTRIBUTION true)
endif()
configure_file("${PROJECT_SOURCE_DIR}/src/app/build_identity.h.in"
    "${PROJECT_BINARY_DIR}/generated/build_identity.h" @ONLY)
