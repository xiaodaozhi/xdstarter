add_library(xdstarter_services STATIC prototype_service.cpp prototype_service.h layout_store.cpp
                                        layout_store.h application_catalog.cpp application_catalog.h
                                        desktop_shell_service.cpp desktop_shell_service.h)
target_include_directories(xdstarter_services PUBLIC "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(xdstarter_services PUBLIC xdstarter_core xdstarter_platform Qt6::Core Qt6::Gui)
