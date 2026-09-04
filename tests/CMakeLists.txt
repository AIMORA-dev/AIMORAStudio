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

add_executable(aimora_studio_canvas_tests canvas_tests.cpp)
target_link_libraries(
    aimora_studio_canvas_tests
    PRIVATE
        AIMORA::StudioCanvas
        Qt6::Gui
        Qt6::Widgets
        Qt6::Test
)
aimora_target_defaults(aimora_studio_canvas_tests)

add_executable(aimora_studio_renderer_tests renderer_tests.cpp)
target_link_libraries(
    aimora_studio_renderer_tests
    PRIVATE
        AIMORA::StudioRenderer
        Qt6::Gui
        Qt6::Widgets
        Qt6::Test
)
aimora_target_defaults(aimora_studio_renderer_tests)

if(AIMORA_ENABLE_CLANG_TIDY)
    set_property(TARGET aimora_studio_canvas_tests PROPERTY CXX_CLANG_TIDY "")
    set_property(TARGET aimora_studio_renderer_tests PROPERTY CXX_CLANG_TIDY "")
endif()

add_executable(aimora_studio_mock_service mock_service.cpp)
target_link_libraries(
    aimora_studio_mock_service
    PRIVATE
        AIMORA::StudioProtocol
        Qt6::Core
        Qt6::Network
)
aimora_target_defaults(aimora_studio_mock_service)

add_executable(aimora_studio_protocol_tests protocol_tests.cpp)
target_link_libraries(
    aimora_studio_protocol_tests
    PRIVATE
        AIMORA::StudioProtocol
        Qt6::Core
        Qt6::Network
        Qt6::Test
)
aimora_target_defaults(aimora_studio_protocol_tests)
add_dependencies(aimora_studio_protocol_tests aimora_studio_mock_service)

if(AIMORA_ENABLE_CLANG_TIDY)
    set_property(TARGET aimora_studio_protocol_tests PROPERTY CXX_CLANG_TIDY "")
    set_property(TARGET aimora_studio_mock_service PROPERTY CXX_CLANG_TIDY "")
endif()

add_test(NAME aimora.foundation COMMAND aimora_studio_foundation_tests)
add_test(NAME aimora.shell COMMAND aimora_studio_shell_tests)
add_test(NAME aimora.canvas COMMAND aimora_studio_canvas_tests)
add_test(NAME aimora.renderer COMMAND aimora_studio_renderer_tests)
add_test(NAME aimora.protocol COMMAND aimora_studio_protocol_tests)
set_tests_properties(
    aimora.shell aimora.canvas aimora.renderer
    PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)
set_tests_properties(
    aimora.protocol
    PROPERTIES
        ENVIRONMENT "AIMORA_MOCK_SERVICE_PATH=$<TARGET_FILE:aimora_studio_mock_service>"
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
add_test(
    NAME aimora.protocol-source
    COMMAND
        ${CMAKE_COMMAND}
        -DAIMORA_SOURCE_DIR=${PROJECT_SOURCE_DIR}
        -P ${PROJECT_SOURCE_DIR}/cmake/VerifyServiceProtocol.cmake
)
