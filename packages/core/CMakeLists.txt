# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
add_library(aimora_studio_core STATIC src/application_info.cpp)
add_library(AIMORA::StudioCore ALIAS aimora_studio_core)

set_target_properties(aimora_studio_core PROPERTIES EXPORT_NAME StudioCore)

target_include_directories(
    aimora_studio_core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${PROJECT_BINARY_DIR}/generated>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_link_libraries(aimora_studio_core PUBLIC Qt6::Core)
aimora_target_defaults(aimora_studio_core)
