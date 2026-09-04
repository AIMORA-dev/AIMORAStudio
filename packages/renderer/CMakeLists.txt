# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
add_library(
    aimora_studio_renderer STATIC
    include/aimora/studio/renderer/scene_surface.hpp
    src/scene_surface.cpp
)
add_library(AIMORA::StudioRenderer ALIAS aimora_studio_renderer)

set_target_properties(aimora_studio_renderer PROPERTIES EXPORT_NAME StudioRenderer)

target_include_directories(
    aimora_studio_renderer
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_link_libraries(
    aimora_studio_renderer
    PUBLIC
        AIMORA::StudioCanvas
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
    PRIVATE
        Qt6::GuiPrivate
)

qt_add_shaders(
    aimora_studio_renderer aimora_scene_shaders
    PREFIX "/aimora/renderer"
    FILES
        shaders/scene.vert
        shaders/scene.frag
)

aimora_target_defaults(aimora_studio_renderer)
