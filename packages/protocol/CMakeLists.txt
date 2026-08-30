# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
add_library(
    aimora_studio_protocol
    STATIC
        include/aimora/studio/protocol/client_configuration.hpp
        include/aimora/studio/protocol/frame_codec.hpp
        include/aimora/studio/protocol/generated/service_protocol.hpp
        include/aimora/studio/protocol/service_client.hpp
        include/aimora/studio/protocol/service_message.hpp
        include/aimora/studio/protocol/service_process.hpp
        src/client_configuration.cpp
        src/frame_codec.cpp
        src/generated/service_protocol.cpp
        src/service_client.cpp
        src/service_message.cpp
        src/service_process.cpp
)
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
