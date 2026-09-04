# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
cmake_minimum_required(VERSION 3.28)

project(
    AIMORAStudio
    VERSION 0.2.0
    DESCRIPTION "Native desktop client for AIMORA"
    HOMEPAGE_URL "https://github.com/ahmelkholy/AIMORAStudio"
    LANGUAGES CXX
)

list(PREPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(CTest)
include(GNUInstallDirs)

set(AIMORA_QT_VERSION "6.11.2" CACHE STRING "Exact Qt version required by AIMORAStudio")
set_property(CACHE AIMORA_QT_VERSION PROPERTY STRINGS "6.11.2")

option(AIMORA_WARNINGS_AS_ERRORS "Treat AIMORAStudio compiler warnings as errors" OFF)
option(AIMORA_ENABLE_CLANG_TIDY "Run clang-tidy while compiling AIMORAStudio targets" OFF)
option(AIMORA_ENABLE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC OFF)
set(CMAKE_AUTOUIC OFF)

find_package(
    Qt6 ${AIMORA_QT_VERSION} EXACT
    REQUIRED COMPONENTS
        Core
        Gui
        Widgets
        Network
        PrintSupport
        Concurrent
)

find_package(Qt6 ${AIMORA_QT_VERSION} EXACT REQUIRED COMPONENTS GuiPrivate ShaderTools)

if(BUILD_TESTING)
    find_package(Qt6 ${AIMORA_QT_VERSION} EXACT REQUIRED COMPONENTS Test)
endif()

if(NOT Qt6_VERSION VERSION_EQUAL AIMORA_QT_VERSION)
    message(
        FATAL_ERROR
        "AIMORAStudio requires Qt ${AIMORA_QT_VERSION} exactly; found Qt ${Qt6_VERSION}."
    )
endif()

include(AIMORATargetDefaults)

file(MAKE_DIRECTORY "${PROJECT_BINARY_DIR}/generated/aimora/studio/core")
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/version.hpp.in"
    "${PROJECT_BINARY_DIR}/generated/aimora/studio/core/version.hpp"
    @ONLY
)

add_subdirectory(packages/core)
add_subdirectory(packages/protocol)
add_subdirectory(packages/canvas)
add_subdirectory(packages/renderer)
add_subdirectory(packages/inspector)
add_subdirectory(packages/catalog)
add_subdirectory(packages/commands)
add_subdirectory(packages/themes)
add_subdirectory(packages/shell)
add_subdirectory(apps/studio)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

include(AIMORAInstall)
