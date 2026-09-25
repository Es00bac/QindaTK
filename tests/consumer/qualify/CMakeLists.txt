# SPDX-License-Identifier: LGPL-3.0-or-later
# WP-088 qualification consumer: a lightweight application built only from an
# installed QindaTK prefix. tools/qualify installs it into that prefix, moves
# the prefix, and runs it from there (docs/qualification.md).
cmake_minimum_required(VERSION 3.25)
project(QindaTKQualifyConsumer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
include(GNUInstallDirs)

find_package(QindaTK 0.1 CONFIG REQUIRED)
find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Qml Quick)
# Optional: the native dialog / print adapters exist only when QindaTK was
# built with Qt Widgets (QINDATK_ENABLE_WIDGETS). Their absence is reported.
find_package(QindaTKWidgets 0.1 CONFIG QUIET)

# AGENT-CONTRACT (tools/qualify/qualify_install.py): nothing absolute is
# compiled in. The executable finds the QindaTK import root and libqindatk
# relative to its own installed location, so the prefix can be moved.
get_filename_component(qualify_qml_root "${QindaTK_QML_DIR}" DIRECTORY)
get_filename_component(qualify_lib_dir "${QindaTK_DIR}/../.." ABSOLUTE)
set(qualify_bin_dir "${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}")
file(RELATIVE_PATH qualify_bin_to_qml "${qualify_bin_dir}" "${qualify_qml_root}")
file(RELATIVE_PATH qualify_bin_to_lib "${qualify_bin_dir}" "${qualify_lib_dir}")

qt_add_executable(qtk_qualify_consumer main.cpp)
qt_add_resources(qtk_qualify_consumer "qualify_qml" PREFIX "/qualify" FILES Main.qml)
target_link_libraries(qtk_qualify_consumer PRIVATE QindaTK::qindatk Qt6::Gui Qt6::Qml Qt6::Quick)
target_compile_definitions(qtk_qualify_consumer PRIVATE
    QUALIFY_BIN_TO_QML="${qualify_bin_to_qml}")
if(TARGET QindaTK::qindatk_widgets)
    find_package(Qt6 6.8 REQUIRED COMPONENTS Widgets)
    target_link_libraries(qtk_qualify_consumer PRIVATE QindaTK::qindatk_widgets Qt6::Widgets)
    target_compile_definitions(qtk_qualify_consumer PRIVATE QUALIFY_HAVE_WIDGETS=1)
endif()

# A QML-only application: it links Qt Quick but not libqindatk, so the
# installed QML plugin must find the library through its own RUNPATH.
qt_add_executable(qtk_qualify_qml_only qml_only.cpp)
qt_add_resources(qtk_qualify_qml_only "qualify_qml_only" PREFIX "/qualify" FILES Main.qml)
target_link_libraries(qtk_qualify_qml_only PRIVATE Qt6::Gui Qt6::Qml Qt6::Quick)
target_compile_definitions(qtk_qualify_qml_only PRIVATE
    QUALIFY_BIN_TO_QML="${qualify_bin_to_qml}")

set_target_properties(qtk_qualify_consumer qtk_qualify_qml_only PROPERTIES
    INSTALL_RPATH "$ORIGIN/${qualify_bin_to_lib}")
install(TARGETS qtk_qualify_consumer qtk_qualify_qml_only
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
