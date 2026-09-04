# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
add_library(
    aimora_studio_shell STATIC
    include/aimora/studio/shell/studio_shell.hpp
    src/drawing_workspace.cpp
    src/studio_dock_widget.cpp
    src/studio_main_window.cpp
    src/studio_menus.cpp
    src/studio_panels.cpp
    src/workspace_settings.cpp
)
add_library(AIMORA::StudioShell ALIAS aimora_studio_shell)

set_target_properties(aimora_studio_shell PROPERTIES EXPORT_NAME StudioShell)

target_include_directories(
    aimora_studio_shell
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_link_libraries(
    aimora_studio_shell
    PUBLIC
        AIMORA::StudioCore
        AIMORA::StudioCommands
        AIMORA::StudioInspector
        AIMORA::StudioRenderer
        AIMORA::StudioThemes
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
)

aimora_target_defaults(aimora_studio_shell)
