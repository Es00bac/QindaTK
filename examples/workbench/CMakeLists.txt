# SPDX-License-Identifier: LGPL-3.0-or-later
cmake_minimum_required(VERSION 3.25)
project(QindaTKReferenceWorkbench LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
find_package(QindaTK 0.1 CONFIG REQUIRED)
find_package(Qt6 REQUIRED COMPONENTS Quick)
get_filename_component(QINDATK_QML_IMPORT_ROOT "${QindaTK_QML_DIR}" DIRECTORY)

add_executable(qindatk_workbench main.cpp workbench_host.h workbench_host.cpp
    workbench_lifecycle.cpp)
target_link_libraries(qindatk_workbench PRIVATE QindaTK::qindatk Qt6::Quick)
target_compile_definitions(qindatk_workbench PRIVATE
    WORKBENCH_QML_FILE="${CMAKE_CURRENT_SOURCE_DIR}/Main.qml"
    QINDATK_QML_IMPORT_ROOT="${QINDATK_QML_IMPORT_ROOT}")
