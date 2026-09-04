# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
add_library(
    aimora_studio_catalog STATIC
    include/aimora/studio/catalog/catalog_library.hpp
    src/catalog_library.cpp
)
add_library(AIMORA::StudioCatalog ALIAS aimora_studio_catalog)

set_target_properties(aimora_studio_catalog PROPERTIES EXPORT_NAME StudioCatalog)

target_include_directories(
    aimora_studio_catalog
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_link_libraries(
    aimora_studio_catalog
    PUBLIC
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
)

qt_add_resources(
    aimora_studio_catalog
    "aimora_equipment_catalog"
    PREFIX "/aimora/catalog"
    BASE resources
    FILES resources/equipment_catalog.json
)

aimora_target_defaults(aimora_studio_catalog)
