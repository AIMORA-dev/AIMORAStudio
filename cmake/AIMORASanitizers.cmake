# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
function(aimora_enable_sanitizers target)
    if(NOT AIMORA_ENABLE_SANITIZERS)
        return()
    endif()

    if(MSVC)
        target_compile_options(${target} PRIVATE /fsanitize=address)
        return()
    endif()

    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(
            ${target}
            PRIVATE
                -fsanitize=address,undefined
                -fno-omit-frame-pointer
        )
        target_link_options(
            ${target}
            PRIVATE
                -fsanitize=address,undefined
                -fno-omit-frame-pointer
        )
        return()
    endif()

    message(FATAL_ERROR "Sanitizers are not configured for ${CMAKE_CXX_COMPILER_ID}.")
endfunction()
