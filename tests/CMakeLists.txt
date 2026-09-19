# SPDX-License-Identifier: LGPL-3.0-or-later

# AGENT-CONTRACT: every test runs headless (offscreen platform, software
# renderer) and imports QindaTK from the build tree, so `ctest --preset dev`
# needs no display. Add a C++ test with qindatk_add_test(<name>) where
# cpp/<name>.cpp exists; QML tests go through tst_qml (QtQuickTest) and are
# any tst_*.qml file under qml/.

function(qindatk_add_test name)
    qt_add_executable(${name} cpp/${name}.cpp)
    qindatk_enable_warnings(${name})
    target_link_libraries(${name} PRIVATE qindatk Qt6::Test Qt6::Quick)
    target_compile_definitions(${name} PRIVATE QINDATK_BUILD_QML_DIR="${CMAKE_BINARY_DIR}/qml")
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QML_IMPORT_PATH=${CMAKE_BINARY_DIR}/qml")
endfunction()

qindatk_add_test(tst_grid_tracks)
qindatk_add_test(tst_svg_path)
qindatk_add_test(tst_theme)
qindatk_add_test(tst_dock_model)
qindatk_add_test(tst_layout)
qindatk_add_test(tst_icon)
qindatk_add_test(tst_stylus_handler)
qindatk_add_test(tst_app_window)
qindatk_add_test(tst_telemetry)
# The test registers synthetic pointing devices (QPointingDevicePrivate);
# the handler itself needs QuickPrivate, which qindatk links privately.
target_link_libraries(tst_stylus_handler PRIVATE Qt6::GuiPrivate Qt6::QuickPrivate)

# QML tests (QtQuickTest): one runner, every tst_*.qml in tests/qml.
qt_add_executable(tst_qml cpp/tst_qml.cpp)
target_link_libraries(tst_qml PRIVATE qindatk Qt6::QuickTest Qt6::Quick)
target_compile_definitions(tst_qml PRIVATE
    QUICK_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/qml"
    QINDATK_BUILD_QML_DIR="${CMAKE_BINARY_DIR}/qml")
add_test(NAME tst_qml COMMAND tst_qml -input "${CMAKE_CURRENT_SOURCE_DIR}/qml")
set_tests_properties(tst_qml PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QML_IMPORT_PATH=${CMAKE_BINARY_DIR}/qml")
