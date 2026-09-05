# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
cmake_minimum_required(VERSION 3.28)

if(NOT DEFINED AIMORA_SOURCE_DIR)
    get_filename_component(AIMORA_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
endif()

set(required_protocol_paths
    "packages/protocol/include/aimora/studio/protocol/client_configuration.hpp"
    "packages/protocol/include/aimora/studio/protocol/frame_codec.hpp"
    "packages/protocol/include/aimora/studio/protocol/generated/service_protocol.hpp"
    "packages/protocol/include/aimora/studio/protocol/service_client.hpp"
    "packages/protocol/include/aimora/studio/protocol/service_message.hpp"
    "packages/protocol/include/aimora/studio/protocol/service_process.hpp"
    "packages/protocol/src/client_configuration.cpp"
    "packages/protocol/src/frame_codec.cpp"
    "packages/protocol/src/generated/service_protocol.cpp"
    "packages/protocol/src/service_client.cpp"
    "packages/protocol/src/service_message.cpp"
    "packages/protocol/src/service_process.cpp"
    "tests/mock_service.cpp"
    "tests/protocol_tests.cpp"
)

foreach(relative_path IN LISTS required_protocol_paths)
    if(NOT EXISTS "${AIMORA_SOURCE_DIR}/${relative_path}")
        message(FATAL_ERROR "Missing GUI040 protocol path: ${relative_path}")
    endif()
endforeach()

set(expected_schema_sha "0a78c31060a493e42879a2260e54d2532eb2ed6d1b7d4793cb33a2d3507b7881")
file(
    READ
    "${AIMORA_SOURCE_DIR}/packages/protocol/include/aimora/studio/protocol/generated/service_protocol.hpp"
    generated_header
)
string(FIND "${generated_header}" "${expected_schema_sha}" schema_offset)
if(schema_offset EQUAL -1)
    message(FATAL_ERROR "The generated service binding does not contain the accepted schema SHA.")
endif()

file(GLOB_RECURSE protocol_sources
    LIST_DIRECTORIES false
    "${AIMORA_SOURCE_DIR}/packages/protocol/*.cpp"
    "${AIMORA_SOURCE_DIR}/packages/protocol/*.hpp"
)
foreach(forbidden_token IN ITEMS
    "AIMORASolvers"
    "private solver"
    "eval("
    "system("
)
    foreach(protocol_source IN LISTS protocol_sources)
        file(READ "${protocol_source}" source_text)
        string(FIND "${source_text}" "${forbidden_token}" token_offset)
        if(NOT token_offset EQUAL -1)
            message(
                FATAL_ERROR
                "Forbidden protocol token '${forbidden_token}' in ${protocol_source}"
            )
        endif()
    endforeach()
endforeach()

message(STATUS "AIMORAStudio GUI040 protocol source contract passed.")
