# SPDX-License-Identifier: LGPL-3.0-or-later
cmake_minimum_required(VERSION 3.25)
project(QindaTKWorkbookConsumer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(QindaTK CONFIG REQUIRED)
get_filename_component(QINDATK_QML_IMPORT_ROOT "${QindaTK_QML_DIR}" DIRECTORY)

add_executable(qindatk_workbook_consumer main.cpp)
target_link_libraries(qindatk_workbook_consumer PRIVATE QindaTK::qindatk)
target_compile_definitions(qindatk_workbook_consumer PRIVATE
    CONSUMER_QML_FILE="${CMAKE_CURRENT_SOURCE_DIR}/Main.qml"
    QINDATK_QML_IMPORT_ROOT="${QINDATK_QML_IMPORT_ROOT}")
