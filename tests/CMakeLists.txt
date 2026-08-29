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

add_test(NAME aimora.foundation COMMAND aimora_studio_foundation_tests)
add_test(
    NAME aimora.structure
    COMMAND
        ${CMAKE_COMMAND}
        -DAIMORA_SOURCE_DIR=${PROJECT_SOURCE_DIR}
        -P ${PROJECT_SOURCE_DIR}/cmake/VerifySourceTree.cmake
)
