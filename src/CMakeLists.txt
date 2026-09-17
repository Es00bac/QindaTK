# SPDX-License-Identifier: LGPL-3.0-or-later

# AGENT-CONTRACT: one QML module, `QindaTK`, backed by the shared library
# `qindatk` (C++ types: layout engines, Theme, Density, Icon, DockModel) and
# the QML files under qml/. QML files are globbed so adding a control is
# "drop a .qml file here and re-run cmake"; the type name is the file name.
# Applications link QindaTK::qindatk only when they use the C++ API
# (DockModel from C++, Theme::instance()); QML-only users need the plugin
# on the import path and nothing else.

file(GLOB qindatk_qml_files RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}" CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/qml/*.qml")
file(GLOB qindatk_js_files RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}" CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/qml/*.js")

# Singleton QML types are marked here (pragma Singleton alone is not enough
# for a compiled module), and every file is published flat (Box.qml, not
# qml/Box.qml) so the installed module reads like any other Qt module.
foreach(qml_file IN LISTS qindatk_qml_files qindatk_js_files)
    get_filename_component(qml_name "${qml_file}" NAME)
    set_source_files_properties("${qml_file}" PROPERTIES QT_RESOURCE_ALIAS "${qml_name}")
    file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/${qml_file}" singleton_pragma REGEX "^pragma Singleton")
    if(singleton_pragma)
        set_source_files_properties("${qml_file}" PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
    endif()
endforeach()

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
        core/density.cpp core/density.h
        core/dock_model.cpp core/dock_model.h core/dock_model_p.h
        core/dock_model_ops.cpp
        core/dock_model_store.cpp
        core/flex.cpp core/flex.h
        core/grid.cpp core/grid.h
        core/grid_tracks.cpp core/grid_tracks.h
        core/icon.cpp core/icon.h
        core/icon_data.cpp core/icon_data.h
        core/layout_attached.cpp core/layout_attached.h
        core/layout_info.cpp core/layout_info.h
        core/stack.cpp core/stack.h
        core/svg_path.cpp core/svg_path.h
        core/theme.cpp core/theme.h
        core/theme_presets.cpp core/theme_presets.h
    QML_FILES ${qindatk_qml_files} ${qindatk_js_files}
)


target_include_directories(qindatk PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/core>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_link_libraries(qindatk
    PUBLIC Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick
    PRIVATE Qt6::Svg Qt6::QuickPrivate)

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
if(NOT DEFINED QINDATK_QML_INSTALL_DIR)
    set(QINDATK_QML_INSTALL_DIR "${QT6_INSTALL_PREFIX}/${QT6_INSTALL_QML}" CACHE PATH
        "Root QML import directory to install the QindaTK modules into")
endif()
set(QINDATK_QML_INSTALL_DIR "${QINDATK_QML_INSTALL_DIR}" PARENT_SCOPE)

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
