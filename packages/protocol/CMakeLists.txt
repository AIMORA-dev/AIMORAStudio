# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
add_library(aimora_studio_protocol STATIC src/client_configuration.cpp)
add_library(AIMORA::StudioProtocol ALIAS aimora_studio_protocol)

set_target_properties(aimora_studio_protocol PROPERTIES EXPORT_NAME StudioProtocol)

target_include_directories(
    aimora_studio_protocol
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_link_libraries(aimora_studio_protocol PUBLIC Qt6::Core Qt6::Network)
aimora_target_defaults(aimora_studio_protocol)
