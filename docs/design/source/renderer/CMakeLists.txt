qt_add_executable(xdstarter_render_svg main.cpp "${PROJECT_SOURCE_DIR}/src/app/interface_font.cpp")
target_include_directories(xdstarter_render_svg PRIVATE "${PROJECT_SOURCE_DIR}/src/app")
target_link_libraries(xdstarter_render_svg PRIVATE Qt6::Gui Qt6::Svg)
set_target_properties(xdstarter_render_svg PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin"
)
