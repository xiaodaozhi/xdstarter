function(xdstarter_add_ui target import_root)
    target_sources(${target} PRIVATE
        "${PROJECT_SOURCE_DIR}/src/app/application_icon_provider.h"
        "${PROJECT_SOURCE_DIR}/src/app/application_icon_provider.cpp"
        "${PROJECT_SOURCE_DIR}/src/app/desktop_window_host.h"
        "${PROJECT_SOURCE_DIR}/src/app/desktop_window_host.cpp"
    )
    set(qml_files)
    foreach(component IN ITEMS Main TileButton TileGroup ShortcutRail MenuChrome)
        set(qml_file "${PROJECT_SOURCE_DIR}/qml/${component}.qml")
        set_source_files_properties("${qml_file}" PROPERTIES QT_RESOURCE_ALIAS "${component}.qml")
        list(APPEND qml_files "${qml_file}")
    endforeach()
    qt_add_qml_module(${target}
        URI XDStarter
        VERSION 1.0
        DEPENDENCIES QtQml.Models
        OUTPUT_DIRECTORY "${import_root}/XDStarter"
        IMPORT_PATH "${import_root}"
        QML_FILES ${qml_files}
        SOURCES
            "${PROJECT_SOURCE_DIR}/src/app/menu_controller.h"
            "${PROJECT_SOURCE_DIR}/src/app/menu_controller.cpp"
    )

    set(visual_tokens "${PROJECT_SOURCE_DIR}/docs/design/tokens/visual.json")
    set_source_files_properties("${visual_tokens}" PROPERTIES QT_RESOURCE_ALIAS visual.json)
    qt_add_resources(${target} visual_tokens
        PREFIX "/xdstarter/tokens"
        FILES "${visual_tokens}"
    )
    set(interface_assets)
    foreach(theme IN ITEMS dark light)
        foreach(glyph IN ITEMS documents downloads settings power avatar search chevron-down more add close pin unpin apps)
            set(asset "${PROJECT_SOURCE_DIR}/docs/design/assets/ui/${theme}/${glyph}.svg")
            set_source_files_properties("${asset}" PROPERTIES QT_RESOURCE_ALIAS "ui/${theme}/${glyph}.svg")
            list(APPEND interface_assets "${asset}")
        endforeach()
    endforeach()
    foreach(name IN ITEMS xdstarter-mark xdstarter-app-icon)
        set(asset "${PROJECT_SOURCE_DIR}/docs/design/assets/app/${name}.svg")
        set_source_files_properties("${asset}" PROPERTIES QT_RESOURCE_ALIAS "app/${name}.svg")
        list(APPEND interface_assets "${asset}")
    endforeach()
    foreach(size IN ITEMS 16 20 24 32 40 48 64 96 128 256 512 1024)
        set(asset "${PROJECT_SOURCE_DIR}/docs/design/assets/app/xdstarter-app-icon-${size}.png")
        set_source_files_properties("${asset}" PROPERTIES QT_RESOURCE_ALIAS "app/xdstarter-app-icon-${size}.png")
        list(APPEND interface_assets "${asset}")
    endforeach()
    qt_add_resources(${target} interface_assets
        PREFIX "/xdstarter"
        FILES ${interface_assets}
    )
    target_link_libraries(${target} PRIVATE
        xdstarter_services xdstarter_platform
        Qt6::Gui Qt6::Qml Qt6::QmlModels Qt6::Quick Qt6::QuickControls2 Qt6::Svg
    )
    target_include_directories(${target} PRIVATE "${PROJECT_SOURCE_DIR}/src/app")
endfunction()

qt_add_executable(xdstarter main.cpp)
xdstarter_add_ui(xdstarter "${PROJECT_BINARY_DIR}/qml")
if(WIN32)
    set(XDSTARTER_ICON_FILE "${PROJECT_SOURCE_DIR}/docs/design/assets/app/xdstarter.ico")
    configure_file(windows_resources.rc.in "${CMAKE_CURRENT_BINARY_DIR}/windows_resources.rc" @ONLY)
    set_source_files_properties("${CMAKE_CURRENT_BINARY_DIR}/windows_resources.rc" PROPERTIES
        OBJECT_DEPENDS "${XDSTARTER_ICON_FILE}")
    target_sources(xdstarter PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/windows_resources.rc")
endif()
set_target_properties(xdstarter PROPERTIES
    WIN32_EXECUTABLE TRUE
    RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin"
)

if(BUILD_TESTING)
    add_test(NAME app.qml_smoke COMMAND xdstarter --smoke-test)
    set_tests_properties(app.qml_smoke PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1"
        WORKING_DIRECTORY "${PROJECT_BINARY_DIR}"
        TIMEOUT 15
        FAIL_REGULAR_EXPRESSION "ReferenceError:;TypeError:;Unable to assign;Cannot assign;is not installed"
    )
    if(WIN32 AND DEFINED ENV{WINDIR})
        set_property(TEST app.qml_smoke APPEND PROPERTY ENVIRONMENT "QT_QPA_FONTDIR=$ENV{WINDIR}/Fonts")
    endif()
endif()
