# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
cmake_minimum_required(VERSION 3.28)

if(NOT DEFINED AIMORA_SOURCE_DIR)
    get_filename_component(AIMORA_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
endif()

foreach(mode IN ITEMS light dark)
    set(fixture "${AIMORA_SOURCE_DIR}/tests/fixtures/theme-${mode}.json")
    if(NOT EXISTS "${fixture}")
        message(FATAL_ERROR "Missing committed ${mode} theme fixture: ${fixture}")
    endif()

    file(READ "${fixture}" fixture_text)
    string(JSON fixture_schema GET "${fixture_text}" schema)
    string(JSON fixture_mode GET "${fixture_text}" mode)
    string(JSON token_count LENGTH "${fixture_text}" tokens)

    if(NOT fixture_schema STREQUAL "aimora.theme.tokens.v1")
        message(FATAL_ERROR "Unexpected theme fixture schema in ${fixture}")
    endif()
    if(NOT fixture_mode STREQUAL mode)
        message(FATAL_ERROR "Theme fixture mode mismatch in ${fixture}")
    endif()
    if(NOT token_count EQUAL 18)
        message(FATAL_ERROR "Theme fixture must contain 18 semantic tokens: ${fixture}")
    endif()
endforeach()

message(STATUS "AIMORAStudio committed theme fixtures passed.")
