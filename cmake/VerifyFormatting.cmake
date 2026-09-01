# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
cmake_minimum_required(VERSION 3.28)

if(NOT DEFINED AIMORA_SOURCE_DIR)
    get_filename_component(AIMORA_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
endif()

file(
    GLOB_RECURSE formatted_files
    LIST_DIRECTORIES false
    "${AIMORA_SOURCE_DIR}/CMakeLists.txt"
    "${AIMORA_SOURCE_DIR}/apps/*/CMakeLists.txt"
    "${AIMORA_SOURCE_DIR}/apps/*.cpp"
    "${AIMORA_SOURCE_DIR}/apps/*.hpp"
    "${AIMORA_SOURCE_DIR}/cmake/*.cmake"
    "${AIMORA_SOURCE_DIR}/packages/*/CMakeLists.txt"
    "${AIMORA_SOURCE_DIR}/packages/*.cpp"
    "${AIMORA_SOURCE_DIR}/packages/*.hpp"
    "${AIMORA_SOURCE_DIR}/tests/CMakeLists.txt"
    "${AIMORA_SOURCE_DIR}/tests/*.cpp"
    "${AIMORA_SOURCE_DIR}/tests/*.hpp"
)

if(NOT formatted_files)
    message(FATAL_ERROR "No files were selected for the formatting contract.")
endif()

foreach(formatted_file IN LISTS formatted_files)
    file(READ "${formatted_file}" contents)

    if(contents MATCHES "\t")
        message(FATAL_ERROR "Tab character is prohibited: ${formatted_file}")
    endif()
    if(contents MATCHES "[ ]+\n")
        message(FATAL_ERROR "Trailing whitespace is prohibited: ${formatted_file}")
    endif()
    if(NOT contents MATCHES "\n$")
        message(FATAL_ERROR "File must end with a newline: ${formatted_file}")
    endif()

    if(formatted_file MATCHES "\\.(cpp|hpp)$")
        string(REPLACE "\n" ";" lines "${contents}")
        set(line_number 0)
        foreach(line IN LISTS lines)
            math(EXPR line_number "${line_number} + 1")
            string(LENGTH "${line}" line_length)
            if(line_length GREATER 100)
                message(
                    FATAL_ERROR
                    "Native source line exceeds 100 columns: ${formatted_file}:${line_number}"
                )
            endif()
        endforeach()
    endif()
endforeach()

message(STATUS "AIMORAStudio deterministic formatting contract passed.")
