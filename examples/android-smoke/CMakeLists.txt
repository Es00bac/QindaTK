# SPDX-License-Identifier: LGPL-3.0-or-later
# QindaTK Android smoke app: org.qindatk.smoke (docs/android.md).
#
# Works two ways: added from the QindaTK tree (target `qindatk` exists), or
# as a standalone consumer of an installed/staged QindaTK:
#   qt-cmake -S examples/android-smoke -B build/smoke -DCMAKE_PREFIX_PATH=<stage>
cmake_minimum_required(VERSION 3.25)
if(NOT PROJECT_NAME STREQUAL "QindaTK")
    project(QindaTKSmoke LANGUAGES CXX)
    find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2)
    qt_standard_project_setup(REQUIRES 6.8)
    find_package(QindaTK REQUIRED)
    set(smoke_qindatk QindaTK::qindatk)
else()
    set(smoke_qindatk qindatk)
endif()

qt_add_executable(qindatk-android-smoke main.cpp)
qt_add_qml_module(qindatk-android-smoke
    URI QindaTKSmoke
    VERSION 1.0
    QML_FILES AndroidSmoke.qml
    NO_RESOURCE_TARGET_PATH)
target_link_libraries(qindatk-android-smoke PRIVATE ${smoke_qindatk}
    Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2)

# androiddeployqt bundles QML plugins only from the import paths it is given.
# In-tree the module's output directory is derived automatically; against an
# installed QindaTK it must be named (QindaTK_QML_IMPORT_PATH, from the package).
if(DEFINED QindaTK_QML_IMPORT_PATH)
    set_property(TARGET qindatk-android-smoke APPEND PROPERTY
        QT_QML_IMPORT_PATH "${QindaTK_QML_IMPORT_PATH}")
endif()

set_target_properties(qindatk-android-smoke PROPERTIES
    QT_ANDROID_PACKAGE_NAME "org.qindatk.smoke"
    QT_ANDROID_VERSION_NAME "0.1.0"
    QT_ANDROID_VERSION_CODE 1
    QT_ANDROID_MIN_SDK_VERSION 28
    QT_ANDROID_TARGET_SDK_VERSION 34)
