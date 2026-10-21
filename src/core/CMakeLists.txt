set(grid_tokens_source "${PROJECT_SOURCE_DIR}/docs/design/tokens/tile-grid.json")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${grid_tokens_source}")
file(READ "${grid_tokens_source}" grid_tokens_json)

function(read_grid_integer output minimum)
    string(JSON value_type ERROR_VARIABLE type_error TYPE "${grid_tokens_json}" ${ARGN})
    string(JSON value ERROR_VARIABLE json_error GET "${grid_tokens_json}" ${ARGN})
    if(type_error OR json_error OR NOT value_type STREQUAL "NUMBER" OR NOT value MATCHES "^[0-9]+$")
        message(FATAL_ERROR "Invalid integer tile-grid token: ${ARGN}")
    endif()
    if(value LESS minimum OR value GREATER 2147483647)
        message(FATAL_ERROR "Tile-grid token out of range: ${ARGN}")
    endif()
    set(${output} "${value}" PARENT_SCOPE)
endfunction()

read_grid_integer(GRID_CELL 1 cell)
read_grid_integer(GRID_GAP 0 gap)
read_grid_integer(GRID_COLUMNS 1 columns)
read_grid_integer(GRID_SMALL_COLUMNS 1 sizes small columns)
read_grid_integer(GRID_SMALL_ROWS 1 sizes small rows)
read_grid_integer(GRID_STANDARD_COLUMNS 1 sizes standard columns)
read_grid_integer(GRID_STANDARD_ROWS 1 sizes standard rows)
read_grid_integer(GRID_WIDE_COLUMNS 1 sizes wide columns)
read_grid_integer(GRID_WIDE_ROWS 1 sizes wide rows)

foreach(span IN ITEMS GRID_SMALL_COLUMNS GRID_STANDARD_COLUMNS GRID_WIDE_COLUMNS)
    if(${span} GREATER GRID_COLUMNS)
        message(FATAL_ERROR "Tile-grid size exceeds available columns: ${span}")
    endif()
endforeach()
math(EXPR grid_width "${GRID_COLUMNS} * ${GRID_CELL} + (${GRID_COLUMNS} - 1) * ${GRID_GAP}")
if(grid_width GREATER 2147483647)
    message(FATAL_ERROR "Tile-grid width exceeds the supported integer coordinate range")
endif()

configure_file(
    "${PROJECT_SOURCE_DIR}/cmake/grid_tokens.h.in"
    "${PROJECT_BINARY_DIR}/generated/xdstarter/grid_tokens.h"
    @ONLY
)

add_library(xdstarter_core STATIC tile_grid.cpp tile_grid.h)
target_include_directories(xdstarter_core PUBLIC
    "${PROJECT_SOURCE_DIR}/src"
    "${PROJECT_BINARY_DIR}/generated"
)
target_compile_features(xdstarter_core PUBLIC cxx_std_20)
