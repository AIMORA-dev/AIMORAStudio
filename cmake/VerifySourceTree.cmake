# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
cmake_minimum_required(VERSION 3.28)

if(NOT DEFINED AIMORA_SOURCE_DIR)
    get_filename_component(AIMORA_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
endif()

set(required_paths
    "CMakeLists.txt"
    "CMakePresets.json"
    "cmake/VerifyFormatting.cmake"
    "dependencies/qt-lock.json"
    "dependencies/third-party.toml"
    "apps/studio/CMakeLists.txt"
    "apps/studio/main.cpp"
    "packages/core/CMakeLists.txt"
    "packages/protocol/CMakeLists.txt"
    "packages/canvas/CMakeLists.txt"
    "packages/inspector/CMakeLists.txt"
    "packages/commands/CMakeLists.txt"
    "packages/themes/CMakeLists.txt"
    "tests/CMakeLists.txt"
    "tests/foundation_tests.cpp"
)

foreach(relative_path IN LISTS required_paths)
    if(NOT EXISTS "${AIMORA_SOURCE_DIR}/${relative_path}")
        message(FATAL_ERROR "Missing native Studio foundation path: ${relative_path}")
    endif()
endforeach()

set(retired_paths
    "package.json"
    "protocol/package.json"
    "protocol/src/index.ts"
    "test/structure.test.mjs"
)

foreach(relative_path IN LISTS retired_paths)
    if(EXISTS "${AIMORA_SOURCE_DIR}/${relative_path}")
        message(FATAL_ERROR "Retired TypeScript/Node scaffold remains: ${relative_path}")
    endif()
endforeach()

file(
    GLOB_RECURSE forbidden_client_files
    LIST_DIRECTORIES false
    "${AIMORA_SOURCE_DIR}/*.ts"
    "${AIMORA_SOURCE_DIR}/*.tsx"
    "${AIMORA_SOURCE_DIR}/*.mjs"
    "${AIMORA_SOURCE_DIR}/*.cjs"
    "${AIMORA_SOURCE_DIR}/*.qml"
)

if(forbidden_client_files)
    list(JOIN forbidden_client_files "\n" forbidden_listing)
    message(FATAL_ERROR "Forbidden primary-client files detected:\n${forbidden_listing}")
endif()

file(READ "${AIMORA_SOURCE_DIR}/dependencies/qt-lock.json" qt_lock)
string(JSON locked_qt_version GET "${qt_lock}" qt_version)
string(JSON locked_cpp_standard GET "${qt_lock}" cpp_standard)
string(JSON locked_cmake_version GET "${qt_lock}" cmake_minimum)

if(NOT locked_qt_version STREQUAL "6.11.2")
    message(FATAL_ERROR "Unexpected Qt lock: ${locked_qt_version}")
endif()
if(NOT locked_cpp_standard EQUAL 20)
    message(FATAL_ERROR "Unexpected C++ standard lock: ${locked_cpp_standard}")
endif()
if(NOT locked_cmake_version STREQUAL "3.28")
    message(FATAL_ERROR "Unexpected CMake minimum lock: ${locked_cmake_version}")
endif()

file(
    GLOB_RECURSE native_sources
    LIST_DIRECTORIES false
    "${AIMORA_SOURCE_DIR}/apps/*.cpp"
    "${AIMORA_SOURCE_DIR}/apps/*.hpp"
    "${AIMORA_SOURCE_DIR}/packages/*.cpp"
    "${AIMORA_SOURCE_DIR}/packages/*.hpp"
    "${AIMORA_SOURCE_DIR}/tests/*.cpp"
    "${AIMORA_SOURCE_DIR}/tests/*.hpp"
)

if(NOT native_sources)
    message(FATAL_ERROR "No native C++ source files were found.")
endif()

foreach(source_file IN LISTS native_sources)
    file(READ "${source_file}" source_text)
    if(NOT source_text MATCHES "SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0")
        message(FATAL_ERROR "Missing SPDX licence identifier: ${source_file}")
    endif()
endforeach()

file(
    GLOB_RECURSE product_build_files
    LIST_DIRECTORIES false
    "${AIMORA_SOURCE_DIR}/CMakeLists.txt"
    "${AIMORA_SOURCE_DIR}/apps/CMakeLists.txt"
    "${AIMORA_SOURCE_DIR}/packages/CMakeLists.txt"
    "${AIMORA_SOURCE_DIR}/apps/*/CMakeLists.txt"
    "${AIMORA_SOURCE_DIR}/packages/*/CMakeLists.txt"
    "${AIMORA_SOURCE_DIR}/apps/*.cpp"
    "${AIMORA_SOURCE_DIR}/packages/*.cpp"
    "${AIMORA_SOURCE_DIR}/apps/*.hpp"
    "${AIMORA_SOURCE_DIR}/packages/*.hpp"
)

set(forbidden_product_tokens
    "Electron"
    "Chromium"
    "Tauri"
    "Qt6::Qml"
    "Qt6::Quick"
    "Qt6::WebEngine"
    "Qt6::WebView"
)

foreach(product_file IN LISTS product_build_files)
    file(READ "${product_file}" product_text)
    foreach(forbidden_token IN LISTS forbidden_product_tokens)
        string(FIND "${product_text}" "${forbidden_token}" token_offset)
        if(NOT token_offset EQUAL -1)
            message(
                FATAL_ERROR
                "Forbidden primary-product dependency token '${forbidden_token}' in ${product_file}"
            )
        endif()
    endforeach()
endforeach()

message(STATUS "AIMORAStudio native source-tree contract passed.")
