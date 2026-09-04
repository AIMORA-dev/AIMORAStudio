# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
add_library(
    aimora_studio_inspector STATIC
    include/aimora/studio/inspector/inspection_document.hpp
    include/aimora/studio/inspector/schema_inspector_widget.hpp
    src/inspection_document.cpp
    src/panel_state.cpp
    src/schema_inspector_widget.cpp
)
add_library(AIMORA::StudioInspector ALIAS aimora_studio_inspector)

set_target_properties(aimora_studio_inspector PROPERTIES EXPORT_NAME StudioInspector)

target_include_directories(
    aimora_studio_inspector
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_link_libraries(
    aimora_studio_inspector
    PUBLIC
        AIMORA::StudioProtocol
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
)
aimora_target_defaults(aimora_studio_inspector)
