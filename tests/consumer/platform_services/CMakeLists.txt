# SPDX-License-Identifier: LGPL-3.0-or-later
cmake_minimum_required(VERSION 3.25)
project(QindaTKPlatformServicesConsumer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Qml Quick)
find_package(QindaTK CONFIG REQUIRED)
get_filename_component(QINDATK_QML_IMPORT_ROOT "${QindaTK_QML_DIR}" DIRECTORY)

add_executable(qindatk_platform_services_consumer main.cpp)
target_link_libraries(qindatk_platform_services_consumer PRIVATE
    QindaTK::qindatk Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick)
target_compile_definitions(qindatk_platform_services_consumer PRIVATE
    CONSUMER_QML_FILE="${CMAKE_CURRENT_SOURCE_DIR}/Main.qml"
    QINDATK_QML_IMPORT_ROOT="${QINDATK_QML_IMPORT_ROOT}")
