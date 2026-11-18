# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
function(aimora_enable_compiler_warnings target)
    if(MSVC)
        target_compile_options(
            ${target}
            PRIVATE
                /W4
                /permissive-
                /Zc:__cplusplus
                /utf-8
        )
        if(AIMORA_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE /WX)
        endif()
        return()
    endif()

    target_compile_options(
        ${target}
        PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Wconversion
            -Wsign-conversion
            -Wshadow
            -Wold-style-cast
            -Wcast-align
            -Wnon-virtual-dtor
            -Woverloaded-virtual
            -Wnull-dereference
            -Wdouble-promotion
            -Wformat=2
    )

    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        target_compile_options(
            ${target}
            PRIVATE
                -Wduplicated-cond
                -Wduplicated-branches
                -Wlogical-op
        )
    endif()

    if(AIMORA_WARNINGS_AS_ERRORS)
        target_compile_options(${target} PRIVATE -Werror)
    endif()
endfunction()
