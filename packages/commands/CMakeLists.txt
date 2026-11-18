# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
add_library(aimora_studio_commands STATIC src/command_registry.cpp)
add_library(AIMORA::StudioCommands ALIAS aimora_studio_commands)

set_target_properties(aimora_studio_commands PROPERTIES EXPORT_NAME StudioCommands)

target_include_directories(
    aimora_studio_commands
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_link_libraries(aimora_studio_commands PUBLIC Qt6::Core Qt6::Gui)
aimora_target_defaults(aimora_studio_commands)
