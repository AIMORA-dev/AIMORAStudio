# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
add_library(aimora_studio_canvas STATIC src/viewport_state.cpp)
add_library(AIMORA::StudioCanvas ALIAS aimora_studio_canvas)

set_target_properties(aimora_studio_canvas PROPERTIES EXPORT_NAME StudioCanvas)

target_include_directories(
    aimora_studio_canvas
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_link_libraries(aimora_studio_canvas PUBLIC Qt6::Core Qt6::Gui)
aimora_target_defaults(aimora_studio_canvas)
