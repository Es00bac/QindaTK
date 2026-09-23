# SPDX-License-Identifier: LGPL-3.0-or-later
cmake_minimum_required(VERSION 3.24)
project(qindatk_atspi_fixture LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_package(Qt6 REQUIRED COMPONENTS Core Gui Qml Quick)
set(QINDA_BUILD_DIR "" CACHE PATH "Configured QindaTK build directory")
if(NOT QINDA_BUILD_DIR)
    message(FATAL_ERROR "Pass -DQINDA_BUILD_DIR=/path/to/QindaTK/build/wp017-atspi")
endif()
find_library(QINDA_LIBRARY qindatk PATHS "${QINDA_BUILD_DIR}/src" NO_DEFAULT_PATH REQUIRED)
add_executable(atspi_action_fixture atspi_action_fixture.cpp)
target_include_directories(atspi_action_fixture PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../../src/core")
target_link_libraries(atspi_action_fixture PRIVATE
    "${QINDA_LIBRARY}" Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick)
set_target_properties(atspi_action_fixture PROPERTIES
    BUILD_RPATH "${QINDA_BUILD_DIR}/src")
