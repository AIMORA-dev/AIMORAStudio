# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
add_executable(aimora_studio_foundation_tests foundation_tests.cpp)

target_link_libraries(
    aimora_studio_foundation_tests
    PRIVATE
        AIMORA::StudioCore
        AIMORA::StudioProtocol
        AIMORA::StudioCanvas
        AIMORA::StudioInspector
        AIMORA::StudioCommands
        AIMORA::StudioThemes
        Qt6::Test
)

aimora_target_defaults(aimora_studio_foundation_tests)

if(AIMORA_ENABLE_CLANG_TIDY)
    set_property(TARGET aimora_studio_foundation_tests PROPERTY CXX_CLANG_TIDY "")
endif()

add_executable(aimora_studio_shell_tests shell_tests.cpp)
target_link_libraries(
    aimora_studio_shell_tests
    PRIVATE
        AIMORA::StudioShell
        AIMORA::StudioThemes
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Qt6::Test
)
target_compile_definitions(
    aimora_studio_shell_tests
    PRIVATE AIMORA_TEST_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
)
aimora_target_defaults(aimora_studio_shell_tests)

if(AIMORA_ENABLE_CLANG_TIDY)
    set_property(TARGET aimora_studio_shell_tests PROPERTY CXX_CLANG_TIDY "")
endif()

add_test(NAME aimora.foundation COMMAND aimora_studio_foundation_tests)
add_test(NAME aimora.shell COMMAND aimora_studio_shell_tests)
set_tests_properties(
    aimora.shell
    PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

add_test(
    NAME aimora.structure
    COMMAND
        ${CMAKE_COMMAND}
        -DAIMORA_SOURCE_DIR=${PROJECT_SOURCE_DIR}
        -P ${PROJECT_SOURCE_DIR}/cmake/VerifySourceTree.cmake
)
add_test(
    NAME aimora.theme-fixtures
    COMMAND
        ${CMAKE_COMMAND}
        -DAIMORA_SOURCE_DIR=${PROJECT_SOURCE_DIR}
        -P ${PROJECT_SOURCE_DIR}/cmake/VerifyThemeFixtures.cmake
)
