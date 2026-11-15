# SPDX-License-Identifier: LGPL-3.0-or-later

# AGENT-CONTRACT: one QML module, `QindaTK`, backed by the shared library
# `qindatk` (C++ types: layout engines, Theme, Density, Icon, DockModel) and
# the QML files under qml/. QML files are globbed so adding a control is
# "drop a .qml file here and re-run cmake"; the type name is the file name.
# Applications link QindaTK::qindatk only when they use the C++ API
# (DockModel from C++, Theme::instance()); QML-only users need the plugin
# on the import path and nothing else.

file(GLOB qindatk_qml_files RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}" CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/qml/*.qml")
# Scene data stays renderer-independent. SDKs without Quick3D still expose
# SceneView3D, which reports its renderer unavailable through the lazy Loader.
if(QINDATK_ENABLE_QUICK3D)
    qindatk_find_qt_module(Quick3D QUIET)
endif()
# AGENT-GUARD: test QINDATK_WITH_QUICK3D, never TARGET Qt6::Quick3D alone;
# another package may define the target while the option is OFF (D-201).
if(QINDATK_ENABLE_QUICK3D AND TARGET Qt6::Quick3D)
    set(QINDATK_WITH_QUICK3D TRUE)
else()
    set(QINDATK_WITH_QUICK3D FALSE)
endif()
set(QINDATK_WITH_QUICK3D ${QINDATK_WITH_QUICK3D} PARENT_SCOPE)
set(qindatk_quick3d_sources)
if(NOT QINDATK_WITH_QUICK3D)
    list(FILTER qindatk_qml_files EXCLUDE REGEX "(SceneView3DRenderer|MaterialPreviewRenderer|ScientificPlot3DRenderer)\\.qml$")
    message(STATUS "QindaTK: optional SceneView3D renderer unavailable (Qt Quick 3D not installed)")
else()
    list(APPEND qindatk_quick3d_sources
        core/mesh_rig_geometry.h core/mesh_rig_geometry.cpp
        core/scientific_geometry3d.h core/scientific_geometry3d.cpp)
endif()
file(GLOB qindatk_js_files RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}" CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/qml/*.js")

# Singleton QML types are marked here (pragma Singleton alone is not enough
# for a compiled module), and every file is published flat (Box.qml, not
# qml/Box.qml) so the installed module reads like any other Qt module.
foreach(qml_file IN LISTS qindatk_qml_files qindatk_js_files)
    get_filename_component(qml_name "${qml_file}" NAME)
    set_source_files_properties("${qml_file}" PROPERTIES QT_RESOURCE_ALIAS "${qml_name}")
    # AGENT-GUARD (D-355, docs/android.md): on Android no qmldir may name a .js
    # file. androiddeployqt's android_rcc_bundle carries only qmldir and the
    # typeinfo, and the engine resolves a qmldir *script* entry against that
    # bundle path (not the module's `prefer`red qrc path), so one listed script
    # makes `import QindaTK` fail with "Script qrc:/android_rcc_bundle/qml/QindaTK/X.js
    # unavailable". The module's own QML imports these by relative path
    # (`import "X.js" as X`), which resolves inside the module resources, so
    # the entries are skipped there; desktop qmldir stays unchanged.
    if(ANDROID AND qml_file MATCHES "\\.m?js$")
        set_source_files_properties("${qml_file}" PROPERTIES QT_QML_SKIP_QMLDIR_ENTRY TRUE)
    endif()
    file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/${qml_file}" singleton_pragma REGEX "^pragma Singleton")
    if(singleton_pragma)
        set_source_files_properties("${qml_file}" PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
    endif()
endforeach()

# The library's C++ sources are listed by domain under cmake/ (paths are
# relative to this directory). Add a new .cpp/.h to the list for its domain.
include(cmake/qindatk_sources_app.cmake)
include(cmake/qindatk_sources_audio.cmake)
include(cmake/qindatk_sources_timeline_media.cmake)
include(cmake/qindatk_sources_text_data.cmake)
include(cmake/qindatk_sources_layout_canvas.cmake)

# AGENT-NOTE (D-354): swap in the "unavailable" implementations for desktop
# libraries a build omits. Each replacement keeps the QML-visible API of the
# file it replaces and reports ErrorCode::Unavailable / an explanatory error.
if(NOT QINDATK_ENABLE_LCMS2)
    list(REMOVE_ITEM qindatk_app_sources core/color_engine.cpp core/color_profile.cpp)
    list(APPEND qindatk_app_sources core/color_unavailable.cpp)
endif()
if(NOT QINDATK_ENABLE_LIBVTERM)
    list(REMOVE_ITEM qindatk_app_sources core/terminal_session.cpp
        core/terminal_session_screen.cpp core/terminal_session_input.cpp
        core/terminal_session_protocol.cpp core/terminal_session_protocol_p.h
        core/terminal_session_osc.cpp)
    list(APPEND qindatk_app_sources core/terminal_session_unavailable.cpp)
endif()
if(NOT QINDATK_ENABLE_SQLITE)
    list(REMOVE_ITEM qindatk_text_data_sources core/local_database_provider.cpp)
    list(APPEND qindatk_text_data_sources core/local_database_provider_unavailable.cpp)
endif()

# Desktop global menu (D-361): the dbusmenu transport needs Qt6::DBus. Without
# it GlobalMenuExport stays in the module, reports "unavailable" and every
# Tk.MenuBar keeps its in-window bar.
if(QINDATK_ENABLE_DBUS)
    qindatk_find_qt_module(DBus QUIET)
endif()
if(NOT (QINDATK_ENABLE_DBUS AND TARGET Qt6::DBus))
    list(REMOVE_ITEM qindatk_app_sources core/global_menu_server.cpp core/global_menu_server_p.h
        core/global_menu_hosted_p.h)
endif()
qt_add_library(qindatk SHARED)
set_target_properties(qindatk PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
    EXPORT_NAME qindatk)
qindatk_enable_warnings(qindatk)

qt_add_qml_module(qindatk
    URI QindaTK
    VERSION 1.0
    CLASS_NAME QindaTKPlugin
    PLUGIN_TARGET qindatkplugin
    DEPENDENCIES QtQuick QtQuick.Controls QtQuick.Templates
    SOURCES
        ${qindatk_quick3d_sources}
        ${qindatk_app_sources}
        ${qindatk_audio_sources}
        ${qindatk_timeline_media_sources}
        ${qindatk_text_data_sources}
        ${qindatk_layout_canvas_sources}
    QML_FILES ${qindatk_qml_files} ${qindatk_js_files}
)


target_include_directories(qindatk PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/core>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_link_libraries(qindatk
    PUBLIC Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::Network
    PRIVATE Qt6::Svg Qt6::Concurrent)
# AGENT-NOTE (D-354): each of these three desktop libraries has a source swap
# in src/cmake/qindatk_sources_*.cmake. OFF compiles an "unavailable" variant of
# the same C++ API instead (color_unavailable.cpp, local_database_provider_
# unavailable.cpp, terminal_session_unavailable.cpp), so QML still loads.
if(QINDATK_ENABLE_LCMS2)
    target_link_libraries(qindatk PRIVATE PkgConfig::LCMS2)
endif()
if(QINDATK_ENABLE_SQLITE)
    target_link_libraries(qindatk PRIVATE SQLite3::SQLite3)
endif()
# AGENT-CONTRACT (core/terminal_session.h): libvterm types appear in the public
# header, so the build mode travels with the exported target: PUBLIC.
if(QINDATK_ENABLE_LIBVTERM)
    target_link_libraries(qindatk PUBLIC PkgConfig::VTERM)
else()
    target_compile_definitions(qindatk PUBLIC QINDATK_NO_LIBVTERM=1)
endif()
if(QINDATK_WITH_QUICK3D)
    target_link_libraries(qindatk PRIVATE Qt6::Quick3D)
endif()
# GPU volume raycasting (D-147) is optional. Without Qt Shader Tools the
# resources are absent, VolumeView reports "GPU volume shaders are not built"
# and draws the reference path. 3D samplers need GLSL 300 es / 330.
# AGENT-CONTRACT (core/volume_render_node.cpp): PREFIX + file path is the
# resource path the node loads, ":/qindatk/shaders/volume_raycast.*.qsb".
if(QINDATK_ENABLE_SHADER_TOOLS)
    qindatk_find_qt_module(ShaderTools QUIET)
endif()
set(QINDATK_WITH_SHADER_TOOLS FALSE PARENT_SCOPE)
if(QINDATK_ENABLE_SHADER_TOOLS AND TARGET Qt6::ShaderTools)
    set(QINDATK_WITH_SHADER_TOOLS TRUE PARENT_SCOPE)
    qt_add_shaders(qindatk "qindatk_volume_shaders"
        PREFIX "/qindatk"
        GLSL "300es,330"
        NOHLSL
        NOMSL
        FILES
            shaders/volume_raycast.vert
            shaders/volume_raycast.frag)
else()
    message(STATUS "QindaTK: GPU volume shaders unavailable (Qt Shader Tools not installed)")
endif()
if(QINDATK_ENABLE_POPPLER_QT6 AND POPPLER_QT6_FOUND)
    target_link_libraries(qindatk PRIVATE PkgConfig::POPPLER_QT6)
    target_compile_definitions(qindatk PRIVATE QINDATK_HAVE_POPPLER_QT6=1)
endif()
if(QINDATK_ENABLE_ALSA AND QINDATK_ALSA_FOUND)
    target_sources(qindatk PRIVATE core/audio_device_alsa.cpp)
    target_link_libraries(qindatk PRIVATE PkgConfig::QINDATK_ALSA)
    target_compile_definitions(qindatk PRIVATE QINDATK_HAVE_ALSA=1)
endif()
# The media backend's FFmpeg headers stay inside qindatk's .cpp files and
# media_ffmpeg_p.h, so consumers never need them (D-096).
if(QINDATK_ENABLE_FFMPEG AND FFMPEG_FOUND)
    target_link_libraries(qindatk PRIVATE PkgConfig::FFMPEG)
    target_compile_definitions(qindatk PRIVATE QINDATK_HAVE_FFMPEG=1)
endif()
# Qt Quick's private tablet-event hook is confined to stylus_handler.cpp.
target_link_libraries(qindatk PRIVATE Qt6::QuickPrivate)
# Notification delivery is optional; headless/minimal SDK builds retain the
# same API and report available=false without a D-Bus dependency.
if(QINDATK_ENABLE_DBUS)
    qindatk_find_qt_module(DBus QUIET)
endif()
if(QINDATK_ENABLE_DBUS AND TARGET Qt6::DBus)
    # Keep optional D-Bus headers local to the adapter; the rest of the
    # toolkit and its public header remain usable without this Qt module.
    target_link_libraries(qindatk PRIVATE "$<LINK_ONLY:Qt6::DBus>")
    set_source_files_properties(core/desktop_notifications.cpp core/global_menu_export.cpp PROPERTIES
        COMPILE_DEFINITIONS QINDATK_HAVE_DBUS=1
        INCLUDE_DIRECTORIES "$<TARGET_PROPERTY:Qt6::DBus,INTERFACE_INCLUDE_DIRECTORIES>")
    # The global-menu transport headers (and their moc output) use QtDBus types.
    target_include_directories(qindatk PRIVATE "$<TARGET_PROPERTY:Qt6::DBus,INTERFACE_INCLUDE_DIRECTORIES>")
endif()

# ---- out-of-process plugin host (WP-066, D-190, docs/audio-plugins.md) ----
# AGENT-CONTRACT: qindatk never loads plugin code. It starts this executable
# (one process per plugin instance, and one per scanned binary) and talks to
# it through core/audio_plugin_ipc_p.h. The CLAP headers (MIT, vendored in
# third_party/clap) are used only here and by the test plugins.
if(QINDATK_ENABLE_PLUGIN_HOST)
    qt_add_executable(qindatk-plugin-host
        pluginhost/plugin_host_main.cpp
        pluginhost/plugin_host_library.cpp
        pluginhost/plugin_host_instance.cpp
        pluginhost/plugin_host_editor.cpp
        pluginhost/plugin_host_audio.cpp
        pluginhost/plugin_host_clap.h)
    qindatk_enable_warnings(qindatk-plugin-host)
    target_include_directories(qindatk-plugin-host PRIVATE core "${PROJECT_SOURCE_DIR}/third_party/clap/include")
    target_link_libraries(qindatk-plugin-host PRIVATE Qt6::Core ${CMAKE_DL_LIBS})
    set_target_properties(qindatk-plugin-host PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/libexec")
    install(TARGETS qindatk-plugin-host RUNTIME DESTINATION "${CMAKE_INSTALL_LIBEXECDIR}/qindatk")
    # AGENT-CONTRACT (core/audio_plugin_types.cpp hostExecutable(), D-202): only
    # paths relative to libqindatk's directory are compiled in, never the build or
    # install prefix, so an installed QindaTK stays relocatable.
    file(RELATIVE_PATH qindatk_host_build_rel "${CMAKE_CURRENT_BINARY_DIR}" "${CMAKE_BINARY_DIR}/libexec/qindatk-plugin-host")
    file(RELATIVE_PATH qindatk_host_install_rel "${CMAKE_INSTALL_FULL_LIBDIR}"
        "${CMAKE_INSTALL_FULL_LIBEXECDIR}/qindatk/qindatk-plugin-host")
    set_source_files_properties(core/audio_plugin_types.cpp PROPERTIES COMPILE_DEFINITIONS
        "QINDATK_PLUGIN_HOST_BUILD_REL=\"${qindatk_host_build_rel}\";QINDATK_PLUGIN_HOST_INSTALL_REL=\"${qindatk_host_install_rel}\"")
    add_dependencies(qindatk qindatk-plugin-host)
else()
    # Android cannot exec a sibling binary: no host, so scans/instances report
    # a missing host. The macros still need values for audio_plugin_types.cpp.
    set_source_files_properties(core/audio_plugin_types.cpp PROPERTIES COMPILE_DEFINITIONS
        "QINDATK_PLUGIN_HOST_BUILD_REL=\"\";QINDATK_PLUGIN_HOST_INSTALL_REL=\"\"")
endif()

if(QINDATK_ENABLE_WIDGETS AND TARGET Qt6::Widgets)
    add_library(qindatk_widgets STATIC
        platform_widgets/native_file_dialog_backend.cpp
        platform_widgets/native_file_dialog_backend.h)
    set_target_properties(qindatk_widgets PROPERTIES EXPORT_NAME qindatk_widgets)
    qindatk_enable_warnings(qindatk_widgets)
    target_include_directories(qindatk_widgets PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/core>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/platform_widgets>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/qindatk>)
    target_link_libraries(qindatk_widgets PUBLIC qindatk Qt6::Widgets)
    # AGENT-NOTE (WP-053): the native print backend needs Qt PrintSupport,
    # which depends on Widgets; base qindatk keeps only the PDF writer.
    if(QINDATK_ENABLE_NATIVE_PRINT AND TARGET Qt6::PrintSupport)
        target_sources(qindatk_widgets PRIVATE
            platform_widgets/native_print_backend.cpp
            platform_widgets/native_print_backend.h)
        target_link_libraries(qindatk_widgets PUBLIC Qt6::PrintSupport)
        target_compile_definitions(qindatk_widgets PUBLIC QINDATK_HAVE_NATIVE_PRINT=1)
        # AGENT-NOTE (PLT-04): jobs go to CUPS through cupsPrintFile2, whose
        # job ID is the only proof the queue accepted them.
        if(TARGET Cups::Cups)
            target_link_libraries(qindatk_widgets PRIVATE Cups::Cups)
            target_compile_definitions(qindatk_widgets PRIVATE QINDATK_HAVE_CUPS=1)
            set(QINDATK_WIDGETS_CUPS TRUE PARENT_SCOPE)
        endif()
        install(FILES platform_widgets/native_print_backend.h
            DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/qindatk)
        set(QINDATK_WIDGETS_PRINT TRUE PARENT_SCOPE)
    endif()
    install(TARGETS qindatk_widgets EXPORT QindaTKWidgetsTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
    install(EXPORT QindaTKWidgetsTargets NAMESPACE QindaTK::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/QindaTKWidgets)
    install(FILES platform_widgets/native_file_dialog_backend.h
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/qindatk)
    set(QINDATK_WITH_WIDGETS TRUE PARENT_SCOPE)
endif()

# ---- optional bridge to the QindaQt desktop's QST-1 tokens (pure QML) ----
# Importing QindaTK.QindaQt requires QindaQt.Tokens at run time; the module
# itself builds everywhere so a package can ship it unconditionally.
set_source_files_properties(bridge/QindaQtTheme.qml PROPERTIES QT_RESOURCE_ALIAS QindaQtTheme.qml)
qt_add_library(qindatk_qindaqt SHARED)
qt_add_qml_module(qindatk_qindaqt
    URI QindaTK.QindaQt
    VERSION 1.0
    CLASS_NAME QindaTKQindaQtPlugin
    PLUGIN_TARGET qindatk_qindaqtplugin
    QML_FILES bridge/QindaQtTheme.qml
)
target_link_libraries(qindatk_qindaqt PRIVATE Qt6::Qml)

# ---- install -------------------------------------------------------------
# AGENT-NOTE (D-202): installing into Qt's own prefix (a distro package)
# puts the modules on Qt's default import path. Any other prefix keeps them
# inside that prefix, relative, so a staged install can be moved; the
# WP-088 relocation check (tools/qualify) runs a consumer from the moved stage.
if(NOT DEFINED QINDATK_QML_INSTALL_DIR)
    cmake_path(COMPARE "${CMAKE_INSTALL_PREFIX}" EQUAL "${QT6_INSTALL_PREFIX}" qindatk_into_qt_prefix)
    if(qindatk_into_qt_prefix)
        set(qindatk_default_qml_dir "${QT6_INSTALL_PREFIX}/${QT6_INSTALL_QML}")
    else()
        set(qindatk_default_qml_dir "${CMAKE_INSTALL_LIBDIR}/qml")
    endif()
    set(QINDATK_QML_INSTALL_DIR "${qindatk_default_qml_dir}" CACHE PATH
        "Root QML import directory to install the QindaTK modules into (relative: under the install prefix)")
endif()
set(QINDATK_QML_INSTALL_DIR "${QINDATK_QML_INSTALL_DIR}" PARENT_SCOPE)

# AGENT-GUARD (D-202): the QML plugins live in <qml>/QindaTK and
# <qml>/QindaTK/QindaQt, not in bindir, so Qt's standard install RPATH
# ($ORIGIN/../lib) does not reach libqindatk from them. A QML-only
# application (one that does not link qindatk itself) then fails to load the
# plugin from any prefix outside the linker's default path. Each plugin gets
# an $ORIGIN-relative RUNPATH to the library directory instead.
cmake_path(ABSOLUTE_PATH QINDATK_QML_INSTALL_DIR BASE_DIRECTORY "${CMAKE_INSTALL_PREFIX}"
    NORMALIZE OUTPUT_VARIABLE qindatk_abs_qml_dir)
cmake_path(ABSOLUTE_PATH CMAKE_INSTALL_LIBDIR BASE_DIRECTORY "${CMAKE_INSTALL_PREFIX}"
    NORMALIZE OUTPUT_VARIABLE qindatk_abs_lib_dir)
file(RELATIVE_PATH qindatk_plugin_to_lib "${qindatk_abs_qml_dir}/QindaTK" "${qindatk_abs_lib_dir}")
file(RELATIVE_PATH qindatk_bridge_to_lib "${qindatk_abs_qml_dir}/QindaTK/QindaQt" "${qindatk_abs_lib_dir}")
set_target_properties(qindatkplugin PROPERTIES INSTALL_RPATH "$ORIGIN/${qindatk_plugin_to_lib}")
set_target_properties(qindatk_qindaqt qindatk_qindaqtplugin PROPERTIES
    INSTALL_RPATH "$ORIGIN;$ORIGIN/${qindatk_bridge_to_lib}")

install(TARGETS qindatk EXPORT QindaTKTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(TARGETS qindatkplugin
    LIBRARY DESTINATION "${QINDATK_QML_INSTALL_DIR}/QindaTK"
    RUNTIME DESTINATION "${QINDATK_QML_INSTALL_DIR}/QindaTK")
install(TARGETS qindatk_qindaqt qindatk_qindaqtplugin
    LIBRARY DESTINATION "${QINDATK_QML_INSTALL_DIR}/QindaTK/QindaQt"
    RUNTIME DESTINATION "${QINDATK_QML_INSTALL_DIR}/QindaTK/QindaQt")
install(DIRECTORY "${CMAKE_BINARY_DIR}/qml/QindaTK/"
    DESTINATION "${QINDATK_QML_INSTALL_DIR}/QindaTK"
    FILES_MATCHING PATTERN "qmldir" PATTERN "*.qmltypes" PATTERN "*.qml" PATTERN "*.js")
install(DIRECTORY "${CMAKE_BINARY_DIR}/qml/QindaTK/QindaQt/"
    DESTINATION "${QINDATK_QML_INSTALL_DIR}/QindaTK/QindaQt"
    FILES_MATCHING PATTERN "qmldir" PATTERN "*.qmltypes" PATTERN "*.qml")
install(DIRECTORY core/ DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/qindatk"
    FILES_MATCHING PATTERN "*.h" PATTERN "*_p.h" EXCLUDE)
install(EXPORT QindaTKTargets NAMESPACE QindaTK::
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/QindaTK")

# AGENT-GUARD (D-355): fail the Android build when the module's qmldir names a
# file that is missing from the module directory or one the Android bundle
# cannot reach (a script). Runs on every Android build, after the plugin.
if(ANDROID)
    add_custom_target(qindatk_check_android_bundle ALL
        COMMAND ${CMAKE_COMMAND}
            -DMODULE_DIR=${CMAKE_BINARY_DIR}/qml/QindaTK
            -DANDROID_BUNDLE=ON
            -P ${PROJECT_SOURCE_DIR}/cmake/CheckQmlModuleBundle.cmake
        COMMENT "Checking the QindaTK qmldir against the Android bundle rules"
        VERBATIM)
    add_dependencies(qindatk_check_android_bundle qindatk qindatkplugin)
endif()
