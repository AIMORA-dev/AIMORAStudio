# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
include(AIMORACompilerWarnings)
include(AIMORASanitizers)
include(AIMORAStaticAnalysis)

function(aimora_target_defaults target)
    target_compile_features(${target} PUBLIC cxx_std_20)
    set_target_properties(
        ${target}
        PROPERTIES
            CXX_EXTENSIONS OFF
            POSITION_INDEPENDENT_CODE ON
    )

    aimora_enable_compiler_warnings(${target})
    aimora_enable_sanitizers(${target})
    aimora_enable_static_analysis(${target})
endfunction()
