# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
add_executable(aimora_studio_app main.cpp)
add_executable(AIMORA::Studio ALIAS aimora_studio_app)

set_target_properties(
    aimora_studio_app
    PROPERTIES
        EXPORT_NAME Studio
        OUTPUT_NAME aimora-studio
)

target_link_libraries(
    aimora_studio_app
    PRIVATE
        AIMORA::StudioCore
        AIMORA::StudioProtocol
        AIMORA::StudioShell
        AIMORA::StudioThemes
        Qt6::Core
        Qt6::Gui
        Qt6::Network
        Qt6::Widgets
)

aimora_target_defaults(aimora_studio_app)

add_custom_target(
    aimora_studio_smoke
    COMMAND
        ${CMAKE_COMMAND} -E env
        QT_QPA_PLATFORM=offscreen
        $<TARGET_FILE:aimora_studio_app>
        --shell-smoke
        --reset-workspace
        --theme
        light
    COMMAND
        ${CMAKE_COMMAND} -E env
        QT_QPA_PLATFORM=offscreen
        $<TARGET_FILE:aimora_studio_app>
        --shell-smoke
        --reset-workspace
        --theme
        dark
    DEPENDS aimora_studio_app
    COMMENT "Running AIMORAStudio light and dark native shell smoke commands"
    VERBATIM
)
