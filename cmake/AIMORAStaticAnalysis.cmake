# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
function(aimora_enable_static_analysis target)
    if(NOT AIMORA_ENABLE_CLANG_TIDY)
        return()
    endif()

    find_program(AIMORA_CLANG_TIDY_EXECUTABLE NAMES clang-tidy REQUIRED)
    set_property(
        TARGET ${target}
        PROPERTY CXX_CLANG_TIDY
            "${AIMORA_CLANG_TIDY_EXECUTABLE};--config-file=${PROJECT_SOURCE_DIR}/.clang-tidy"
    )
endfunction()
