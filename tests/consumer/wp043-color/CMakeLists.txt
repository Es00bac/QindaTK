# SPDX-License-Identifier: LGPL-3.0-or-later
cmake_minimum_required(VERSION 3.25)
project(QindaTKColorConsumer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_package(Qt6 6.8 REQUIRED COMPONENTS Core)
find_package(QindaTK CONFIG REQUIRED)

add_executable(qindatk_wp043_color_consumer main.cpp)
target_link_libraries(qindatk_wp043_color_consumer PRIVATE
    QindaTK::qindatk Qt6::Core)
