# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
include(CMakePackageConfigHelpers)

set(AIMORA_STUDIO_LIBRARY_TARGETS
    aimora_studio_core
    aimora_studio_protocol
    aimora_studio_canvas
    aimora_studio_inspector
    aimora_studio_commands
    aimora_studio_themes
    aimora_studio_shell
)

install(
    TARGETS
        ${AIMORA_STUDIO_LIBRARY_TARGETS}
        aimora_studio_app
    EXPORT AIMORAStudioTargets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

foreach(package_name IN ITEMS core protocol canvas inspector commands themes shell)
    install(
        DIRECTORY "${PROJECT_SOURCE_DIR}/packages/${package_name}/include/"
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
        FILES_MATCHING PATTERN "*.hpp"
    )
endforeach()

install(
    FILES "${PROJECT_BINARY_DIR}/generated/aimora/studio/core/version.hpp"
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/aimora/studio/core"
)

configure_package_config_file(
    "${PROJECT_SOURCE_DIR}/cmake/AIMORAStudioConfig.cmake.in"
    "${PROJECT_BINARY_DIR}/AIMORAStudioConfig.cmake"
    INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/AIMORAStudio"
)

write_basic_package_version_file(
    "${PROJECT_BINARY_DIR}/AIMORAStudioConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(
    EXPORT AIMORAStudioTargets
    FILE AIMORAStudioTargets.cmake
    NAMESPACE AIMORA::
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/AIMORAStudio"
)

install(
    FILES
        "${PROJECT_BINARY_DIR}/AIMORAStudioConfig.cmake"
        "${PROJECT_BINARY_DIR}/AIMORAStudioConfigVersion.cmake"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/AIMORAStudio"
)

set(CPACK_PACKAGE_NAME "AIMORAStudio")
set(CPACK_PACKAGE_VENDOR "AIMORA")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Native AIMORAStudio desktop shell")
set(CPACK_RESOURCE_FILE_LICENSE "${PROJECT_SOURCE_DIR}/LICENSE")
set(CPACK_GENERATOR "TGZ;ZIP")
include(CPack)
