# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
add_library(aimora_studio_inspector STATIC src/panel_state.cpp)
add_library(AIMORA::StudioInspector ALIAS aimora_studio_inspector)

set_target_properties(aimora_studio_inspector PROPERTIES EXPORT_NAME StudioInspector)

target_include_directories(
    aimora_studio_inspector
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_link_libraries(aimora_studio_inspector PUBLIC Qt6::Core Qt6::Widgets)
aimora_target_defaults(aimora_studio_inspector)
