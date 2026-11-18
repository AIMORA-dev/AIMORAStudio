# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
add_library(
    aimora_studio_themes STATIC
    include/aimora/studio/themes/theme_system.hpp
    src/theme_mode.cpp
    src/theme_system.cpp
)
add_library(AIMORA::StudioThemes ALIAS aimora_studio_themes)

set_target_properties(aimora_studio_themes PROPERTIES EXPORT_NAME StudioThemes)

target_include_directories(
    aimora_studio_themes
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_link_libraries(aimora_studio_themes PUBLIC Qt6::Core Qt6::Gui Qt6::Widgets)
aimora_target_defaults(aimora_studio_themes)
