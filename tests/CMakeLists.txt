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
        Qt6::Widgets
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

add_executable(aimora_studio_interaction_tests interaction_tests.cpp)
target_link_libraries(
    aimora_studio_interaction_tests
    PRIVATE
        AIMORA::StudioCommands
        AIMORA::StudioShell
        Qt6::Gui
        Qt6::Widgets
        Qt6::Test
)
aimora_target_defaults(aimora_studio_interaction_tests)

if(AIMORA_ENABLE_CLANG_TIDY)
    set_property(TARGET aimora_studio_canvas_tests PROPERTY CXX_CLANG_TIDY "")
    set_property(TARGET aimora_studio_renderer_tests PROPERTY CXX_CLANG_TIDY "")
    set_property(TARGET aimora_studio_interaction_tests PROPERTY CXX_CLANG_TIDY "")
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
add_dependencies(aimora_studio_interaction_tests aimora_studio_mock_service)

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

add_executable(aimora_studio_inspector_tests inspector_tests.cpp)
target_link_libraries(
    aimora_studio_inspector_tests
    PRIVATE
        AIMORA::StudioInspector
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Qt6::Test
)
aimora_target_defaults(aimora_studio_inspector_tests)

add_executable(aimora_studio_catalog_tests catalog_tests.cpp)
target_link_libraries(
    aimora_studio_catalog_tests
    PRIVATE
        AIMORA::StudioCatalog
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Qt6::Test
)
aimora_target_defaults(aimora_studio_catalog_tests)

if(AIMORA_ENABLE_CLANG_TIDY)
    set_property(TARGET aimora_studio_protocol_tests PROPERTY CXX_CLANG_TIDY "")
    set_property(TARGET aimora_studio_mock_service PROPERTY CXX_CLANG_TIDY "")
    set_property(TARGET aimora_studio_inspector_tests PROPERTY CXX_CLANG_TIDY "")
    set_property(TARGET aimora_studio_catalog_tests PROPERTY CXX_CLANG_TIDY "")
endif()

add_test(NAME aimora.foundation COMMAND aimora_studio_foundation_tests)
add_test(NAME aimora.shell COMMAND aimora_studio_shell_tests)
add_test(NAME aimora.canvas COMMAND aimora_studio_canvas_tests)
add_test(NAME aimora.renderer COMMAND aimora_studio_renderer_tests)
add_test(NAME aimora.interaction COMMAND aimora_studio_interaction_tests)
add_test(NAME aimora.protocol COMMAND aimora_studio_protocol_tests)
add_test(NAME aimora.inspector COMMAND aimora_studio_inspector_tests)
add_test(NAME aimora.catalog COMMAND aimora_studio_catalog_tests)
set_tests_properties(
    aimora.foundation aimora.shell aimora.canvas aimora.renderer aimora.interaction aimora.inspector
    aimora.catalog
    PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)
set_tests_properties(
    aimora.protocol
    PROPERTIES
        ENVIRONMENT "AIMORA_MOCK_SERVICE_PATH=$<TARGET_FILE:aimora_studio_mock_service>"
)
set_tests_properties(
    aimora.interaction
    PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;AIMORA_MOCK_SERVICE_PATH=$<TARGET_FILE:aimora_studio_mock_service>"
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

add_executable(aimora_studio_service_launch_tests service_launch_tests.cpp)
set_target_properties(aimora_studio_service_launch_tests PROPERTIES AUTOMOC ON)
target_link_libraries(aimora_studio_service_launch_tests PRIVATE aimora_studio_protocol Qt6::Test)
add_test(NAME aimora.service-launch COMMAND aimora_studio_service_launch_tests)

add_executable(aimora_studio_julia_drafting_tests julia_drafting_integration_tests.cpp)
set_target_properties(aimora_studio_julia_drafting_tests PROPERTIES AUTOMOC ON)
target_link_libraries(aimora_studio_julia_drafting_tests PRIVATE aimora_studio_protocol Qt6::Test)
set(AIMORA_NATIVE_JULIA_EXECUTABLE "" CACHE FILEPATH "Julia executable for opt-in native integration")
set(AIMORA_NATIVE_SERVICE_PROJECT "" CACHE PATH "Canonical AIMORAService checkout for native integration")
set(AIMORA_NATIVE_DRAWING_FIXTURE "" CACHE FILEPATH "Canonical project fixture script for native integration")
if(AIMORA_NATIVE_JULIA_EXECUTABLE AND AIMORA_NATIVE_SERVICE_PROJECT AND AIMORA_NATIVE_DRAWING_FIXTURE)
    add_test(NAME aimora.julia-drafting COMMAND aimora_studio_julia_drafting_tests)
    set_tests_properties(aimora.julia-drafting PROPERTIES
        LABELS "integration;julia"
        RUN_SERIAL TRUE
        ENVIRONMENT "AIMORA_NATIVE_JULIA_EXECUTABLE=${AIMORA_NATIVE_JULIA_EXECUTABLE};AIMORA_NATIVE_SERVICE_PROJECT=${AIMORA_NATIVE_SERVICE_PROJECT};AIMORA_NATIVE_DRAWING_FIXTURE=${AIMORA_NATIVE_DRAWING_FIXTURE};JULIA_NUM_THREADS=1;OPENBLAS_NUM_THREADS=1")
endif()

target_link_libraries(aimora_studio_julia_drafting_tests PRIVATE aimora_studio_shell)
if(TEST aimora.julia-drafting)
    set_property(TEST aimora.julia-drafting APPEND PROPERTY ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()

add_executable(aimora_studio_command_history_tests command_line_history_tests.cpp)
set_target_properties(aimora_studio_command_history_tests PROPERTIES AUTOMOC ON)
target_link_libraries(aimora_studio_command_history_tests PRIVATE aimora_studio_shell Qt6::Test)
add_test(NAME aimora.command-history COMMAND aimora_studio_command_history_tests)
set_tests_properties(aimora.command-history PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# Use a controlled installed font family, not arbitrary symbol-font fallback order.
# These local test copies stay in the build tree and are never packaged for distribution.
if(WIN32 AND EXISTS "$ENV{WINDIR}/Fonts/segoeui.ttf")
    set(native_test_font_directory "${CMAKE_CURRENT_BINARY_DIR}/offscreen-fonts")
    file(MAKE_DIRECTORY "${native_test_font_directory}")
    foreach(native_font_file IN ITEMS segoeui.ttf segoeuib.ttf segoeuii.ttf segoeuiz.ttf)
        if(EXISTS "$ENV{WINDIR}/Fonts/${native_font_file}")
            configure_file("$ENV{WINDIR}/Fonts/${native_font_file}"
                "${native_test_font_directory}/${native_font_file}" COPYONLY)
        endif()
    endforeach()
    foreach(native_widget_test IN ITEMS aimora.shell aimora.canvas aimora.renderer
            aimora.interaction aimora.inspector aimora.catalog aimora.command-history
            aimora.julia-drafting)
        if(TEST ${native_widget_test})
            set_property(TEST ${native_widget_test} APPEND PROPERTY ENVIRONMENT
                "QT_QPA_FONTDIR=${native_test_font_directory}")
        endif()
    endforeach()
endif()

add_executable(aimora_studio_curve_tessellation_tests curve_tessellation_tests.cpp)
set_target_properties(aimora_studio_curve_tessellation_tests PROPERTIES AUTOMOC ON)
target_link_libraries(aimora_studio_curve_tessellation_tests PRIVATE aimora_studio_canvas Qt6::Test)
add_test(NAME aimora.curve-tessellation COMMAND aimora_studio_curve_tessellation_tests)
