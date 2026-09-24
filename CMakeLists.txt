# SPDX-License-Identifier: LGPL-3.0-or-later
cmake_minimum_required(VERSION 3.25)

# AGENT-NOTE: this version, the QML module VERSION below, and git tags move
# together. Bump all three in one commit.
project(QindaTK VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

include(GNUInstallDirs)
include(CTest)

option(QINDATK_BUILD_EXAMPLES "Build the gallery and studio-shell examples" ON)
option(QINDATK_BUILD_TOOLS "Build qtk-preview (headless render/dump tool)" ON)
option(QINDATK_ENABLE_POPPLER_QT6 "Enable the optional Poppler Qt 6 PDF backend" ON)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2 Svg Concurrent)
find_package(SQLite3 REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(LCMS2 REQUIRED IMPORTED_TARGET lcms2>=2.14)
pkg_check_modules(VTERM REQUIRED IMPORTED_TARGET vterm>=0.3)
if(QINDATK_ENABLE_POPPLER_QT6)
    pkg_check_modules(POPPLER_QT6 QUIET IMPORTED_TARGET poppler-qt6)
endif()
# Native QFileDialog is an optional adapter; base qindatk stays QtQuick-only.
find_package(Qt6 6.8 QUIET COMPONENTS Widgets)
# AGENT-NOTE: QuickPrivate serves layout_info.cpp (anchor/explicit size
# detection) and StylusHandler's implementation-only tablet event path (D-015).
# Official Qt SDKs define Qt6::QuickPrivate from the Qt6Quick package;
# distro Qt packages may instead provide a separate Qt6QuickPrivate config.
set(QT_NO_PRIVATE_MODULE_WARNING ON)
if(NOT TARGET Qt6::QuickPrivate)
    find_package(Qt6QuickPrivate 6.8 REQUIRED)
endif()
if(BUILD_TESTING)
    find_package(Qt6 6.8 REQUIRED COMPONENTS Test QuickTest)
endif()

qt_standard_project_setup(REQUIRES 6.8)

# AGENT-CONTRACT: every QML module of this repository lands under
# ${CMAKE_BINARY_DIR}/qml so that tools, tests and examples can import
# QindaTK from the build tree with a single import path
# (`-I ${CMAKE_BINARY_DIR}/qml`, or QML_IMPORT_PATH). Installed builds land
# under ${QT6_INSTALL_QML} which is already on the engine's default path.
set(QT_QML_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/qml")
set(QINDATK_WITH_WIDGETS FALSE)

function(qindatk_enable_warnings target)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

add_subdirectory(src)

if(QINDATK_BUILD_TOOLS)
    add_subdirectory(tools/preview)
endif()

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

if(QINDATK_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

# ---- package config so applications can find_package(QindaTK) ----------
include(CMakePackageConfigHelpers)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/QindaTKConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion)
set(QINDATK_QML_INSTALL_DIR "${QINDATK_QML_INSTALL_DIR}/QindaTK")
configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/QindaTKConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/QindaTKConfig.cmake"
    INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/QindaTK"
    PATH_VARS QINDATK_QML_INSTALL_DIR)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/QindaTKConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/QindaTKConfigVersion.cmake"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/QindaTK")

if(QINDATK_WITH_WIDGETS)
    configure_package_config_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/QindaTKWidgetsConfig.cmake.in"
        "${CMAKE_CURRENT_BINARY_DIR}/QindaTKWidgetsConfig.cmake"
        INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/QindaTKWidgets")
    write_basic_package_version_file(
        "${CMAKE_CURRENT_BINARY_DIR}/QindaTKWidgetsConfigVersion.cmake"
        VERSION ${PROJECT_VERSION}
        COMPATIBILITY SameMajorVersion)
    install(FILES
        "${CMAKE_CURRENT_BINARY_DIR}/QindaTKWidgetsConfig.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/QindaTKWidgetsConfigVersion.cmake"
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/QindaTKWidgets")
endif()
