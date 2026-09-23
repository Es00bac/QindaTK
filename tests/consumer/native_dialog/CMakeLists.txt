# SPDX-License-Identifier: LGPL-3.0-or-later
cmake_minimum_required(VERSION 3.25)
project(QindaTKNativeDialogConsumer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_package(QindaTKWidgets CONFIG REQUIRED)

add_executable(qindatk_native_dialog_consumer main.cpp)
target_link_libraries(qindatk_native_dialog_consumer PRIVATE QindaTK::qindatk_widgets)
