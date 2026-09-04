# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
cmake_minimum_required(VERSION 3.28)

if(NOT DEFINED AIMORA_SOURCE_DIR)
    get_filename_component(AIMORA_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
endif()

set(required_paths
    "CMakeLists.txt"
    "CMakePresets.json"
    "cmake/VerifyFormatting.cmake"
    "cmake/VerifyThemeFixtures.cmake"
    "dependencies/qt-lock.json"
    "dependencies/third-party.toml"
    "apps/studio/CMakeLists.txt"
    "apps/studio/main.cpp"
    "packages/core/CMakeLists.txt"
    "packages/protocol/CMakeLists.txt"
    "packages/canvas/CMakeLists.txt"
    "packages/canvas/include/aimora/studio/canvas/painter_scene_renderer.hpp"
    "packages/canvas/include/aimora/studio/canvas/retained_scene.hpp"
    "packages/canvas/include/aimora/studio/canvas/scene_cache.hpp"
    "packages/canvas/include/aimora/studio/canvas/spatial_index.hpp"
    "packages/canvas/src/painter_scene_renderer.cpp"
    "packages/canvas/src/retained_scene.cpp"
    "packages/canvas/src/scene_cache.cpp"
    "packages/canvas/src/spatial_index.cpp"
    "packages/renderer/CMakeLists.txt"
    "packages/renderer/include/aimora/studio/renderer/scene_surface.hpp"
    "packages/renderer/shaders/scene.frag"
    "packages/renderer/shaders/scene.vert"
    "packages/renderer/src/scene_surface.cpp"
    "packages/inspector/CMakeLists.txt"
    "packages/commands/CMakeLists.txt"
    "packages/commands/include/aimora/studio/commands/drawing_interaction.hpp"
    "packages/commands/src/drawing_interaction.cpp"
    "packages/themes/CMakeLists.txt"
    "packages/themes/include/aimora/studio/themes/theme_system.hpp"
    "packages/themes/src/theme_system.cpp"
    "packages/shell/CMakeLists.txt"
    "packages/shell/include/aimora/studio/shell/studio_shell.hpp"
    "packages/shell/src/drawing_workspace.cpp"
    "packages/shell/src/studio_dock_widget.cpp"
    "packages/shell/src/studio_main_window.cpp"
    "packages/shell/src/studio_menus.cpp"
    "packages/shell/src/studio_panels.cpp"
    "packages/shell/src/workspace_settings.cpp"
    "tests/CMakeLists.txt"
    "tests/foundation_tests.cpp"
    "tests/canvas_tests.cpp"
    "tests/renderer_tests.cpp"
    "tests/interaction_tests.cpp"
    "tests/shell_tests.cpp"
    "tests/fixtures/theme-light.json"
    "tests/fixtures/theme-dark.json"
)

foreach(relative_path IN LISTS required_paths)
    if(NOT EXISTS "${AIMORA_SOURCE_DIR}/${relative_path}")
        message(FATAL_ERROR "Missing native Studio shell path: ${relative_path}")
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
list(FILTER forbidden_client_files EXCLUDE REGEX "/build[^/]*/")

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

set(shell_source "")
foreach(shell_file IN ITEMS
    studio_main_window.cpp
    studio_menus.cpp
    studio_panels.cpp
)
    file(READ "${AIMORA_SOURCE_DIR}/packages/shell/src/${shell_file}" shell_part)
    string(APPEND shell_source "${shell_part}")
endforeach()
foreach(required_menu IN ITEMS File Edit View Draw Modify Electrical Studies Results Output Tools Help)
    string(FIND "${shell_source}" "\"&${required_menu}\"" menu_offset)
    if(menu_offset EQUAL -1)
        message(FATAL_ERROR "Missing required native menu family: ${required_menu}")
    endif()
endforeach()

string(FIND "${shell_source}" "QToolBar" toolbar_offset)
if(NOT toolbar_offset EQUAL -1)
    message(FATAL_ERROR "The drawing-first shell must not create a permanent toolbar.")
endif()

file(READ "${AIMORA_SOURCE_DIR}/apps/studio/main.cpp" application_source)
string(FIND "${application_source}" "setHighDpiScaleFactorRoundingPolicy" high_dpi_offset)
if(high_dpi_offset EQUAL -1)
    message(FATAL_ERROR "The native application must set an explicit high-DPI rounding policy.")
endif()

execute_process(
    COMMAND
        "${CMAKE_COMMAND}"
        -DAIMORA_SOURCE_DIR=${AIMORA_SOURCE_DIR}
        -P
        "${AIMORA_SOURCE_DIR}/cmake/VerifyThemeFixtures.cmake"
    RESULT_VARIABLE fixture_result
)
if(NOT fixture_result EQUAL 0)
    message(FATAL_ERROR "Committed theme fixture verification failed.")
endif()

message(STATUS "AIMORAStudio native shell source-tree contract passed.")
