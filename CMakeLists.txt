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
option(QINDATK_ENABLE_ALSA "Enable the optional ALSA audio device backend" ON)
option(QINDATK_ENABLE_FFMPEG "Enable the optional FFmpeg media decode/encode backend" ON)
# AGENT-NOTE (D-201): every optional Qt module is behind an explicit switch as
# well as its QUIET find_package, so a package (a Gentoo USE flag) or the
# WP-088 qualification harness (tools/qualify) can omit it even when it is
# installed. OFF means "not searched", not merely "not linked".
option(QINDATK_ENABLE_QUICK3D "Enable the optional Qt Quick 3D renderers (SceneView3D, material and 3D plot views)" ON)
option(QINDATK_ENABLE_SHADER_TOOLS "Compile the optional GPU volume raycasting shaders with Qt Shader Tools" ON)
option(QINDATK_ENABLE_WIDGETS "Build the optional qindatk_widgets adapters (native file dialog) with Qt Widgets" ON)
option(QINDATK_ENABLE_NATIVE_PRINT "Add the native print backend (Qt PrintSupport) to qindatk_widgets" ON)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2 Svg Concurrent Network)
# AGENT-NOTE (D-199): every other Qt module, optional or not, is looked up
# with qindatk_find_qt_module(): only in the Qt6 installation this build uses
# (the directory holding Qt6_DIR) and only at exactly its version. A plain
# find_package(Qt6Quick3D 6.8) also searches the system prefixes, so a build
# against the Qt 6.8 SDK loaded the system Qt 6.11 Quick3D config and failed
# with "Unknown CMake command _qt_internal_should_include_targets".
# AGENT-GUARD: never add a bare find_package(Qt6<Module> ...) in this tree;
# `-DCMAKE_PREFIX_PATH=<Qt 6.8 SDK>` alone must configure (docs/platform-support.md).
get_filename_component(QINDATK_QT_CMAKE_DIR "${Qt6_DIR}/.." ABSOLUTE)
macro(qindatk_find_qt_module module)
    # A Qt6<module>_DIR cached from another Qt would win over PATHS; drop it.
    if(Qt6${module}_DIR)
        string(FIND "${Qt6${module}_DIR}" "${QINDATK_QT_CMAKE_DIR}/" _qindatk_qt_dir_at)
        if(NOT _qindatk_qt_dir_at EQUAL 0)
            unset(Qt6${module}_DIR CACHE)
            unset(Qt6${module}_DIR)
        endif()
    endif()
    find_package(Qt6${module} ${Qt6_VERSION} EXACT ${ARGN} CONFIG
        PATHS "${QINDATK_QT_CMAKE_DIR}" NO_DEFAULT_PATH)
endmacro()
find_package(SQLite3 REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(LCMS2 REQUIRED IMPORTED_TARGET lcms2>=2.14)
pkg_check_modules(VTERM REQUIRED IMPORTED_TARGET vterm>=0.3)
if(QINDATK_ENABLE_POPPLER_QT6)
    pkg_check_modules(POPPLER_QT6 QUIET IMPORTED_TARGET poppler-qt6)
    # AGENT-NOTE: a distro poppler-qt6 is built against the distro Qt. Linked
    # into a build against another Qt (such as the Qt 6.8 SDK) it fails on the
    # newer Qt's symbols, so the optional backend is used only when it links
    # with the Qt this build uses.
    if(POPPLER_QT6_FOUND)
        include(CheckCXXSourceCompiles)
        set(CMAKE_REQUIRED_LIBRARIES PkgConfig::POPPLER_QT6 Qt6::Core Qt6::Gui)
        check_cxx_source_compiles("
            #include <poppler-qt6.h>
            int main() { return Poppler::Document::load(QString()) ? 1 : 0; }"
            QINDATK_POPPLER_QT6_LINKS)
        unset(CMAKE_REQUIRED_LIBRARIES)
        if(NOT QINDATK_POPPLER_QT6_LINKS)
            message(STATUS "poppler-qt6 does not link with Qt ${Qt6_VERSION}; the Poppler PDF backend is off")
            set(POPPLER_QT6_FOUND FALSE)
        endif()
    endif()
endif()
# AGENT-NOTE: the audio engine always builds with its silent built-in
# reference devices; ALSA adds real Linux PCM devices (WP-060, D-088). Like
# Poppler it is used only when the library actually links.
if(QINDATK_ENABLE_ALSA)
    pkg_check_modules(QINDATK_ALSA QUIET IMPORTED_TARGET alsa>=1.1)
    if(QINDATK_ALSA_FOUND)
        include(CheckCXXSourceCompiles)
        set(CMAKE_REQUIRED_LIBRARIES PkgConfig::QINDATK_ALSA)
        check_cxx_source_compiles("
            #include <alsa/asoundlib.h>
            int main() { snd_pcm_t *pcm = nullptr; return snd_pcm_open(&pcm, \"null\", SND_PCM_STREAM_PLAYBACK, SND_PCM_NONBLOCK); }"
            QINDATK_ALSA_LINKS)
        unset(CMAKE_REQUIRED_LIBRARIES)
        if(NOT QINDATK_ALSA_LINKS)
            set(QINDATK_ALSA_FOUND FALSE)
        endif()
    endif()
    if(NOT QINDATK_ALSA_FOUND)
        message(STATUS "QindaTK: ALSA not found or not linkable; the audio engine offers built-in devices only")
    endif()
endif()
# AGENT-NOTE (D-096): the FFmpeg libraries are a private, optional media
# backend (decode, composite input, encode). Without them the media types
# still build and report an explicit "built without FFmpeg" capability error.
if(QINDATK_ENABLE_FFMPEG)
    pkg_check_modules(FFMPEG QUIET IMPORTED_TARGET
        libavformat>=60 libavcodec>=60 libavutil>=58 libswscale>=7 libswresample>=4.10)
    if(FFMPEG_FOUND)
        message(STATUS "QindaTK: FFmpeg media backend enabled (libavformat ${FFMPEG_libavformat_VERSION}, libavcodec ${FFMPEG_libavcodec_VERSION})")
    else()
        message(STATUS "QindaTK: FFmpeg libraries not found; the media backend reports unsupported")
    endif()
endif()
# Native QFileDialog is an optional adapter; base qindatk stays QtQuick-only.
if(QINDATK_ENABLE_WIDGETS)
    qindatk_find_qt_module(Widgets QUIET)
endif()
# The native print backend (WP-053) joins qindatk_widgets when PrintSupport exists.
if(QINDATK_ENABLE_WIDGETS AND QINDATK_ENABLE_NATIVE_PRINT)
    qindatk_find_qt_module(PrintSupport QUIET)
    # CUPS submits its printer jobs; without it the backend offers print-to-file only.
    if(TARGET Qt6::PrintSupport)
        find_package(Cups QUIET)
    endif()
endif()
# AGENT-NOTE: QuickPrivate serves layout_info.cpp (anchor/explicit size
# detection) and StylusHandler's implementation-only tablet event path (D-015).
# Official Qt SDKs define Qt6::QuickPrivate from the Qt6Quick package;
# distro Qt packages may instead provide a separate Qt6QuickPrivate config.
set(QT_NO_PRIVATE_MODULE_WARNING ON)
if(NOT TARGET Qt6::QuickPrivate)
    qindatk_find_qt_module(QuickPrivate REQUIRED)
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

add_subdirectory(src/agent)
add_subdirectory(src)

# AGENT-CONTRACT (tools/qualify/qualify_build.py, D-201): the optional
# engines this configuration actually resolved, for the qualification harness
# and packagers. Keys are stable; each value is true or false.
function(qindatk_write_feature_summary)
    set(qindatk_features "")
    foreach(pair IN ITEMS
            "poppler:QINDATK_ENABLE_POPPLER_QT6;POPPLER_QT6_FOUND"
            "alsa:QINDATK_ENABLE_ALSA;QINDATK_ALSA_FOUND"
            "ffmpeg:QINDATK_ENABLE_FFMPEG;FFMPEG_FOUND"
            "quick3d:QINDATK_WITH_QUICK3D"
            "shaderTools:QINDATK_WITH_SHADER_TOOLS"
            "widgets:QINDATK_WITH_WIDGETS"
            "nativePrint:QINDATK_WIDGETS_PRINT")
        string(REPLACE ":" ";" parts "${pair}")
        list(POP_FRONT parts key)
        set(value true)
        foreach(variable IN LISTS parts)
            if(NOT ${variable})
                set(value false)
            endif()
        endforeach()
        string(APPEND qindatk_features "  \"${key}\": ${value},\n")
    endforeach()
    file(WRITE "${CMAKE_BINARY_DIR}/qindatk-features.json"
        "{\n${qindatk_features}  \"qtVersion\": \"${Qt6_VERSION}\"\n}\n")
endfunction()
qindatk_write_feature_summary()

if(QINDATK_BUILD_TOOLS)
    add_subdirectory(tools/preview)
    install(PROGRAMS tools/agent/qinda-agent DESTINATION ${CMAKE_INSTALL_BINDIR})
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

# Portage packages this separately to keep file ownership independent.
option(QINDATK_INSTALL_RASTER_COMPANION "Install the optional raster QML companion with core" OFF)
if(QINDATK_INSTALL_RASTER_COMPANION)
    install(DIRECTORY modules/QindaTK/Raster DESTINATION "${QINDATK_QML_INSTALL_DIR}"
        FILES_MATCHING PATTERN "*.qml" PATTERN "qmldir")
endif()
